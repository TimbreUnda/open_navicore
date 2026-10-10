#!/bin/sh
# Build a hash-pinned VP4R mock-NaviCore deployment directory on the host.

set -eu

KNOWN_STOCK_CFG_SHA256=c889c2b4c5d1d5ddb4b32c0c79a46c8482950ee0db783a83b1ed949aef7d680e
RADIO_PROVIDER=/datastore/navicore-mock/provider/libNaviCore_mock.so

die()
{
    echo "ERROR: $*" >&2
    exit 1
}

usage()
{
    echo "usage: $0 PROVIDER_SO STOCK_NV_NAVIGATION_CFG OUTPUT_DIR [QNX_OPENSSL]" >&2
    exit 64
}

[ "$#" -eq 3 ] || [ "$#" -eq 4 ] || usage
PROVIDER=$1
STOCK_CFG=$2
OUTPUT=$3
QNX_OPENSSL=${4:-}

[ -f "$PROVIDER" ] || die "provider not found: $PROVIDER"
[ -f "$STOCK_CFG" ] || die "stock config not found: $STOCK_CFG"
[ ! -e "$OUTPUT" ] || die "refusing to overwrite existing output: $OUTPUT"
command -v readelf >/dev/null 2>&1 || die "readelf is required"
command -v sha256sum >/dev/null 2>&1 || die "sha256sum is required"

stock_hash=$(sha256sum "$STOCK_CFG" | awk '{print $1}')
[ "$stock_hash" = "$KNOWN_STOCK_CFG_SHA256" ] ||
    die "stock config is not the reviewed VP4R revision ($stock_hash)"

readelf -h "$PROVIDER" | grep -Eq 'Class:[[:space:]]+ELF32' ||
    die "provider is not ELF32"
readelf -h "$PROVIDER" | grep -Eq 'Machine:[[:space:]]+ARM' ||
    die "provider is not ARM"
readelf -h "$PROVIDER" | grep -Eq 'Type:[[:space:]]+DYN' ||
    die "provider is not an ELF shared object"

for symbol in panaVersionCheck panaInitialize panaGetSoftwareInfrastructure panaDeinitialize
do
    readelf -Ws "$PROVIDER" | awk -v wanted="$symbol" '$8 == wanted { found = 1 } END { exit !found }' ||
        die "provider does not export $symbol"
done

mkdir -p "$OUTPUT/provider" "$OUTPUT/override" "$OUTPUT/bin"
cp -p "$PROVIDER" "$OUTPUT/provider/libNaviCore_mock.so"

awk -v replacement="NaviDllName=$RADIO_PROVIDER" '
    {
        sub(/\r$/, "")
        if ($0 ~ /^NaviDllName=/) {
            count++
            print replacement
        } else {
            print
        }
    }
    END {
        if (count != 1)
            exit 7
    }
' "$STOCK_CFG" > "$OUTPUT/override/nv_navigation.cfg" ||
    die "stock config did not contain exactly one NaviDllName entry"

case $0 in
    */*) SCRIPT_DIR=${0%/*} ;;
    *) SCRIPT_DIR=. ;;
esac
SCRIPT_DIR=$(CDPATH= cd "$SCRIPT_DIR" && pwd) || exit 1
cp -p "$SCRIPT_DIR/navicore_provider_ctl.sh" "$OUTPUT/navicore_provider_ctl.sh"

if [ -n "$QNX_OPENSSL" ]; then
    [ -x "$QNX_OPENSSL" ] || die "QNX OpenSSL is not executable: $QNX_OPENSSL"
    cp -p "$QNX_OPENSSL" "$OUTPUT/bin/openssl"
fi

(
    cd "$OUTPUT"
    sha256sum provider/libNaviCore_mock.so override/nv_navigation.cfg > deploy.sha256
)

echo "Prepared $OUTPUT"
echo "Copy its contents to /datastore/navicore-mock on the bench radio."
echo "Provider: $(awk '$2 == "provider/libNaviCore_mock.so" {print $1}' "$OUTPUT/deploy.sha256")"

