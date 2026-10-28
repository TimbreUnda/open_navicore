#!/bin/sh
# Builds the junction lab (host OsmAnd router + junction_lab_tool + junction_probe)
# into build/junction_lab/. First build about 3 minutes.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
cmake -S "$root/tools/junction_lab/host_osmand" -B "$root/build/junction_lab/host_osmand" \
  -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$root/build/junction_lab/host_osmand" -j "$(nproc)" --target junction_lab_tool junction_probe
cp /mnt/d/osmand-qnx-build/source/resources/routing/routing.xml "$root/build/junction_lab/routing.xml"
