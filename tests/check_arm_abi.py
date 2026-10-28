#!/usr/bin/env python3
"""ARM calling-convention checks on the provider's compiled objects.

  OBJDUMP=<ntoarmv7-objdump> python3 tests/check_arm_abi.py OBJECT...

The stock nv_navigation calls the provider (and the provider calls stock PAS
proxies) with the QNX ARM EABI soft-float convention: the first four words in
r0-r3, the rest on the caller's stack, floats in core registers. The checks
below pin, for recovered provider entries and callback wrappers, where each
argument is read and forwarded. They are compiler-independent: the frame of
each function is computed from its prologue (push/stmdb plus sub sp), so the
caller's stack words are expected at sp+frame+4*i, and any register may hold
a value.
"""
import os
import re
import subprocess
import sys

REG = r'(?:r\d+|ip|lr|fp|sl|sb)'


def disassemble(objdump, objects):
    """{function: [instruction text]} from objdump -d."""
    out = subprocess.run([objdump, '-dr'] + objects, check=True, capture_output=True,
                         text=True).stdout
    functions, current = {}, None
    for line in out.splitlines():
        head = re.match(r'^[0-9a-f]+ <([^>]+)>:$', line)
        if head:
            current = functions.setdefault(head.group(1), [])
            continue
        ins = re.match(r'^\s+[0-9a-f]+:\s+(?:[0-9a-f]{2,8}\s)+\s*(\S.*)$', line)
        if current is not None and ins:
            text = re.sub(r'\s*;.*$', '', ins.group(1)).strip()
            current.append(re.sub(r'\s+', ' ', text))
        elif current is not None and 'R_ARM_' in line:
            current.append('reloc ' + line.strip())
    return functions


def frame_size(body):
    """Bytes between the caller's sp and this function's sp after the
    prologue: pushed registers plus the local frame."""
    size = 0
    for ins in body[:12]:
        m = re.match(r'(?:push|stmdb(?:\.w)?\s+sp!,)\s*\{([^}]*)\}', ins.replace('push.w', 'push'))
        if m and size == 0:
            regs = 0
            for part in m.group(1).split(','):
                part = part.strip()
                r = re.match(r'r(\d+)-r(\d+)$', part)
                regs += int(r.group(2)) - int(r.group(1)) + 1 if r else 1
            size += 4 * regs
            continue
        m = re.match(r'sub(?:\.w|w)?\s+sp,\s*(?:sp,\s*)?#(\d+)', ins)
        if m:
            size += int(m.group(1))
    return size


class Checker:
    def __init__(self, functions):
        self.functions = functions
        self.failures = []

    def body(self, name):
        if name not in self.functions:
            self.failures.append('%s: not found' % name)
            return []
        return self.functions[name]

    def expect(self, ok, name, what):
        if not ok:
            self.failures.append('%s: %s' % (name, what))

    def reads_register(self, name, reg):
        """`reg` is read before anything writes it (the argument is used)."""
        for ins in self.body(name):
            op, _, args = ins.partition(' ')
            operands = [a.strip() for a in re.split(r',(?![^\[]*\])', args)]
            if not operands or not operands[0]:
                continue
            dest, sources = operands[0], ' '.join(operands[1:])
            if op.startswith(('str', 'push', 'stm', 'cmp', 'cbz', 'cbnz', 'tst')):
                sources = args
                dest = ''
            if re.search(r'\b%s\b' % reg, sources):
                return True
            if dest == reg or (op.startswith(('bl', 'blx')) and reg in ('r0', 'r1', 'r2', 'r3')):
                return False
        return False

    def loads(self, name, offset, byte=False):
        op = r'ldrb(?:\.w)?' if byte else r'ldr(?:b|h|d)?(?:\.w)?'
        pattern = r'^%s\s+%s(?:,\s*%s)?,\s*\[sp(?:,\s*#%d)?\]' % (op, REG, REG, offset)
        if offset == 0:
            pattern = r'^%s\s+%s(?:,\s*%s)?,\s*\[sp(?:,\s*#0)?\]' % (op, REG, REG)
        return any(re.match(pattern, ins) for ins in self.body(name))

    def stores(self, name, offset):
        """A word store to outgoing sp+offset (str, or the second word of strd)."""
        for ins in self.body(name):
            m = re.match(r'^str(?:b|h)?(?:\.w)?\s+%s,\s*\[sp(?:,\s*#(\d+))?\]' % REG, ins)
            if m and int(m.group(1) or 0) == offset:
                return True
            m = re.match(r'^strd(?:\.w)?\s+%s,\s*%s,\s*\[sp(?:,\s*#(\d+))?\]' % (REG, REG), ins)
            if m and offset in (int(m.group(1) or 0), int(m.group(1) or 0) + 4):
                return True
        return False

    def indirect_call(self, name):
        return any(re.match(r'^blx\s+%s$' % REG, ins) for ins in self.body(name))

    def stack_arguments(self, name, words, forwarded=None, byte_first=False):
        """The caller's stack words 0..words-1 are loaded; `forwarded` maps
        incoming word index -> outgoing sp offset."""
        frame = frame_size(self.body(name))
        for i in range(words):
            self.expect(self.loads(name, frame + 4 * i, byte=byte_first and i == 0), name,
                        'incoming stack word %d (sp+%d) not loaded' % (i, frame + 4 * i))
        for i, out in (forwarded or {}).items():
            self.expect(self.stores(name, out), name,
                        'incoming word %d not forwarded to outgoing sp+%d' % (i, out))


def main():
    objdump = os.environ.get('OBJDUMP', 'ntoarmv7-objdump')
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    c = Checker(disassemble(objdump, sys.argv[1:]))

    # Soft-float: VehicleData slot 8's float bits arrive in r1. Recovered
    # uint8_t provider words arrive in ordinary core registers.
    for name, reg in (('open_navi_vehicle_speed', 'r1'),
                      ('open_navi_vehicle_day_night_status', 'r1'),
                      ('open_navi_vehicle_day_night_setting', 'r1'),
                      ('nc_provider_vehicle_day_night_status_reply', 'r2'),
                      ('nc_provider_vehicle_day_night_setting_reply', 'r2'),
                      ('open_navi_10_17', 'r3'),
                      ('open_navi_audio_volume', 'r2'),
                      ('open_navi_vehicle_speed', 'r2')):
        c.expect(c.reads_register(name, reg), name, 'argument in %s not used' % reg)

    # Connectivity slot 3: the fourth word after self is the caller's sp[0].
    c.stack_arguments('open_navi_20_3', 1)
    # HMI signalPressStateAnimation: incoming sp[0] (y) to outgoing sp[0].
    c.stack_arguments('nc_host_hmi_signal_press_state_animation', 1, {0: 0})
    c.expect(c.indirect_call('nc_host_hmi_signal_press_state_animation'),
             'nc_host_hmi_signal_press_state_animation', 'no indirect call')
    # Logging slot 4: three stack words (the first a byte) forwarded.
    c.stack_arguments('nc_host_logging_signal_write_log_data', 3, {0: 0, 1: 4, 2: 8},
                      byte_first=True)
    c.expect(c.indirect_call('nc_host_logging_signal_write_log_data'),
             'nc_host_logging_signal_write_log_data', 'no indirect call')
    # Voice slot 19: four pointer words forwarded.
    c.stack_arguments('nc_host_voice_reply_location_get', 4, {0: 0, 1: 4, 2: 8, 3: 12})
    c.expect(c.indirect_call('nc_host_voice_reply_location_get'),
             'nc_host_voice_reply_location_get', 'no indirect call')
    c.expect(any(re.match(r'^blx\s+%s$' % REG, i)
                 for i in c.body('nc_host_root_no_arg_internal_state_cleanup')),
             'nc_host_root_no_arg_internal_state_cleanup', 'no indirect call')

    # Concrete-vtable pointer seams stay synchronous wrappers with owned
    # stack storage before the indirect callback.
    for name in ('nc_host_ews_reply_ews_area', 'nc_host_rds_signal_selection_filter',
                 'nc_host_tpeg_signal_select_station', 'nc_host_tpeg_signal_select_station_list',
                 'nc_host_gps_signal_map_matched_position',
                 'nc_host_vics_signal_vics_wide_emergency_warning',
                 'nc_host_vehicle_signal_can_message', 'nc_host_i18n_reply_time_zone_information'):
        c.expect(frame_size(c.body(name)) > 0 and
                 any(re.match(r'^sub(?:\.w|w)?\s+sp', i) for i in c.body(name)),
                 name, 'no owned stack storage')
        c.expect(c.indirect_call(name), name, 'no indirect call')
    # The opaque SoftwareInfrastructure row calls slot 11 (+44) directly and
    # never copies or retains.
    opaque = 'nc_host_root_supplier_specific_server_function'
    c.expect(any(re.match(r'^ldr(?:\.w)?\s+%s,\s*\[%s,\s*#44\]' % (REG, REG), i)
                 for i in c.body(opaque)), opaque, 'slot +44 not loaded')
    c.expect(c.indirect_call(opaque), opaque, 'no indirect call')
    c.expect(not any('memcpy' in i for i in c.body(opaque)), opaque, 'copies (memcpy)')
    # VICS: minute and pointer at the caller's sp[0]/sp[4]; the owned record
    # is forwarded at outgoing sp+4.
    c.stack_arguments('nc_host_vics_signal_vics_wide_emergency_warning', 2, {0: 0, 1: 4})

    if c.failures:
        raise SystemExit('ARM ABI check failed:\n  ' + '\n  '.join(c.failures))
    print('ARM scalar/callback ABI checks passed (%d objects)' % (len(sys.argv) - 1))


if __name__ == '__main__':
    main()
