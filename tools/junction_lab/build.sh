#!/bin/sh
# Builds the junction lab (host OsmAnd router + junction_lab_tool + junction_probe)
# into build/junction_lab/. First build about 3 minutes.
# OSMAND_QNX_ROOT: the OsmAnd QNX build tree (its source/ is compiled for the host);
# default the directory that holds this repository, as for the radio build (set it
# from a linked worktree).
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
OSMAND_QNX_ROOT=${OSMAND_QNX_ROOT:-$(cd "$root/.." && pwd)/osmand-qnx-build}
export OSMAND_QNX_ROOT
cmake -S "$root/tools/junction_lab/host_osmand" -B "$root/build/junction_lab/host_osmand" \
  -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$root/build/junction_lab/host_osmand" -j "$(nproc)" --target junction_lab_tool junction_probe
cp "$OSMAND_QNX_ROOT/source/resources/routing/routing.xml" "$root/build/junction_lab/routing.xml"
