"""Build a directed car road graph from OBF routing sections.

    python3 build_graph.py <routing.xml> <out.graph> <map.obf>...

Nodes are junctions: points shared by two or more roads (OsmAnd connects
roads at identical coordinates) and road ends.  An edge runs between two
consecutive junctions along a road; its cost is length / (speed * priority)
from the routing.xml car profile (see routing_rules.py).  Roads open to cars
only.

Junctions where two or more roads meet are expanded: one node per arriving
edge and one per leaving edge, joined by transition edges (road id -1).  A
transition between two different roads costs what GeneralRouter::
calculateTurnTime charges (class change |penalty_transition| / 2, roundabout
entry or exit, and the angle penalties 7.5 / 15 / 30 s above 30 / 60 / 120
degrees, directions measured over the first 5 m as RouteDataObject::
directionRoute does); staying on a road is free.  A transition the OBF turn
restrictions forbid (core semantics, binaryRoutePlanner.cpp: the candidates
are the other car roads at the junction; a NO_* restriction on the arriving
road forbids its target; an ONLY_* restriction whose target is a candidate
forbids all other candidates) is simply absent.  Restrictions with a via road
are counted, not modelled.  Not modelled: traffic-signal and barrier times
(they only enter the reported time, not the routing cost).

File layout, little-endian (read by ch_tool.cpp):
  char magic[4] = "CHG1"; uint32 nodes, edges
  int32  node_x[nodes], node_y[nodes]        31-bit tile coordinates
  uint32 from[edges], to[edges]
  uint32 cost_ms[edges]                      routing cost
  uint32 time_ms[edges]                      travel time at the profile speed
  float  length_m[edges]
  int64  road_id[edges]                      OBF route id; -1 for a transition edge
  uint32 point_from[edges], point_to[edges]  point range in the road (from < to)
  uint8  reversed[edges]                     1: travelled from point_to to point_from
"""
import math
import struct
import sys
import time
from collections import defaultdict

import numpy as np

import obf_routing
import routing_rules

EARTH_RADIUS = 6378137.0
UNIT_31 = 2.0 ** 31
DIRECTION_LOOKAHEAD_M = 5.0   # RouteDataObject::directionRoute(start, plus)

NO_RIGHT, NO_LEFT, NO_U, NO_STRAIGHT, ONLY_RIGHT, ONLY_LEFT, ONLY_STRAIGHT = 1, 2, 3, 4, 5, 6, 7
NO_TYPES = (NO_RIGHT, NO_LEFT, NO_U, NO_STRAIGHT)
ONLY_TYPES = (ONLY_RIGHT, ONLY_LEFT, ONLY_STRAIGHT)


def allowed_exit(before, after, candidates, direct):
    """May the route go from road `before` to road `after` at a junction whose
    other roads are `candidates`?  `direct` is before's (to, type) restrictions."""
    only = [to for to, kind in direct if kind in ONLY_TYPES and to in candidates]
    for to, kind in direct:
        if to == after and kind in NO_TYPES:
            return False
    return not only or after in only


def directions(g, plus, lo, hi, xs, ys, metres_per_unit):
    """RouteDataObject::directionRoute(start, plus, 5) for global point indexes g:
    walk along the road from the start point, summing the distance from the start
    to each visited point, until the sum reaches 5 m or the road ends; the
    direction is -atan2(dx, dy) of the vector from the last point to the start."""
    x = xs[g].astype(np.float64)
    y = ys[g].astype(np.float64)
    cur = g.copy()
    px, py = x.copy(), y.copy()
    total = np.zeros(len(g))
    active = np.ones(len(g), dtype=bool)
    step = np.where(plus, 1, -1)
    scale = metres_per_unit[g]
    for _ in range(16):
        nxt = cur + step
        ok = active & (nxt >= lo) & (nxt <= hi)
        cur = np.where(ok, nxt, cur)
        px = np.where(ok, xs[cur], px)
        py = np.where(ok, ys[cur], py)
        total += np.where(ok, np.hypot(px - x, py - y) * scale, 0.0)
        active = ok & (total < DIRECTION_LOOKAHEAD_M)
    return -np.arctan2(x - px, y - py)


def align_angle(diff):
    """alignAngleDifference: into (-pi, pi]."""
    return np.pi - np.mod(np.pi - diff, 2.0 * np.pi)


def expand_junctions(e_from, e_to, e_road, e_roadid, dir_in, dir_out, road_penalty, road_roundabout,
                     profile, restrictions, node_count):
    """Split every node where two or more roads meet.  Returns the new edge
    endpoints, the transition edges (from, to, cost_ms), the original node of
    every new node, and statistics."""
    m = len(e_from)
    # nodes where two or more distinct roads meet
    road_count = int(e_road.max()) + 1
    incident = np.unique(np.concatenate((e_to, e_from)).astype(np.int64) * road_count +
                         np.concatenate((e_road, e_road)))
    roads_per_node = np.bincount(incident // road_count, minlength=node_count)
    is_split = roads_per_node >= 2
    arriving = np.flatnonzero(is_split[e_to])
    leaving = np.flatnonzero(is_split[e_from])
    arriving = arriving[np.argsort(e_to[arriving], kind='stable')]
    leaving = leaving[np.argsort(e_from[leaving], kind='stable')]
    a_count = np.bincount(e_to[arriving], minlength=node_count).astype(np.int64)
    l_count = np.bincount(e_from[leaving], minlength=node_count).astype(np.int64)
    a_first = np.concatenate(([0], np.cumsum(a_count)[:-1]))
    l_first = np.concatenate(([0], np.cumsum(l_count)[:-1]))
    pair_count = a_count * l_count
    nodes = np.flatnonzero(pair_count > 0)
    total = int(pair_count[nodes].sum())
    node_of = np.repeat(nodes, pair_count[nodes])
    offsets = np.cumsum(pair_count[nodes]) - pair_count[nodes]
    local = np.arange(total, dtype=np.int64) - np.repeat(offsets, pair_count[nodes])
    a = arriving[a_first[node_of] + local // l_count[node_of]]
    l = leaving[l_first[node_of] + local % l_count[node_of]]

    # turn cost of each pair, in seconds (GeneralRouter::calculateTurnTime)
    ra, rl = e_road[a], e_road[l]
    different = ra != rl
    seconds = np.where(different, np.abs(road_penalty[rl] - road_penalty[ra]) / 2.0, 0.0)
    toggle = road_roundabout[ra] != road_roundabout[rl]
    angle = np.abs(align_angle(dir_out[l] - dir_in[a] - np.pi))
    turn = np.where(angle > np.pi / 1.5, profile.sharp_turn,
                    np.where(angle > np.pi / 3.0, profile.slight_turn,
                             np.where(angle > np.pi / 6.0, profile.slight_turn / 2.0, 0.0)))
    if profile.sharp_turn <= 0 and profile.slight_turn <= 0:
        turn = np.zeros(len(a))
    entry = profile.roundabout_turn / 2.0
    seconds += np.where(different, np.where(toggle, entry if entry > 0 else 0.0, turn), 0.0)

    # turn restrictions: remove the forbidden pairs
    forbidden = set()
    restricted = np.isin(e_roadid, np.array(list(restrictions), dtype=np.int64)) if restrictions else np.zeros(m, bool)
    cand = np.unique(e_to[restricted & is_split[e_to]])
    if len(cand):
        by_head, by_tail = defaultdict(list), defaultdict(list)
        for e in np.flatnonzero(np.isin(e_to, cand)):
            by_head[int(e_to[e])].append(int(e))
        for e in np.flatnonzero(np.isin(e_from, cand)):
            by_tail[int(e_from[e])].append(int(e))
        for node in cand.tolist():
            arrive, leave = by_head.get(node, []), by_tail.get(node, [])
            roads_here = set(int(e_roadid[e]) for e in arrive) | set(int(e_roadid[e]) for e in leave)
            for e_a in arrive:
                before = int(e_roadid[e_a])
                direct = [(to, kind) for to, via, kind in restrictions.get(before, []) if not via]
                if not direct:
                    continue
                others = roads_here - {before}
                for e_l in leave:
                    after = int(e_roadid[e_l])
                    if after != before and not allowed_exit(before, after, others, direct):
                        forbidden.add(e_a * m + e_l)
    keep = np.ones(len(a), dtype=bool)
    if forbidden:
        keep = ~np.isin(a.astype(np.int64) * m + l, np.fromiter(forbidden, dtype=np.int64, count=len(forbidden)))
    a, l, seconds = a[keep], l[keep], seconds[keep]

    in_id = np.full(m, -1, dtype=np.int64)
    out_id = np.full(m, -1, dtype=np.int64)
    in_id[arriving] = node_count + np.arange(len(arriving))
    out_id[leaving] = node_count + len(arriving) + np.arange(len(leaving))
    new_from = np.where(out_id >= 0, out_id, e_from)
    new_to = np.where(in_id >= 0, in_id, e_to)
    origin = np.concatenate((e_to[arriving], e_from[leaving]))
    transitions = (in_id[a], out_id[l], np.rint(seconds * 1000.0).astype(np.uint32))
    stats = {'split': int(is_split.sum()), 'pairs': total, 'forbidden': total - int(keep.sum()),
             'turn_cost_pairs': int((seconds > 0).sum())}
    return new_from, new_to, transitions, origin, stats


def main(rules_path, out_path, obf_paths):
    started = time.time()
    profile = routing_rules.Profile(rules_path)
    roads = []          # (road, access/oneway/speed/priority)
    seen = set()
    cache = {}
    restrictions = defaultdict(list)   # from road id -> [(to, via, type)]
    for file_index, path in enumerate(obf_paths):
        index = obf_routing.RoutingIndex(path)
        for from_id, to_id, via_id, kind in index.restrictions:
            if (to_id, via_id, kind) not in restrictions[from_id]:
                restrictions[from_id].append((to_id, via_id, kind))
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
                attrs = cache[key] = profile.road(tags) + (profile.transition_penalty(tags),
                                                          profile.roundabout(tags))
            if attrs[0]:
                roads.append((road, attrs))
        print('%s: %d roads so far (%.0fs)' % (path.rsplit('/', 1)[-1], len(roads),
                                               time.time() - started), flush=True)
    if not roads:
        raise SystemExit('no routable roads')
    via_count = sum(1 for rules in restrictions.values() for r in rules if r[1])
    print('restrictions: %d roads carry %d (%d with a via road, not modelled)' % (
        len(restrictions), sum(len(v) for v in restrictions.values()), via_count))

    counts = np.array([len(r.xs) for r, _ in roads], dtype=np.int64)
    starts = np.concatenate(([0], np.cumsum(counts)[:-1]))
    total = int(counts.sum())
    xs = np.concatenate([np.frombuffer(r.xs, dtype=np.uint32) for r, _ in roads]).astype(np.int64)
    ys = np.concatenate([np.frombuffer(r.ys, dtype=np.uint32) for r, _ in roads]).astype(np.int64)
    road_of = np.repeat(np.arange(len(roads)), counts)
    oneway = np.array([a[1] for _, a in roads], dtype=np.int8)
    speed_ms = np.array([a[2] for _, a in roads], dtype=np.float64) / 3.6
    priority = np.array([a[3] for _, a in roads], dtype=np.float64)
    road_penalty = np.array([a[4] for _, a in roads], dtype=np.float64)
    road_roundabout = np.array([a[5] for _, a in roads], dtype=bool)
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
    e_from, e_to, e_len, e_road, e_first, e_last, e_rev = (
        np.concatenate([p[i] for p in parts]) for i in range(7))
    e_pfrom = e_first - starts[e_road]
    e_pto = e_last - starts[e_road]
    e_speed = speed_ms[e_road]
    cost_ms = np.maximum(1, np.rint(e_len / (e_speed * priority[e_road]) * 1000)).astype(np.uint32)
    time_ms = np.maximum(1, np.rint(e_len / e_speed * 1000)).astype(np.uint32)
    e_roadid = road_ids[e_road]
    node_count = len(point_counts)

    # direction of each edge where it leaves its first junction and where it arrives
    # at its last (core: a1 = directionRoute(start, positive), a2 = directionRoute(end, !positive))
    travel_start = np.where(e_rev == 1, e_last, e_first)   # global point index in travel order
    travel_end = np.where(e_rev == 1, e_first, e_last)
    positive = travel_end > travel_start
    lo, hi = starts[e_road], starts[e_road] + counts[e_road] - 1
    dir_out = directions(travel_start, positive, lo, hi, xs, ys, metres_per_unit)
    dir_in = directions(travel_end, ~positive, lo, hi, xs, ys, metres_per_unit)
    print('edges %d, directions computed (%.0fs)' % (len(e_from), time.time() - started), flush=True)

    e_from, e_to, (t_from, t_to, t_cost), origin, stats = expand_junctions(
        e_from, e_to, e_road, e_roadid, dir_in, dir_out, road_penalty, road_roundabout,
        profile, restrictions, node_count)
    node_origin = np.concatenate((np.arange(node_count, dtype=np.int64), origin))
    transitions = len(t_from)
    print('junctions expanded: %d nodes, %d transition pairs (%d forbidden by restrictions, %d with a turn cost), '
          '+%d nodes (%.0fs)' % (stats['split'], stats['pairs'], stats['forbidden'], stats['turn_cost_pairs'],
                                 len(origin), time.time() - started), flush=True)
    if transitions:
        zeros_u32 = np.zeros(transitions, dtype=np.uint32)
        e_from = np.concatenate((e_from, t_from))
        e_to = np.concatenate((e_to, t_to))
        cost_ms = np.concatenate((cost_ms, t_cost))
        time_ms = np.concatenate((time_ms, zeros_u32))
        e_len = np.concatenate((e_len, np.zeros(transitions)))
        e_roadid = np.concatenate((e_roadid, np.full(transitions, -1, dtype=np.int64)))
        e_pfrom = np.concatenate((e_pfrom, zeros_u32.astype(e_pfrom.dtype)))
        e_pto = np.concatenate((e_pto, zeros_u32.astype(e_pto.dtype)))
        e_rev = np.concatenate((e_rev, np.zeros(transitions, dtype=np.uint8)))

    used, remap = np.unique(np.concatenate((e_from, e_to)), return_inverse=True)
    m = len(e_from)
    e_from, e_to = remap[:m].astype(np.uint32), remap[m:].astype(np.uint32)
    # node coordinates: any point with the node's (original) key
    node_first = np.full(node_count, -1, dtype=np.int64)
    node_first[inverse[::-1]] = np.arange(total - 1, -1, -1)
    node_point = node_first[node_origin[used]]
    node_x = xs[node_point].astype(np.int32)
    node_y = ys[node_point].astype(np.int32)

    with open(out_path, 'wb') as f:
        f.write(struct.pack('<4sII', b'CHG1', len(used), m))
        for array in (node_x, node_y, e_from, e_to, cost_ms, time_ms,
                      e_len.astype(np.float32), e_roadid,
                      e_pfrom.astype(np.uint32), e_pto.astype(np.uint32), e_rev):
            f.write(np.ascontiguousarray(array).tobytes())
    print('graph: %d nodes, %d directed edges (%d transitions), %.0f km of road, %.1f MB, %.0fs' % (
        len(used), m, transitions, e_len.sum() / 2000.0, (28 + len(used) * 8 + m * 41) / 1e6,
        time.time() - started))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
