#!/bin/bash
# deploy_backend.sh LOCAL_SO BACKUP_SUFFIX [--running]
#
# Installs the OsmAnd backend (or another file, see ACTIVE) on the bench
# radio: stage, verify by read-back, back up the active file once, install,
# verify, restore the backup on a mismatch. The backend is loaded by the
# provider from its fixed path, so no image rebuild is needed; it takes
# effect when nv_navigation restarts (or at the next boot).
#
# --running: nv_navigation has the file mapped; replace by unlink + copy
# (the running process keeps the old inode until it restarts). Without it
# the script refuses to touch a mapped file.
#
# Environment:
#   RADIO_HOST (192.168.2.8), RADIO_SSH_KEY ($HOME/priv_openssh)
#   ACTIVE  installed path (/datastore/navicore-backend/libOpenNaviBackendOsmAnd.so)
#   MATCH   name pidin shows while it is mapped (BackendOsmAnd)
#   RADIO_SYNC    flush helper on the unit, run as "$RADIO_SYNC <path>" (the
#                 unit has no sync); empty skips flushing
#   RADIO_FSFREE  free-space helper on the unit printing "avail=<n>MiB" for a
#                 path; empty skips the free-space check
set -u
LOCAL=${1:?usage: deploy_backend.sh LOCAL_SO BACKUP_SUFFIX [--running]}
SUFFIX=${2:?backup suffix}
RUNNING=${3:-}
HOST=root@${RADIO_HOST:-192.168.2.8}
KEY=${RADIO_SSH_KEY:-$HOME/priv_openssh}
OPTS=(-oHostKeyAlgorithms=+ssh-dss -oPubkeyAcceptedAlgorithms=+ssh-rsa
      -oBatchMode=yes -oConnectTimeout=10 -i "$KEY")
ACTIVE=${ACTIVE:-/datastore/navicore-backend/libOpenNaviBackendOsmAnd.so}
MATCH=${MATCH:-BackendOsmAnd}
SYNC=${RADIO_SYNC-/datastore/navicore-mock-final-a1/navicore_sync}
FSFREE=${RADIO_FSFREE-/tmp/fsfree}
DIR=${ACTIVE%/*}
STAGED=$ACTIVE.staged-$SUFFIX
BACKUP=$ACTIVE.pre-$SUFFIX
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

remote() { ssh "${OPTS[@]}" "$HOST" "$1" 2>&1 | grep -v '^welcome'; return "${PIPESTATUS[0]}"; }
flush() { if [ -z "$SYNC" ]; then echo ":"; else echo "$SYNC $1"; fi; }  # remote command
fetch_hash() {  # remote path -> sha256 or empty
    rm -f "$WORK/readback"
    scp -O -q "${OPTS[@]}" "$HOST:$1" "$WORK/readback" 2>/dev/null &&
        sha256sum "$WORK/readback" | cut -d' ' -f1
}
fail() { echo "ABORT: $*"; exit 1; }

[ -f "$LOCAL" ] || fail "local file missing: $LOCAL"
WANT=$(sha256sum "$LOCAL" | cut -d' ' -f1)
echo "local sha256 $WANT"

# 1. Is the file mapped by nv_navigation?
LOADED=0
if remote "p=\$(/proc/boot/pidin -f an | /usr/bin/grep pas/bin/nv_navigation | /bin/awk '{print \$1}'); [ -n \"\$p\" ] && /proc/boot/pidin -p \$p mem | /usr/bin/grep -q $MATCH"; then
    LOADED=1
    [ "$RUNNING" = "--running" ] || fail "nv_navigation has $MATCH mapped; pass --running"
    echo "mapped by nv_navigation: using unlink + copy"
fi

# 2. Room for staging + backup + install (3x), so an unlink-then-copy is
# never stranded by a full partition.
if [ -n "$FSFREE" ]; then
    NEED_MB=$(( ( $(stat -c %s "$LOCAL") * 3 >> 20 ) + 1 ))
    AVAIL_MB=$(remote "$FSFREE $DIR" | sed -n 's/.*avail=\([0-9]*\)MiB.*/\1/p')
    [ -n "$AVAIL_MB" ] || fail "cannot read free space ($FSFREE missing? set RADIO_FSFREE= to skip)"
    [ "$AVAIL_MB" -ge "$NEED_MB" ] || fail "only ${AVAIL_MB} MiB free on $DIR, need ${NEED_MB} MiB"
    echo "free space ok: ${AVAIL_MB} MiB available, need ${NEED_MB} MiB"
else
    echo "warning: free-space check skipped (RADIO_FSFREE empty)"
fi

# 3. Stage and verify the upload.
scp -O -q -p "${OPTS[@]}" "$LOCAL" "$HOST:$STAGED" || fail "upload failed"
[ "$(fetch_hash "$STAGED")" = "$WANT" ] || fail "staged copy hash mismatch (active untouched)"
echo "staged and verified"

# 4. Back up the active file once per suffix.
if remote "[ -e $BACKUP ]"; then
    echo "backup exists: $BACKUP (kept)"
else
    remote "/bin/cp -p $ACTIVE $BACKUP; $(flush "$BACKUP")" || fail "backup failed (active untouched)"
    [ "$(fetch_hash "$BACKUP")" = "$(fetch_hash "$ACTIVE")" ] || fail "backup hash mismatch (active untouched)"
    echo "backup verified: $BACKUP"
fi

# 5. Install, verify, restore on failure.
if [ $LOADED -eq 1 ]; then
    # A mapped file cannot be overwritten; unlink only once staging and
    # backup exist.
    remote "[ -s $STAGED ] && [ -s $BACKUP ] && /bin/rm $ACTIVE && /bin/cp -p $STAGED $ACTIVE; $(flush "$ACTIVE"); $(flush "$DIR")"
else
    remote "/bin/cp -p $STAGED $ACTIVE; $(flush "$ACTIVE"); $(flush "$DIR")"
fi
if [ "$(fetch_hash "$ACTIVE")" != "$WANT" ]; then
    echo "installed hash mismatch; restoring backup"
    remote "/bin/rm -f $ACTIVE; /bin/cp -p $BACKUP $ACTIVE; $(flush "$ACTIVE")"
    fail "install failed; backup restored"
fi
remote "/bin/rm $STAGED"
echo "installed and verified: $ACTIVE ($WANT)"
