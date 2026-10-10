#!/bin/sh
set -eu

matrix=${1:?callback matrix path required}
manifest=${2:?coverage manifest path required}
source_file=${3:?emission source path required}
temporary=$(mktemp -d /tmp/mock-callback-coverage.XXXXXX)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

tail -n +2 "$matrix" | awk -F, '{ print $1 "/" $3 }' | sort >"$temporary/matrix"
tail -n +2 "$manifest" | awk -F, '{ print $1 "/" $2 }' | sort >"$temporary/manifest"

test "$(wc -l <"$temporary/matrix")" -eq 61
test "$(wc -l <"$temporary/manifest")" -eq 61
test "$(uniq -d "$temporary/matrix" | wc -l)" -eq 0
test "$(uniq -d "$temporary/manifest" | wc -l)" -eq 0
cmp "$temporary/matrix" "$temporary/manifest"

awk -F, '
    NR == 1 {
        if ($1 != "adapter" || $2 != "slot" || $3 != "state" ||
            $4 != "symbol" || $5 != "emitted") exit 1
        next
    }
    $3 != "implemented_typed" && $3 != "disabled_needs_trace" &&
        $3 != "unknown_not_emitted" { exit 1 }
    $3 == "implemented_typed" && $4 == "" { exit 1 }
    $3 != "implemented_typed" && $4 != "" { exit 1 }
    $5 != "no" && $3 != "implemented_typed" { exit 1 }
' "$manifest"
test "$(tail -n +2 "$manifest" |
    awk -F, '$3 == "implemented_typed" { count++ } END { print count + 0 }')" \
    -eq 61

tail -n +2 "$manifest" | awk -F, '$4 != "" { print $4 }' | sort >"$temporary/typed"
while IFS= read -r symbol; do
    grep -q "${symbol}(" include/mock_pas_callbacks.h
    grep -q "$symbol" src/compat/qr39/mock_pas_callbacks.c
done <"$temporary/typed"
grep -o 'mock_pas_[a-zA-Z0-9_]*' "$source_file" |
    grep -v '^mock_pas_callbacks$' | sort -u >"$temporary/used"
comm -23 "$temporary/used" "$temporary/typed" >"$temporary/untyped-used"
test ! -s "$temporary/untyped-used"
if grep -Eq '\(\([^)]*\(\*\)[^)]*\)[a-zA-Z_]*vtable\[[0-9]' "$source_file"; then
    echo "direct callback vtable invocation bypasses typed catalog" >&2
    exit 1
fi

tail -n +2 "$manifest" | awk -F, '$5 != "no" { print $4 }' | sort -u |
while IFS= read -r symbol; do
    grep -q "${symbol}(" "$source_file"
done

echo "Callback coverage passed: 61 rows, all emitted callbacks typed"
