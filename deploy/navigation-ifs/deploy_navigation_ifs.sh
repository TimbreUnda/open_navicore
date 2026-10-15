#!/bin/bash
# deploy_navigation_ifs.sh IMAGE IFS_DEPLOY [--dry-run]
#
# Installs navigation.ifs (the whole navigation code) on the bench radio,
# staged, without mounting it (the boot overlay mounts it at boot: activate
# with one vp4r-reboot).
#
#   1. ifs-deploy uploads IMAGE as <dir>/navigation.ifs.new and verifies it
#      by read-back (sha256)
#   2. the current navigation.ifs is pulled to the local backup directory
#   3. on the unit, by rename (no copies, so only one extra image of space):
#      navigation.ifs -> navigation.previous.ifs (replacing the last one),
#      navigation.ifs.new -> navigation.ifs; flush, read back and compare;
#      on a mismatch the previous image is renamed back
#
# A mounted image is never overwritten in place (a truncated in-place copy
# made mountifs crash at boot, unit ledger 2026-10-13); renaming it is safe,
# the mount keeps the old file until the reboot.
#
# Environment: RADIO_HOST (192.168.2.8), RADIO_SSH_KEY ($HOME/priv_openssh),
# NAVIGATION_IFS_DIR (/datastore/navicore), BACKUP_DIR (./ifs-deploy-backups),
# RADIO_SYNC (/usr/bin/vp4r-sync; the unit has no sync), RADIO_RENAME
# (/usr/bin/bench_rename, a rename(2) that never overwrites; the unit has no
# mv), RADIO_SHA256 (/usr/bin/sha256sum, hashes on the unit instead of
# pulling the image back), all from dev-tools.
set -euo pipefail

IMAGE=${1:?usage: deploy_navigation_ifs.sh IMAGE IFS_DEPLOY [--dry-run]}
IFS_DEPLOY=${2:?ifs-deploy path}
DRY=${3:-}
HOST=${RADIO_HOST:-192.168.2.8}
KEY=${RADIO_SSH_KEY:-$HOME/priv_openssh}
DIR=${NAVIGATION_IFS_DIR:-/datastore/navicore}
BACKUP_DIR=${BACKUP_DIR:-./ifs-deploy-backups}
SYNC=${RADIO_SYNC:-/usr/bin/vp4r-sync}
RENAME=${RADIO_RENAME:-/usr/bin/bench_rename}
SHA256=${RADIO_SHA256:-/usr/bin/sha256sum}
ACTIVE=$DIR/navigation.ifs
STAGED=$DIR/navigation.ifs.new
PREVIOUS=$DIR/navigation.previous.ifs
OPTS=(-oBatchMode=yes -oLogLevel=ERROR -oConnectTimeout=10
      -oHostKeyAlgorithms=+ssh-dss -oPubkeyAcceptedAlgorithms=+ssh-rsa -i "$KEY")

[ -f "$IMAGE" ] || { echo "no image: $IMAGE" >&2; exit 2; }
WANT=$(sha256sum "$IMAGE" | cut -d' ' -f1)
STAMP=$(date +%Y%m%d-%H%M%S)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cp -p "$IMAGE" "$WORK/navigation.ifs.new"

echo "image:   $IMAGE ($(($(stat -c %s "$IMAGE") >> 20)) MiB, sha256 $WANT)"
echo "target:  root@$HOST:$ACTIVE (staged as $STAGED)"
if [ "$DRY" = "--dry-run" ]; then
    "$IFS_DEPLOY" "$WORK/navigation.ifs.new" --host "$HOST" --key "$KEY" \
        --remote-dir "$DIR" --backup-dir "$BACKUP_DIR" --dry-run
    echo "[dry-run] pull $ACTIVE to $BACKUP_DIR/navigation.ifs.$STAMP"
    echo "[dry-run] on the unit: rm $PREVIOUS; $RENAME $ACTIVE $PREVIOUS; $RENAME $STAGED $ACTIVE; flush; read back"
    exit 0
fi

remote_ok() { ssh "${OPTS[@]}" "root@$HOST" "$1" >/dev/null 2>&1; }
flush() { remote_ok "$SYNC" || echo "warning: flush failed"; }
readback() {  # sha256 of a file on the unit, hashed there
    ssh "${OPTS[@]}" "root@$HOST" "$SHA256 $1" 2>/dev/null |
        sed -n 's/^\([0-9a-f]\{64\}\) .*/\1/p'
}
remote_ok "[ -x $RENAME ] && [ -x $SYNC ] && [ -x $SHA256 ]" ||
    { echo "the unit lacks $RENAME, $SYNC or $SHA256 (mount dev-tools)"; exit 1; }

# 1. Upload the staged copy (ifs-deploy verifies it).
"$IFS_DEPLOY" "$WORK/navigation.ifs.new" --host "$HOST" --key "$KEY" \
    --remote-dir "$DIR" --backup-dir "$BACKUP_DIR"
flush

# 2. Keep the current image locally.
HAD_ACTIVE=0
if remote_ok "[ -f $ACTIVE ]"; then
    HAD_ACTIVE=1
    mkdir -p "$BACKUP_DIR"
    scp -O -p -q "${OPTS[@]}" "root@$HOST:$ACTIVE" "$BACKUP_DIR/navigation.ifs.$STAMP"
    echo "backup:  $BACKUP_DIR/navigation.ifs.$STAMP"
fi

# 3. Swap by rename and verify; rename the previous image back on failure.
if [ $HAD_ACTIVE -eq 1 ]; then
    remote_ok "/bin/rm -f $PREVIOUS; $RENAME $ACTIVE $PREVIOUS" ||
        { echo "could not set the current image aside; nothing changed"; exit 1; }
fi
remote_ok "$RENAME $STAGED $ACTIVE" || true
flush
if [ "$(readback "$ACTIVE")" != "$WANT" ]; then
    echo "installed image does not read back"
    if [ $HAD_ACTIVE -eq 1 ]; then
        remote_ok "/bin/rm -f $ACTIVE; $RENAME $PREVIOUS $ACTIVE" && flush
        echo "previous image restored"
    fi
    exit 1
fi
echo "installed: $ACTIVE ($WANT); previous image: $PREVIOUS"
echo "activate:  one reboot (vp4r-reboot); then check open_navicore_mount_rc=0 in /datastore/boot_overlay.log"
