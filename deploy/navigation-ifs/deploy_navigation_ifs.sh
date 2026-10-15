#!/bin/bash
# deploy_navigation_ifs.sh IMAGE IFS_DEPLOY [--dry-run]
#
# Installs navigation.ifs on the bench radio, staged, without mounting it
# (the boot overlay mounts it at boot: activate with one vp4r-reboot).
#
#   1. ifs-deploy uploads IMAGE as <dir>/navigation.ifs.new and verifies it
#      by read-back (sha256)
#   2. the current navigation.ifs is pulled to the local backup directory
#      and copied on the unit to navigation.pre-<stamp>.ifs (kept)
#   3. rm navigation.ifs && cp -p navigation.ifs.new navigation.ifs, flush,
#      read back and compare; on a mismatch the previous image is restored
#
# A mounted image is never overwritten in place (a truncated in-place copy
# made mountifs crash at boot, unit ledger 2026-10-13).
#
# Environment: RADIO_HOST (192.168.2.8), RADIO_SSH_KEY ($HOME/priv_openssh),
# NAVIGATION_IFS_DIR (/datastore/navicore), BACKUP_DIR (./ifs-deploy-backups),
# RADIO_SYNC: a flush helper on the unit (it has no sync), run as
# "$RADIO_SYNC <path>"; empty skips flushing (not recommended).
set -euo pipefail

IMAGE=${1:?usage: deploy_navigation_ifs.sh IMAGE IFS_DEPLOY [--dry-run]}
IFS_DEPLOY=${2:?ifs-deploy path}
DRY=${3:-}
HOST=${RADIO_HOST:-192.168.2.8}
KEY=${RADIO_SSH_KEY:-$HOME/priv_openssh}
DIR=${NAVIGATION_IFS_DIR:-/datastore/navicore}
BACKUP_DIR=${BACKUP_DIR:-./ifs-deploy-backups}
SYNC=${RADIO_SYNC-/datastore/navicore-mock-final-a1/navicore_sync}
ACTIVE=$DIR/navigation.ifs
STAGED=$DIR/navigation.ifs.new
OPTS=(-oBatchMode=yes -oLogLevel=ERROR -oConnectTimeout=10
      -oHostKeyAlgorithms=+ssh-dss -oPubkeyAcceptedAlgorithms=+ssh-rsa -i "$KEY")

[ -f "$IMAGE" ] || { echo "no image: $IMAGE" >&2; exit 2; }
WANT=$(sha256sum "$IMAGE" | cut -d' ' -f1)
STAMP=$(date +%Y%m%d-%H%M%S)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cp -p "$IMAGE" "$WORK/navigation.ifs.new"

echo "image:   $IMAGE (sha256 $WANT)"
echo "target:  root@$HOST:$ACTIVE (staged as $STAGED)"
if [ "$DRY" = "--dry-run" ]; then
    "$IFS_DEPLOY" "$WORK/navigation.ifs.new" --host "$HOST" --key "$KEY" \
        --remote-dir "$DIR" --backup-dir "$BACKUP_DIR" --dry-run
    echo "[dry-run] pull $ACTIVE to $BACKUP_DIR/navigation.ifs.$STAMP"
    echo "[dry-run] on the unit: cp -p $ACTIVE $DIR/navigation.pre-$STAMP.ifs"
    echo "[dry-run] on the unit: rm $ACTIVE && cp -p $STAGED $ACTIVE; flush; read back"
    exit 0
fi

remote() { ssh "${OPTS[@]}" "root@$HOST" "$1" 2>&1 | grep -v '^welcome' || true; }
remote_ok() { ssh "${OPTS[@]}" "root@$HOST" "$1" >/dev/null 2>&1; }
flush() { [ -z "$SYNC" ] || remote_ok "$SYNC $1" || echo "warning: flush $1 failed"; }
readback() {
    rm -f "$WORK/readback"
    scp -O -q "${OPTS[@]}" "root@$HOST:$1" "$WORK/readback" 2>/dev/null &&
        sha256sum "$WORK/readback" | cut -d' ' -f1
}

# 1. Upload the staged copy (ifs-deploy verifies it).
"$IFS_DEPLOY" "$WORK/navigation.ifs.new" --host "$HOST" --key "$KEY" \
    --remote-dir "$DIR" --backup-dir "$BACKUP_DIR"
flush "$STAGED"

# 2. Keep the current image, locally and on the unit.
PREVIOUS=
if remote_ok "[ -f $ACTIVE ]"; then
    mkdir -p "$BACKUP_DIR"
    scp -O -p -q "${OPTS[@]}" "root@$HOST:$ACTIVE" "$BACKUP_DIR/navigation.ifs.$STAMP"
    PREVIOUS=$DIR/navigation.pre-$STAMP.ifs
    remote_ok "/bin/cp -p $ACTIVE $PREVIOUS" || { echo "backup on the unit failed; active untouched"; exit 1; }
    flush "$PREVIOUS"
    [ "$(readback "$PREVIOUS")" = "$(sha256sum "$BACKUP_DIR/navigation.ifs.$STAMP" | cut -d' ' -f1)" ] ||
        { echo "backup on the unit does not read back; active untouched"; exit 1; }
    echo "backup:  $BACKUP_DIR/navigation.ifs.$STAMP and $PREVIOUS"
fi

# 3. Swap by unlink + copy and verify; restore the previous image on failure.
remote_ok "[ -s $STAGED ] && /bin/rm -f $ACTIVE && /bin/cp -p $STAGED $ACTIVE" || true
flush "$ACTIVE"
flush "$DIR"
if [ "$(readback "$ACTIVE")" != "$WANT" ]; then
    echo "installed image does not read back"
    if [ -n "$PREVIOUS" ]; then
        remote_ok "/bin/rm -f $ACTIVE; /bin/cp -p $PREVIOUS $ACTIVE" && flush "$ACTIVE"
        echo "previous image restored from $PREVIOUS"
    fi
    exit 1
fi
remote_ok "/bin/rm -f $STAGED" && flush "$DIR"
echo "installed: $ACTIVE ($WANT)"
echo "activate:  one reboot (vp4r-reboot); then check open_navicore_mount_rc=0 in /datastore/boot_overlay.log"
