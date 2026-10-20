"""Random start/end pairs on car roads, for route_bench compare.

    python3 gen_pairs.py <routing.xml> <count> <min km> <max km> <seed> <map.obf>... > pairs.txt

Each end is an interior shape point of a car-accessible road (never a point shared with
another road), so it has one obvious nearest road, like a GPS position or a chosen
address, and the reference router and CH start on the same road.  Prints
"lat1 lon1 lat2 lon2" per line.
"""
import math
import random
import sys

import obf_routing
import routing_rules


def to_lat_lon(x, y):
    lon = x / 2 ** 31 * 360.0 - 180.0
    lat = math.degrees(math.atan(math.sinh(math.pi * (1.0 - 2.0 * y / 2 ** 31))))
    return lat, lon


def km(a, b):
    r = 6371.0
    p1, p2 = math.radians(a[0]), math.radians(b[0])
    dp, dl = p2 - p1, math.radians(b[1] - a[1])
    h = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(h))


def main(rules_path, count, low, high, seed, obf_paths):
    profile = routing_rules.Profile(rules_path)
    shared = {}                       # point -> number of roads through it
    candidates = []                   # (x, y) interior points of car roads
    for path in obf_paths:
        index = obf_routing.RoutingIndex(path)
        cache = {}
        for road in index.roads.values():
            ok = cache.get(road.types)
            if ok is None:
                tags = {index.rules[t][0]: index.rules[t][1] for t in road.types if t in index.rules}
                ok = cache[road.types] = profile.road(tags)[0]
            if not ok:
                continue
            for x, y in zip(road.xs, road.ys):
                shared[(x, y)] = shared.get((x, y), 0) + 1
            if len(road.xs) >= 4:
                for k in range(1, len(road.xs) - 1):
                    candidates.append((road.xs[k], road.ys[k]))
    candidates = [p for p in candidates if shared[p] == 1]
    rng = random.Random(seed)
    out = 0
    while out < count:
        a, b = rng.choice(candidates), rng.choice(candidates)
        la, lb = to_lat_lon(*a), to_lat_lon(*b)
        if low <= km(la, lb) <= high:
            print('%.6f %.6f %.6f %.6f' % (la[0], la[1], lb[0], lb[1]))
            out += 1


if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4]), int(sys.argv[5]), sys.argv[6:])
