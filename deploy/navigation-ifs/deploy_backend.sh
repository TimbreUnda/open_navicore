#!/bin/bash
# deploy_backend.sh LIBRARY [--dry-run]
#
# Installs the OsmAnd backend (build/qnx/navicore-data/navicore/libOpenNaviBackendOsmAnd.so)
# in the booted slot's data dir on the bench radio, where the slot image's
# 50-navicore.pre links it into /open-navicore at boot. The slot's 50-navicore.ifs
# (provider, its config) and the OsmAnd core stay as they are.
#
#   1. scp LIBRARY to <data>/libOpenNaviBackendOsmAnd.so.new and check its sha256 on
#      the unit
#   2. bench_rename the current one to libOpenNaviBackendOsmAnd.before-<stamp>.so (kept:
#      the rollback), then .new to libOpenNaviBackendOsmAnd.so; vp4r-sync
#   3. read back the active file's sha256
# Activate with one vp4r-reboot afterwards (not done here).
#
# Environment: RADIO_HOST (192.168.2.8), RADIO_SSH_KEY ($HOME/priv_openssh), VP4R_HOME
# (/datastore/vp4r); dev-tools on the unit (/usr/bin/sha256sum, bench_rename, vp4r-sync).
set -euo pipefail

LIBRARY=${1:?usage: deploy_backend.sh LIBRARY [--dry-run]}
DRY=${2:-}
HOST=${RADIO_HOST:-192.168.2.8}
KEY=${RADIO_SSH_KEY:-$HOME/priv_openssh}
HOME_DIR=${VP4R_HOME:-/datastore/vp4r}
NAME=libOpenNaviBackendOsmAnd.so
OPTS=(-oBatchMode=yes -oLogLevel=ERROR -oConnectTimeout=10 -oIdentitiesOnly=yes
      -oHostKeyAlgorithms=+ssh-dss -oPubkeyAcceptedAlgorithms=+ssh-rsa -i "$KEY")
[ -f "$LIBRARY" ] || { echo "no library: $LIBRARY" >&2; exit 2; }
WANT=$(sha256sum "$LIBRARY" | cut -d' ' -f1)
STAMP=$(date +%Y%m%d-%H%M%S)
radio() { ssh "${OPTS[@]}" "root@$HOST" "$@" 2>&1 | grep -v '^welcome to uconnect' || true; }

SLOT=$(radio "read S < $HOME_DIR/slot; echo \$S" | tr -d '\r')
[ -n "$SLOT" ] || { echo "cannot read the booted slot ($HOME_DIR/slot)" >&2; exit 1; }
D=$HOME_DIR/slots/$SLOT/data/navicore
BACKUP=$D/libOpenNaviBackendOsmAnd.before-$STAMP.so
echo "slot $SLOT, data dir $D"
echo "new:     $NAME sha256 $WANT"
radio "/usr/bin/sha256sum $D/$NAME" | sed 's/^/current: /'
if [ "$DRY" = "--dry-run" ]; then
    echo "[dry-run] scp $LIBRARY -> $D/$NAME.new; check sha256"
    echo "[dry-run] bench_rename $D/$NAME $BACKUP"
    echo "[dry-run] bench_rename $D/$NAME.new $D/$NAME; vp4r-sync; read back"
    exit 0
fi

scp -O -p "${OPTS[@]}" "$LIBRARY" "root@$HOST:$D/$NAME.new"
GOT=$(radio "/usr/bin/sha256sum $D/$NAME.new" | cut -d' ' -f1)
[ "$GOT" = "$WANT" ] || { echo "uploaded copy does not match ($GOT); $D/$NAME untouched" >&2; exit 1; }
radio "/usr/bin/bench_rename $D/$NAME $BACKUP && /usr/bin/bench_rename $D/$NAME.new $D/$NAME && /usr/bin/vp4r-sync"
ACTIVE=$(radio "/usr/bin/sha256sum $D/$NAME" | cut -d' ' -f1)
if [ "$ACTIVE" = "$WANT" ]; then
    echo "installed; previous kept as $BACKUP"
    echo "activate: one /usr/bin/vp4r-reboot"
else
    echo "active file sha256 $ACTIVE does not match: check $D by hand" >&2
    exit 1
fi
