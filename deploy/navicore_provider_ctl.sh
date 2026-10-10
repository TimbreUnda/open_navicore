#!/bin/sh
# Ephemeral, hash-gated NaviCore provider config redirect for a VP4R bench.
# This never modifies /navi/libNaviCore.so or the IFS-backed factory config.

set -u

BUNDLE_ROOT=/datastore/navicore-mock
PROVIDER=$BUNDLE_ROOT/provider/libNaviCore_mock.so
OVERRIDE_CFG=$BUNDLE_ROOT/override/nv_navigation.cfg
FACTORY_COPY=$BUNDLE_ROOT/stock/nv_navigation.cfg
MANIFEST=$BUNDLE_ROOT/deploy.sha256
ARM_MARKER=$BUNDLE_ROOT/ALLOW_BENCH_PROVIDER_TEST
ACTIVE_CFG=/pas/scfg/nv_navigation.cfg
KNOWN_STOCK_CFG_SHA256=c889c2b4c5d1d5ddb4b32c0c79a46c8482950ee0db783a83b1ed949aef7d680e
KNOWN_OVERRIDE_CFG_SHA256=c68e7c82ae0b64ff067d95d49fba56fe25bf8d3662b2cf7a7a2df53ad811a58e
EXPECTED_ARM_TEXT=I_ACCEPT_NAVIGATION_WATCHDOG_RESET_RISK
AWK=/bin/awk
CP=/bin/cp
MKDIR=/bin/mkdir
LN=/proc/boot/ln
MOUNT=/proc/boot/mount

die()
{
    echo "ERROR: $*" >&2
    exit 1
}

choose_openssl()
{
    if [ -n "${OPENSSL:-}" ]; then
        :
    elif [ -x "$BUNDLE_ROOT/bin/openssl" ]; then
        OPENSSL=$BUNDLE_ROOT/bin/openssl
    elif [ -x /datastore/nng-deploy/bin/openssl ]; then
        OPENSSL=/datastore/nng-deploy/bin/openssl
    else
        die "no ARM/QNX OpenSSL; stage it at $BUNDLE_ROOT/bin/openssl"
    fi
    [ -x "$OPENSSL" ] || die "checksum tool is not executable: $OPENSSL"
}

file_sha256()
{
    "$OPENSSL" dgst -sha256 "$1" 2>/dev/null | "$AWK" '{print $NF}'
}

require_known_normal_firmware()
{
    [ -x "$AWK" ] || die "missing stock awk utility: $AWK"
    [ -x "$CP" ] || die "missing stock copy utility: $CP"
    [ -x "$MKDIR" ] || die "missing stock mkdir utility: $MKDIR"
    [ -x "$LN" ] || die "missing stock process-manager link utility: $LN"
    [ -x "$MOUNT" ] || die "missing stock mount utility"
    mounts=$($MOUNT 2>/dev/null) || die "cannot inspect mounts"
    case "$mounts" in
        *"/dev/sd0t12 on / type qnx6"*)
            die "refusing to operate from the SD recovery root"
            ;;
    esac
    case "$mounts" in
        *"/dev/sd1t177.1 on / type qnx6"*) ;;
        *) die "not the reviewed normal-firmware root (/dev/sd1t177.1)" ;;
    esac
    [ -x /pas/bin/nv_navigation ] || die "stock nv_navigation executable is missing"
    [ -r "$ACTIVE_CFG" ] || die "active navigation config is unreadable"
}

require_arm_marker()
{
    [ -f "$ARM_MARKER" ] ||
        die "bench consent marker is absent: $ARM_MARKER"
    marker=$($AWK 'NR == 1 { first = $0 } NR > 1 { extra = 1 } END { if (extra) exit 2; print first }' "$ARM_MARKER") ||
        die "bench consent marker must contain exactly one line"
    expected_provider=$(manifest_hash provider/libNaviCore_mock.so)
    [ "$marker" = "$EXPECTED_ARM_TEXT $expected_provider" ] ||
        die "bench consent marker must include the reviewed provider SHA-256"
}

manifest_hash()
{
    wanted=$1
    [ -f "$MANIFEST" ] || die "manifest missing: $MANIFEST"
    value=$($AWK -v wanted="$wanted" '
        $2 == wanted && NF == 2 { count++; value = $1 }
        END { if (count != 1) exit 3; print value }
    ' "$MANIFEST") || die "manifest must name $wanted exactly once"
    case "$value" in
        ""|*[!0123456789abcdef]*) die "invalid SHA-256 for $wanted" ;;
    esac
    [ "${#value}" -eq 64 ] || die "invalid SHA-256 length for $wanted"
    echo "$value"
}

verify_bundle()
{
    [ -f "$PROVIDER" ] || die "provider missing: $PROVIDER"
    [ -f "$OVERRIDE_CFG" ] || die "override config missing: $OVERRIDE_CFG"
    expected_provider=$(manifest_hash provider/libNaviCore_mock.so)
    expected_config=$(manifest_hash override/nv_navigation.cfg)
    [ "$expected_config" = "$KNOWN_OVERRIDE_CFG_SHA256" ] ||
        die "manifest names an unreviewed override config ($expected_config)"
    actual_provider=$(file_sha256 "$PROVIDER") || die "cannot hash provider"
    actual_config=$(file_sha256 "$OVERRIDE_CFG") || die "cannot hash override config"
    [ "$actual_provider" = "$expected_provider" ] || die "provider hash mismatch"
    [ "$actual_config" = "$expected_config" ] || die "override config hash mismatch"
    configured=$($AWK -F= '/^NaviDllName=/ { count++; value = substr($0, index($0, "=") + 1) } END { if (count != 1) exit 4; print value }' "$OVERRIDE_CFG") ||
        die "override config must have exactly one NaviDllName"
    [ "$configured" = "$PROVIDER" ] ||
        die "override config points at unexpected provider: $configured"
}

verify_or_create_factory_copy()
{
    if [ -e "$FACTORY_COPY" ]; then
        [ -f "$FACTORY_COPY" ] || die "factory-copy path is not a regular file"
    else
        "$MKDIR" -p "$BUNDLE_ROOT/stock" || die "cannot create stock-copy directory"
        active_hash=$(file_sha256 "$ACTIVE_CFG") || die "cannot hash active config"
        [ "$active_hash" = "$KNOWN_STOCK_CFG_SHA256" ] ||
            die "refusing to copy an unknown active config ($active_hash)"
        "$CP" -p "$ACTIVE_CFG" "$FACTORY_COPY" || die "cannot preserve factory config"
    fi
    saved_hash=$(file_sha256 "$FACTORY_COPY") || die "cannot hash factory config copy"
    [ "$saved_hash" = "$KNOWN_STOCK_CFG_SHA256" ] ||
        die "factory config copy has unexpected hash ($saved_hash)"
}

show_process()
{
    if [ -x /proc/boot/pidin ]; then
        /proc/boot/pidin ar 2>/dev/null | "$AWK" '/nv_navigation/ {print "process=" $0; found=1} END {if (!found) print "process=not-seen"}'
    else
        echo "process=unknown (pidin unavailable)"
    fi
}

status()
{
    require_known_normal_firmware
    choose_openssl
    active_hash=$(file_sha256 "$ACTIVE_CFG") || die "cannot hash active config"
    echo "root=known-normal-firmware"
    echo "active_config_sha256=$active_hash"
    if [ "$active_hash" = "$KNOWN_STOCK_CFG_SHA256" ]; then
        echo "config_state=stock"
    elif [ -f "$MANIFEST" ]; then
        expected_config=$(manifest_hash override/nv_navigation.cfg)
        if [ "$active_hash" = "$expected_config" ]; then
            echo "config_state=mock-provider-redirect"
        else
            echo "config_state=unknown"
            show_process
            exit 2
        fi
    else
        echo "config_state=unknown"
        show_process
        exit 2
    fi
    if [ -f "$PROVIDER" ] && [ -f "$OVERRIDE_CFG" ] && [ -f "$MANIFEST" ]; then
        verify_bundle
        echo "bundle_state=verified"
        echo "provider_sha256=$(file_sha256 "$PROVIDER")"
    else
        echo "bundle_state=incomplete"
    fi
    show_process
}

install_redirect()
{
    require_known_normal_firmware
    require_arm_marker
    choose_openssl
    verify_bundle
    current=$(file_sha256 "$ACTIVE_CFG") || die "cannot hash active config"
    [ "$current" = "$KNOWN_STOCK_CFG_SHA256" ] ||
        die "install requires the known stock config to be active ($current)"
    verify_or_create_factory_copy
    "$LN" -sPfv "$OVERRIDE_CFG" "$ACTIVE_CFG" || die "process-manager redirect failed"
    active=$(file_sha256 "$ACTIVE_CFG") || die "cannot verify redirected config"
    expected=$(manifest_hash override/nv_navigation.cfg)
    [ "$active" = "$expected" ] || die "redirect did not resolve to the reviewed config"
    echo "installed ephemeral config redirect; factory files were not changed"
    echo "the running nv_navigation process has NOT been restarted by this script"
    echo "reboot clears the redirect and returns pathname resolution to stock"
}

rollback_redirect()
{
    require_known_normal_firmware
    choose_openssl
    verify_or_create_factory_copy
    "$LN" -sPfv "$FACTORY_COPY" "$ACTIVE_CFG" || die "stock-config redirect failed"
    active=$(file_sha256 "$ACTIVE_CFG") || die "cannot verify rollback redirect"
    [ "$active" = "$KNOWN_STOCK_CFG_SHA256" ] || die "rollback config verification failed"
    echo "active pathname now resolves to the verified stock config copy"
    echo "the running nv_navigation process has NOT been restarted by this script"
    echo "reboot removes all process-manager redirects and uses the IFS factory config"
}

case ${1:-} in
    status) status ;;
    install) install_redirect ;;
    rollback) rollback_redirect ;;
    *) echo "usage: $0 {status|install|rollback}" >&2; exit 64 ;;
esac
