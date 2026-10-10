#!/bin/sh
set -eu

object=$1
callback_object=${2:?callback object required}
objdump_tool=${OBJDUMP:-ntoarmv7-objdump}
dump_file=${TMPDIR:-/tmp}/mock-navicore-abi-$$.txt
trap 'rm -f "$dump_file"' EXIT HUP INT TERM

"$objdump_tool" -dr "$object" "$callback_object" > "$dump_file"

# QNX's soft-float ABI passes VehicleData slot 8's float bits in r1.
sed -n '/<mock_vehicle_speed>:/,/^$/p' "$dump_file" |
    grep -Eq 'mov[[:space:]]+r[4-9], r1'

# Recovered uint8_t provider words arrive in ordinary AAPCS core registers.
# Preserve the incoming word through the typed wrapper; do not accidentally
# fetch it from a guessed byte pointer or stack slot while adding V2 routing.
sed -n '/<mock_vehicle_day_night_status>:/,/^$/p' "$dump_file" |
    grep -Eq 'mov[[:space:]]+r[4-9], r1'
sed -n '/<mock_vehicle_day_night_setting>:/,/^$/p' "$dump_file" |
    grep -Eq 'mov[[:space:]]+r[4-9], r1'
for function in mock_8_3 mock_8_5; do
    sed -n "/<${function}>:/,/^$/p" "$dump_file" |
        grep -Eq 'mov[[:space:]]+r[4-9], r2'
done
sed -n '/<mock_10_17>:/,/^$/p' "$dump_file" |
    grep -Eq 'mov[[:space:]]+r[4-9], r3'
sed -n '/<mock_audio_volume>:/,/^$/p' "$dump_file" |
    grep -Eq 'mov[[:space:]]+r[4-9], r2'

# Connectivity slot 3 has four arguments after self, so the fourth arrives at
# the caller's stack top and must be loaded without width conversion.  The
# lifecycle lease gives current QNX GCC a larger callee-save/local frame
# (sp+72); retain the older compact sp+24 shape as an accepted codegen variant.
sed -n '/<mock_20_3>:/,/^$/p' "$dump_file" |
    grep -Eq 'ldr[[:space:]]+r[4-9], \[sp, #(24|72)\]'

# HMI signalPressStateAnimation has four arguments after self. Its y word is
# incoming sp[0]. The wrapper's three-register save plus 12-byte local frame
# moves that incoming word to current sp+24, then writes it to outgoing sp[0]
# before BLX. r1/r2/r3 remain the press state, ignored ABI placeholder, and x.
press_body=$(sed -n \
    '/<mock_pas_1_13_signalPressStateAnimation>:/,/^$/p' "$dump_file")
printf '%s\n' "$press_body" | grep -Eq 'ldr[[:space:]]+r[4-9], \[sp, #24\]'
printf '%s\n' "$press_body" | grep -Eq 'str[[:space:]]+r[4-9], \[sp, #0\]'
printf '%s\n' "$press_body" | grep -Eq 'blx[[:space:]]+r[4-9]'

# Logging slot 4 has three stack words after r1-r3. A 12-byte push plus a
# 20-byte local frame preserves 8-byte alignment, so incoming sp+0/+4/+8 are
# current sp+32/+36/+40 and are forwarded to outgoing sp+0/+4/+8.
logging_body=$(sed -n \
    '/<mock_pas_6_4_signalWriteLogData>:/,/^$/p' "$dump_file")
printf '%s\n' "$logging_body" | grep -Eq 'sub[[:space:]]+sp, #20'
printf '%s\n' "$logging_body" | grep -Eq 'ldrb[^[]*\[sp, #32\]'
printf '%s\n' "$logging_body" | grep -Eq 'ldr[[:space:]]+r[4-9], \[sp, #36\]'
printf '%s\n' "$logging_body" | grep -Eq 'ldr[[:space:]]+r[4-9], \[sp, #40\]'
printf '%s\n' "$logging_body" | grep -Eq 'str[[:space:]]+r[4-9], \[sp, #0\]'
printf '%s\n' "$logging_body" | grep -Eq 'str[[:space:]]+r[4-9], \[sp, #4\]'
printf '%s\n' "$logging_body" | grep -Eq 'str[[:space:]]+r[4-9], \[sp, #8\]'
printf '%s\n' "$logging_body" | grep -Eq 'blx[[:space:]]+r[4-9]'

# Voice slot 19 similarly forwards four incoming pointer words from current
# sp+32/+36/+40/+44 to outgoing sp+0/+4/+8/+12 before the indirect call.
voice_body=$(sed -n \
    '/<mock_pas_25_19_replyLocationGet>:/,/^$/p' "$dump_file")
printf '%s\n' "$voice_body" | grep -Eq 'sub[[:space:]]+sp, #20'
for offsets in '32 0' '36 4' '40 8' '44 12'; do
    incoming=${offsets% *}
    outgoing=${offsets#* }
    printf '%s\n' "$voice_body" |
        grep -Eq "ldr[[:space:]]+r[4-9], \\[sp, #${incoming}\\]"
    printf '%s\n' "$voice_body" |
        grep -Eq "str[[:space:]]+r[4-9], \\[sp, #${outgoing}\\]"
done
printf '%s\n' "$voice_body" | grep -Eq 'blx[[:space:]]+r[4-9]'

root3_body=$(sed -n '/<mock_pas_root_3>:/,/^$/p' "$dump_file")
printf '%s\n' "$root3_body" | grep -Eq 'blx[[:space:]]+r[0-9]'

# The concrete-vtable pointer seams must remain explicit synchronous ARM
# wrappers. Eight bounded rows allocate owned stack storage before the
# indirect callback (small constant copies may be inlined by QNX GCC). The
# opaque SoftwareInfrastructure row performs the BLX directly and must never
# acquire a memcpy/retention path.
for function in \
    mock_pas_32_3_replyEWSArea \
    mock_pas_14_7_signalSelectionFilter \
    mock_pas_27_5_signalSelectStation \
    mock_pas_27_6_signalSelectStationList \
    mock_pas_9_5_signalMapMatchedPosition \
    mock_pas_28_5_signalVICSWideEmergencyWarning \
    mock_pas_8_10_signalCANMessage \
    mock_pas_7_5_replyTimeZoneInformation; do
    body=$(sed -n "/<${function}>:/,/^$/p" "$dump_file")
    printf '%s\n' "$body" | grep -Eq 'sub(\.w)?[[:space:]]+sp'
    printf '%s\n' "$body" | grep -Eq 'blx[[:space:]]+(r[4-9]|ip|lr)'
done

opaque_body=$(sed -n \
    '/<mock_pas_root_11_supplierSpecificServerFunction>:/,/^$/p' \
    "$dump_file")
printf '%s\n' "$opaque_body" | grep -Eq 'ldr[^[]*\[r[0-9]+, #44\]'
printf '%s\n' "$opaque_body" | grep -Eq 'blx[[:space:]]+r[4-9]'
! printf '%s\n' "$opaque_body" | grep -q 'memcpy'

# VICS has five words after self: minute and pointer arrive at incoming sp+0
# and sp+4. The owned 0x180-byte record is then forwarded at outgoing sp+4.
vics_body=$(sed -n \
    '/<mock_pas_28_5_signalVICSWideEmergencyWarning>:/,/^$/p' \
    "$dump_file")
printf '%s\n' "$vics_body" | grep -Eq 'ldr[^[]*\[sp, #440\]'
printf '%s\n' "$vics_body" | grep -Eq 'ldr[^[]*\[sp, #444\]'
printf '%s\n' "$vics_body" | grep -Eq 'str[^[]*\[sp(, #0)?\]'
printf '%s\n' "$vics_body" | grep -Eq 'str[^[]*\[sp, #4\]'

echo "ARM scalar/callback ABI checks passed: $object $callback_object"
