#!/bin/sh
set -eu
# Byte order for sort/comm: the expected lists are in C-locale order.
export LC_ALL=C

extract_exports() {
    awk '($5 == "GLOBAL" || $5 == "WEAK" || $5 == "UNIQUE") && \
         ($6 == "DEFAULT" || $6 == "PROTECTED") && $7 != "UND" && \
         $4 != "FILE" && $4 != "SECTION" { print $8 }' | \
        sed 's/@.*//' | sort -u
}

check_exact_exports() {
    actual=$(printf '%s\n' "$1" | extract_exports)
    test "$actual" = open_navi_backend_query_v1
}

if [ "${1:-}" = --self-test ]; then
    query='1: 00000001 4 FUNC GLOBAL DEFAULT 9 open_navi_backend_query_v1'
    check_exact_exports "$query"
    object="$query
2: 00002000 4 OBJECT GLOBAL DEFAULT 16 unexpected_object"
    if check_exact_exports "$object"; then
        echo "exact-export self-test accepted a global object" >&2
        exit 1
    fi
    prefixed_tls="$query
3: 00002004 4 TLS GLOBAL DEFAULT 17 OPEN_NAVI_BACKEND_REFERENCE_ESCAPE"
    if check_exact_exports "$prefixed_tls"; then
        echo "exact-export self-test accepted a prefixed TLS symbol" >&2
        exit 1
    fi
    weak_protected="$query
4: 00001000 4 FUNC WEAK PROTECTED 9 weak_protected_export"
    if check_exact_exports "$weak_protected"; then
        echo "exact-export self-test accepted a weak protected symbol" >&2
        exit 1
    fi
    echo "plugin exact-export self-test passed"
    exit 0
fi

plugin=${1:?usage: check_plugin_elf.sh plugin.so|--self-test}
readelf_bin=${READELF:-readelf}

header=$($readelf_bin -h "$plugin")
symbols=$($readelf_bin -Ws "$plugin")
dynamic=$($readelf_bin -d "$plugin")

printf '%s\n' "$header" | grep -q 'Class:.*ELF32'
printf '%s\n' "$header" | grep -q 'Data:.*little endian'
printf '%s\n' "$header" | grep -q 'Machine:.*ARM'
printf '%s\n' "$header" | grep -q 'Flags:.*Version5 EABI.*soft-float ABI'

if printf '%s\n' "$dynamic" | grep -Eq '(RPATH|RUNPATH|TEXTREL)'; then
    echo "plugin contains forbidden RPATH/RUNPATH/TEXTREL" >&2
    exit 1
fi

needed=$(printf '%s\n' "$dynamic" | \
    sed -n 's/.*NEEDED.*\[\([^]]*\)\].*/\1/p' | sort -u)
for library in $needed; do
    case "$library" in
        libc.so.3|libGLESv2.so.1) ;;
        *) echo "unexpected plugin dependency: $library" >&2; exit 1 ;;
    esac
done
expected_needed='libGLESv2.so.1
libc.so.3'
if test "$needed" != "$expected_needed"; then
    echo "plugin dependencies differ from reviewed exact set: $needed" >&2
    exit 1
fi

exports=$(printf '%s\n' "$symbols" | extract_exports)
check_exact_exports "$symbols" || {
    echo "unexpected plugin exports: $exports" >&2
    exit 1
}
value=$(printf '%s\n' "$symbols" | awk \
    '$4 == "FUNC" && $5 == "GLOBAL" && $8 == "open_navi_backend_query_v1" \
     { print $2; exit }')
test $((0x$value % 2)) -eq 1 || {
    echo "plugin query export is not Thumb: $value" >&2
    exit 1
}

echo "reference plugin ELF checks passed: $plugin"
