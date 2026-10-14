#!/usr/bin/env python3
"""Rebuilds the unit's boot overlay so Open NaviCore is the default provider.

  patch_boot_overlay.py LIVE_BOOT_OVERLAY_IFS OUT_DIR [TOOLKIT_DIR]

LIVE_BOOT_OVERLAY_IFS  a copy of /datastore/ifs-overrides/boot_overlay.ifs.
OUT_DIR                new directory for boot_overlay.ifs and evidence.

Only bin/boot_overlay_main.sh changes. Its three one-shot navigation blocks
(v12, v13, final: an arm marker is consumed, then the config is redirected
for that boot only, so every reboot returns to the native NaviCore) are
replaced by a permanent block: mount /datastore/navicore/navigation.ifs and
link /pas/scfg/nv_navigation.cfg to its config on every boot. There is no
automatic return to the native provider. Manual rollback: create
/datastore/navicore/disabled (or remove navigation.ifs) and reboot.

Every other entry is extracted, rebuilt and checked byte-identical, with the
original modes, as for the earlier Media Studio rebuild.
"""
import hashlib
import os
import re
import subprocess
import sys

BLOCK = '''# Open NaviCore is the navigation provider; there is no fallback to the
# native NaviCore. navigation.ifs (mounted at /, paths under /open-navicore)
# carries the provider and its nv_navigation.cfg; the process-manager link
# below makes System Manager start nv_navigation with it on every boot.
# Rollback: create $NAVI_ROOT/disabled, or remove the image, and reboot.
if [ -e "$NAVI_ROOT/disabled" ]; then
    print "open_navicore_disabled_by_marker" >> "$LOG"
elif [ -r "$NAVI_IFS" ]; then
    /proc/boot/mountifs -v -f "$NAVI_IFS" -m / >> "$LOG" 2>&1
    print "open_navicore_mount_rc=$?" >> "$LOG"
    if [ -r /open-navicore/.open_navicore_navigation_v1 ] && \\
       [ -r /open-navicore/nv_navigation.cfg ] && \\
       [ -r /open-navicore/libNaviCore.so ]; then
        /proc/boot/ln -sPf /open-navicore/nv_navigation.cfg \\
            /pas/scfg/nv_navigation.cfg >> "$LOG" 2>&1
        print "open_navicore_config_link_rc=$?" >> "$LOG"
    else
        print "open_navicore_image_incomplete" >> "$LOG"
    fi
else
    print "open_navicore_image_absent" >> "$LOG"
fi

'''
VARIABLES = '''NAVI_ROOT=/datastore/navicore
NAVI_IFS=$NAVI_ROOT/navigation.ifs
'''
ENTRIES = [  # path, mode (from the live image)
    ('.ifs_boot_overlay_padding.txt', '0400'),
    ('bin/boot_overlay_main.sh', '01555'),
    ('bin/boot_overlay_post.sh', '01555'),
    ('ssh/.ssh/sshd_config', '0600'),
    ('etc/usblauncher/bsp_asix_Interface.sh', '01555'),
    ('ssh/.ssh/authorized_keys.root', '0600'),
    ('etc/usblauncher/bsp_stop_asix_Interface.sh', '01555'),
    ('ssh/.ssh/banner', '0444'),
    ('etc/passwd', '0444'),
    ('etc/group', '0444'),
]


def sha256(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def extract(toolkit, image, into):
    for path, _ in ENTRIES:
        os.makedirs(os.path.join(into, os.path.dirname(path)), exist_ok=True)
    subprocess.run([os.path.join(toolkit, 'dumpifs.sh'), '-x', os.path.abspath(image)],
                   cwd=into, check=True, stdout=subprocess.DEVNULL)
    for path, _ in ENTRIES:
        if not os.path.isfile(os.path.join(into, path)):
            raise SystemExit('entry missing from the live image: ' + path)


def patch(script):
    start = script.index('# Transactional, navigation-only one-boot activation.')
    end = script.index('# Optional, reversible custom-Xlet test.')
    script = script[:start] + BLOCK + script[end:]
    # The one-shot variables are gone with their blocks.
    script = re.sub(r'NAVI_(V12|V13|FINAL)_[A-Z]+=.*\n', '', script)
    anchor = 'USERLAND_IFS=/datastore/ifs-overrides/userland_overlay.ifs\n'
    if anchor not in script:
        raise SystemExit('unexpected boot_overlay_main.sh layout')
    script = script.replace(anchor, anchor + VARIABLES, 1)
    if 'NAVI_V1' in script or 'NAVI_FINAL' in script:
        raise SystemExit('one-shot navigation references remain')
    return script


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    live, out = sys.argv[1:3]
    toolkit = sys.argv[3] if len(sys.argv) > 3 else '/mnt/d/qnx660-mkifs-toolkit'
    if os.path.exists(out):
        raise SystemExit('refusing to overwrite existing ' + out)
    os.makedirs(out)
    before = os.path.join(out, 'before')
    entries = os.path.join(out, 'entries')
    os.makedirs(before)
    extract(toolkit, live, before)

    os.makedirs(entries)
    for path, _ in ENTRIES:
        source = os.path.join(before, path)
        target = os.path.join(entries, path)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        data = open(source, 'rb').read()
        if path == 'bin/boot_overlay_main.sh':
            data = patch(data.decode('utf-8')).encode('utf-8')
        open(target, 'wb').write(data)
        os.chmod(target, 0o644)

    build = os.path.join(out, 'boot_overlay.build')
    with open(build, 'w') as f:
        f.write('[-bigendian]\n')
        for path, mode in ENTRIES:
            f.write('[uid=0 gid=0 perms=%s] %s = entries/%s\n' % (mode, path, path))
    subprocess.run([os.path.join(toolkit, 'mkifs.sh'), '-nn', 'boot_overlay.build',
                    'boot_overlay.ifs'], cwd=out, check=True)

    # Readback: every entry but the patched script is byte-identical.
    check = os.path.join(out, 'readback')
    os.makedirs(check)
    extract(toolkit, os.path.join(out, 'boot_overlay.ifs'), check)
    report = []
    for path, _ in ENTRIES:
        old, new = sha256(os.path.join(before, path)), sha256(os.path.join(check, path))
        expect_same = path != 'bin/boot_overlay_main.sh'
        if expect_same and old != new:
            raise SystemExit('entry changed unexpectedly: ' + path)
        if new != sha256(os.path.join(entries, path)):
            raise SystemExit('readback differs from input: ' + path)
        report.append('%s %s  %s' % ('same   ' if old == new else 'CHANGED', new, path))
    subprocess.run(['diff', '-u', os.path.join(before, 'bin/boot_overlay_main.sh'),
                    os.path.join(entries, 'bin/boot_overlay_main.sh')],
                   stdout=open(os.path.join(out, 'boot_overlay_main.diff'), 'w'))
    report.append('%s  boot_overlay.ifs (new)' % sha256(os.path.join(out, 'boot_overlay.ifs')))
    report.append('%s  boot_overlay.ifs (live input)' % sha256(live))
    open(os.path.join(out, 'REPORT'), 'w').write('\n'.join(report) + '\n')
    print('\n'.join(report))


if __name__ == '__main__':
    main()
