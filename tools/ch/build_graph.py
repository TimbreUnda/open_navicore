"""Build a directed car road graph from OBF routing sections.

    python3 build_graph.py <routing.xml> <out.graph> <map.obf>...

Nodes are junctions: points shared by two or more roads (OsmAnd connects
roads at identical coordinates) and road ends.  An edge runs between two
consecutive junctions along a road; its cost is length / (speed * priority)
from the routing.xml car profile (see routing_rules.py for what is and is not
modelled).  Roads open to cars only.

File layout, little-endian (read by ch_tool.cpp):
  char magic[4] = "CHG1"; uint32 nodes, edges
  int32  node_x[nodes], node_y[nodes]        31-bit tile coordinates
  uint32 from[edges], to[edges]
  uint32 cost_ms[edges]                      routing cost
  uint32 time_ms[edges]                      travel time at the profile speed
  float  length_m[edges]
  int64  road_id[edges]                      OBF route id
  uint32 point_from[edges], point_to[edges]  point range in the road (from < to)
  uint8  reversed[edges]                     1: travelled from point_to to point_from
"""
import math
import struct
import sys
import time

import numpy as np

import obf_routing
import routing_rules

EARTH_RADIUS = 6378137.0
UNIT_31 = 2.0 ** 31


def main(rules_path, out_path, obf_paths):
    started = time.time()
    profile = routing_rules.Profile(rules_path)
    roads = []          # (road, access/oneway/speed/priority)
    seen = set()
    cache = {}
    for file_index, path in enumerate(obf_paths):
        index = obf_routing.RoutingIndex(path)
        for road_id, road in index.roads.items():
            if road_id in seen or len(road.xs) < 2:
                continue
            seen.add(road_id)
            key = (file_index, road.types)
            attrs = cache.get(key)
            if attrs is None:
                tags = {}
                for rule in road.types:
                    pair = index.rules.get(rule)
                    if pair:
                        tags[pair[0]] = pair[1]
                attrs = cache[key] = profile.road(tags)
            if attrs[0]:
                roads.append((road, attrs))
        print('%s: %d roads so far (%.0fs)' % (path.rsplit('/', 1)[-1], len(roads),
                                               time.time() - started), flush=True)
    if not roads:
        raise SystemExit('no routable roads')

    counts = np.array([len(r.xs) for r, _ in roads], dtype=np.int64)
    starts = np.concatenate(([0], np.cumsum(counts)[:-1]))
    total = int(counts.sum())
    xs = np.concatenate([np.frombuffer(r.xs, dtype=np.uint32) for r, _ in roads]).astype(np.int64)
    ys = np.concatenate([np.frombuffer(r.ys, dtype=np.uint32) for r, _ in roads]).astype(np.int64)
    road_of = np.repeat(np.arange(len(roads)), counts)
    oneway = np.array([a[1] for _, a in roads], dtype=np.int8)
    speed_ms = np.array([a[2] for _, a in roads], dtype=np.float64) / 3.6
    priority = np.array([a[3] for _, a in roads], dtype=np.float64)
    road_ids = np.array([r.id for r, _ in roads], dtype=np.int64)
    print('points %d, roads %d (%.0fs)' % (total, len(roads), time.time() - started), flush=True)

    keys = (xs << 31) | ys
    _, inverse, point_counts = np.unique(keys, return_inverse=True, return_counts=True)
    junction = point_counts[inverse] >= 2
    junction[starts] = True
    junction[starts + counts - 1] = True
    cut = np.flatnonzero(junction)
    same = road_of[cut[:-1]] == road_of[cut[1:]]
    first, last = cut[:-1][same], cut[1:][same]

    # Length of the polyline between consecutive points, in metres.
    lat = np.arctan(np.sinh(math.pi * (1.0 - 2.0 * ys / UNIT_31)))
    metres_per_unit = 2.0 * math.pi * EARTH_RADIUS * np.cos(lat) / UNIT_31
    step = np.zeros(total)
    step[:-1] = np.hypot(np.diff(xs), np.diff(ys)) * metres_per_unit[:-1]
    step[starts + counts - 1] = 0.0
    cumulative = np.concatenate(([0.0], np.cumsum(step)))
    length = cumulative[last] - cumulative[first]

    u, v = inverse[first], inverse[last]
    keep = u != v
    first, last, u, v, length = first[keep], last[keep], u[keep], v[keep], length[keep]
    road = road_of[first]
    forward = (oneway[road] != -1)
    backward = (oneway[road] != 1)

    def directed(mask, src, dst, rev):
        return (src[mask], dst[mask], length[mask], road[mask], first[mask], last[mask],
                np.full(int(mask.sum()), rev, dtype=np.uint8))

    parts = [directed(forward, u, v, 0), directed(backward, v, u, 1)]
    e_from, e_to, e_len, e_road, e_pfrom, e_pto, e_rev = (
        np.concatenate([p[i] for p in parts]) for i in range(7))
    e_pfrom = e_pfrom - starts[e_road]
    e_pto = e_pto - starts[e_road]
    e_speed = speed_ms[e_road]
    cost_ms = np.maximum(1, np.rint(e_len / (e_speed * priority[e_road]) * 1000)).astype(np.uint32)
    time_ms = np.maximum(1, np.rint(e_len / e_speed * 1000)).astype(np.uint32)

    used, remap = np.unique(np.concatenate((e_from, e_to)), return_inverse=True)
    m = len(e_from)
    e_from, e_to = remap[:m].astype(np.uint32), remap[m:].astype(np.uint32)
    # node coordinates: any point with that key
    node_first = np.full(len(point_counts), -1, dtype=np.int64)
    node_first[inverse[::-1]] = np.arange(total - 1, -1, -1)
    node_point = node_first[used]
    node_x = xs[node_point].astype(np.int32)
    node_y = ys[node_point].astype(np.int32)

    with open(out_path, 'wb') as f:
        f.write(struct.pack('<4sII', b'CHG1', len(used), m))
        for array in (node_x, node_y, e_from, e_to, cost_ms, time_ms,
                      e_len.astype(np.float32), road_ids[e_road],
                      e_pfrom.astype(np.uint32), e_pto.astype(np.uint32), e_rev):
            f.write(np.ascontiguousarray(array).tobytes())
    print('graph: %d nodes, %d directed edges, %.0f km of road, %.1f MB, %.0fs' % (
        len(used), m, e_len.sum() / 2000.0, (28 + len(used) * 8 + m * 41) / 1e6,
        time.time() - started))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
