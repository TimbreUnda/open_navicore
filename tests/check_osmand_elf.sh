#!/bin/sh
# Deliberately separate from the stricter reference-plugin profile.
set -eu
plugin=${1:?usage: check_osmand_elf.sh plugin.so}
readelf_bin=${READELF:-readelf}
header=$($readelf_bin -h "$plugin")
dynamic=$($readelf_bin -d "$plugin")
symbols=$($readelf_bin --dyn-syms --wide "$plugin")

printf '%s\n' "$header" | grep -q 'Class:.*ELF32'
printf '%s\n' "$header" | grep -q 'Data:.*little endian'
printf '%s\n' "$header" | grep -q 'Type:.*DYN'
printf '%s\n' "$header" | grep -q 'Machine:.*ARM'
printf '%s\n' "$header" | grep -q 'Flags:.*Version5 EABI.*soft-float ABI'
printf '%s\n' "$dynamic" | grep -q 'SONAME.*\[libOpenNaviBackendOsmAnd.so\]'

if printf '%s\n' "$dynamic" | grep -Eq '(RUNPATH|TEXTREL)'; then
    echo "OsmAnd plugin contains forbidden RUNPATH/TEXTREL" >&2
    exit 1
fi
rpath=$(printf '%s\n' "$dynamic" | sed -n 's/.*RPATH.*\[\([^]]*\)\].*/\1/p')
test "$rpath" = /open-navicore || {
    echo "unexpected OsmAnd RPATH: $rpath" >&2
    exit 1
}
needed=$(printf '%s\n' "$dynamic" | sed -n 's/.*NEEDED.*\[\([^]]*\)\].*/\1/p' | sort -u)
expected='libEGL.so.1
libGLESv2.so.1
libOsmAndCore_shared.so
libc.so.3
libm.so.2'
test "$needed" = "$expected" || {
    echo "OsmAnd dependencies differ from reviewed exact set: $needed" >&2
    exit 1
}
exports=$(printf '%s\n' "$symbols" | awk \
    '($5 == "GLOBAL" || $5 == "WEAK" || $5 == "UNIQUE") && \
     ($6 == "DEFAULT" || $6 == "PROTECTED") && $7 != "UND" && \
     $4 != "FILE" && $4 != "SECTION" { print $8 }' | sed 's/@.*//' | sort -u)
test "$exports" = open_navi_backend_query_v1 || {
    echo "unexpected OsmAnd exports: $exports" >&2
    exit 1
}
printf '%s\n' "$symbols" | awk \
    '$4 == "FUNC" && $5 == "GLOBAL" && $6 == "DEFAULT" && $7 != "UND" && \
     $8 == "open_navi_backend_query_v1" { found = 1 } END { exit !found }'
echo "OsmAnd plugin ELF checks passed: $plugin"
