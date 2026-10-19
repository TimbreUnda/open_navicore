#!/bin/sh
set -eu

elf=${1:?usage: check_elf.sh path/to/libNaviCore.so}
readelf_bin=${READELF:-readelf}

header=$($readelf_bin -h "$elf")
symbols=$($readelf_bin -Ws "$elf")
dynamic=$($readelf_bin -d "$elf")

printf '%s\n' "$header" | grep -q 'Class:.*ELF32'
printf '%s\n' "$header" | grep -q 'Data:.*little endian'
printf '%s\n' "$header" | grep -q 'Machine:.*ARM'
printf '%s\n' "$header" | grep -q 'Flags:.*Version5 EABI.*soft-float ABI'
printf '%s\n' "$dynamic" | grep -q 'SONAME.*\[libNaviCore.so\]'

for library in libscreen.so.1 libEGL.so.1 libGLESv2.so.1; do
    printf '%s\n' "$dynamic" | grep -q "NEEDED.*\[$library\]" || {
        echo "missing native renderer dependency: $library" >&2
        exit 1
    }
done

for symbol in panaVersionCheck panaInitialize panaGetSoftwareInfrastructure panaDeinitialize \
    open_navi_backend_install_v1 open_navi_backend_install_v2 \
	    open_navi_backend_install_v3 open_navi_backend_install_v4 \
        open_navi_backend_install_v5 open_navi_backend_install_v6 \
    open_navi_cluster_backend_serialize open_navi_cluster_backend_publish \
    open_navi_cluster_backend_reset; do
    count=$(printf '%s\n' "$symbols" | awk -v wanted="$symbol" '$4 == "FUNC" && $5 == "GLOBAL" && $8 == wanted { n++ } END { print n+0 }')
    test "$count" -ge 1 || {
        echo "missing exported function: $symbol" >&2
        exit 1
    }
    value=$(printf '%s\n' "$symbols" | awk -v wanted="$symbol" \
        '$4 == "FUNC" && $5 == "GLOBAL" && $8 == wanted { print $2; exit }')
    test $((0x$value % 2)) -eq 1 || {
        echo "export is not marked as a Thumb function: $symbol ($value)" >&2
        exit 1
    }
done

for symbol in dlopen dlsym dlerror; do
    printf '%s\n' "$symbols" | awk -v wanted="$symbol" \
        '$4 == "FUNC" && $5 == "GLOBAL" && $7 == "UND" && $8 == wanted \
         { found=1 } END { exit found ? 0 : 1 }' || {
        echo "missing backend-loader import: $symbol" >&2
        exit 1
    }
done

application_exports=$(printf '%s\n' "$symbols" | awk \
    '$4 == "FUNC" && $5 == "GLOBAL" && $7 != "UND" && \
     $8 !~ /^_/ { print $8 }' | sort -u)
expected_exports=$(printf '%s\n' \
    open_navi_cluster_backend_publish \
    open_navi_cluster_backend_reset \
    open_navi_cluster_backend_serialize \
    open_navi_backend_install_v1 \
    open_navi_backend_install_v2 \
	    open_navi_backend_install_v3 \
    open_navi_backend_install_v4 \
    open_navi_backend_install_v5 \
    open_navi_backend_install_v6 \
    panaDeinitialize \
    panaGetSoftwareInfrastructure \
    panaInitialize \
    panaVersionCheck | sort -u)
test "$application_exports" = "$expected_exports" || {
    echo "unexpected provider application exports:" >&2
    printf '%s\n' "$application_exports" >&2
    exit 1
}

echo "ELF and export checks passed: $elf"
