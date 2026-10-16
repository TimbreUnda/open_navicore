#!/bin/bash
# Zoom sweep on the bench radio: one "-" tap every STEP_S seconds (N times),
# then one "+" tap every STEP_S seconds, recording the frame-log time (ms) at
# every tap. Start it with the navigation screen on a place card or free drive
# at a high zoom (jump to a recent destination first); it sends touches to the
# head unit, so run it only on the bench radio and announce it.
#
#   zoom_sweep.sh OUT_DIR            writes OUT_DIR/marks.txt, frames.log, zoom.txt
#
# Environment: RADIO_HOST (192.168.2.8), RADIO_SSH_KEY ($HOME/priv_openssh),
# STEPS (9), STEP_S (5), ZOOM_OUT_XY ("976 190"), ZOOM_IN_XY ("976 120").
# Analyse with: scripts/perf/frame_stats.py OUT_DIR/frames.log OUT_DIR/marks.txt
set -euo pipefail

OUT=${1:?usage: zoom_sweep.sh OUT_DIR}
HOST=${RADIO_HOST:-192.168.2.8}
KEY=${RADIO_SSH_KEY:-$HOME/priv_openssh}
STEPS=${STEPS:-9}
STEP_S=${STEP_S:-5}
OUT_XY=${ZOOM_OUT_XY:-"976 190"}
IN_XY=${ZOOM_IN_XY:-"976 120"}
mkdir -p "$OUT"

SSH=(ssh -oHostKeyAlgorithms=+ssh-dss -oPubkeyAcceptedAlgorithms=+ssh-rsa -i "$KEY"
     -oConnectTimeout=8 "root@$HOST")

last_t() { "${SSH[@]}" '/bin/cat /tmp/open-navicore-frames.log' 2>/dev/null | tail -1 | awk '{print $2}'; }
tap() { "${SSH[@]}" "cd /tmp; /usr/bin/vp4r-screen-tap $1" >/dev/null 2>&1; }
mark() { echo "$1 $(last_t)" >> "$OUT/marks.txt"; }

: > "$OUT/marks.txt"
mark at_start
for n in $(seq 1 "$STEPS"); do tap "$OUT_XY"; sleep "$STEP_S"; mark "out_$n"; done
sleep 3
mark bottom
for n in $(seq 1 "$STEPS"); do tap "$IN_XY"; sleep "$STEP_S"; mark "in_$n"; done
sleep 3
mark end

"${SSH[@]}" '/bin/cat /tmp/open-navicore-frames.log' > "$OUT/frames.log" 2>/dev/null
"${SSH[@]}" '/bin/cat /tmp/open-navicore-osmand.log' 2>/dev/null |
    grep -a "^vector_map" | grep -a -o " zoom=[0-9.]* tile_zoom=[0-9.]*" > "$OUT/zoom.txt" || true
echo "done: $OUT"
