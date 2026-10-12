#!/bin/sh
# Bounded sweep of screen_bench configurations on the radio. Each case runs
# for SECONDS (default 8) and appends to OUT. Read-only for the rest of the
# system: only test windows are created and destroyed.
#   sh run_sweep.sh [seconds] [out]
BENCH=${BENCH:-/tmp/screen_bench}
# EGL needs the graphics environment nv_navigation runs with.
export GRAPHICS_ROOT=${GRAPHICS_ROOT:-/proc/boot/usr/lib/graphics/jacinto6}
export LD_LIBRARY_PATH=/proc/boot/usr/lib/graphics/jacinto6:/usr/lib/graphics/jacinto6:/proc/boot:/usr/lib:/lib/dll:/lib:${LD_LIBRARY_PATH:-}
SECONDS_PER=${1:-8}
OUT=${2:-/tmp/screen_bench_sweep.txt}
: > "$OUT"
run() {
    echo "### $*" >> "$OUT"
    $BENCH --seconds "$SECONDS_PER" "$@" >> "$OUT" 2>&1
    echo >> "$OUT"
}
# 1. Is the cost composition, GPU, or vsync? (NAV-like window under the HMI)
run --mode gl
run --mode gl --finish
run --mode gl --interval 1
# 2. Composition without GL at all, full vs tiny damage.
run --mode sw --format rgba8888 --dirty full --wait-idle
run --mode sw --format rgba8888 --dirty small --wait-idle
run --mode sw --format rgba8888 --dirty full
# 3. Area scaling: half and quarter area.
run --mode gl --size 512x574
run --mode gl --size 512x287
# 4. Window properties Screen might honour.
run --mode gl --transparency none
run --mode gl --transparency none --format rgbx8888
run --mode gl --format rgb565
run --mode gl --buffers 2
# 5. Layer placement: above the HMI vs below, and hardware pipelines.
run --mode gl --zorder 2000
run --mode gl --overlay
run --mode gl --overlay --pipeline 1
run --mode gl --overlay --pipeline 2
run --mode gl --overlay --pipeline 3
echo "sweep written to $OUT"
