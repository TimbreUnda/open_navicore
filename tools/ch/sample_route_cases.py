"""Seeded route cases from interior car-road points, one OBF region at a time.

Usage: sample_route_cases.py ROUTING_XML MAP_DIR OUTPUT_DIR [SEED]
Writes sampled endpoints with road provenance, and 128 forward/reverse cases:
26 regional pairs, 26 cross-region pairs, and 12 short same-road pairs.
The profile approximation is the one used by the CH graph builder. Runtime
snapping and route availability are checked by the real navigation worker.
"""
import gc
import json
from pathlib import Path
import random
import sys

import obf_routing
import routing_rules
from gen_pairs import km, to_lat_lon


def sample(xml, directory, output, seed):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    rng = random.Random(seed)
    profile = routing_rules.Profile(xml)
    regions = {}
    shorts = {}
    for path in sorted(Path(directory).glob('Ukraine_*.obf')):
        region = path.name[len('Ukraine_'):-len('_europe_2.obf')]
        index = obf_routing.RoutingIndex(str(path))
        access = {}
        eligible = []
        for road in index.roads.values():
            if road.types not in access:
                tags = dict(index.rules[t] for t in road.types if t in index.rules)
                access[road.types] = profile.road(tags)[0]
            if access[road.types] and len(road.xs) >= 4:
                eligible.append(road)
        selected = rng.sample(eligible, min(256, len(eligible)))
        points = []
        short = []
        for road in selected:
            i = rng.randrange(1, len(road.xs) - 2)
            a = to_lat_lon(road.xs[i], road.ys[i])
            b = to_lat_lon(road.xs[i + 1], road.ys[i + 1])
            # Mid-segment endpoints exercise road snapping, rather than merely
            # selecting CH nodes. The original road and indexes stay recorded.
            point = tuple((x + y) / 2 for x, y in zip(a, b))
            points.append(dict(region=region, road=road.id, segment=i,
                               lat=point[0], lon=point[1]))
            first = to_lat_lon(road.xs[1], road.ys[1])
            last = to_lat_lon(road.xs[-2], road.ys[-2])
            if 0.015 <= km(first, last) <= 0.8:
                short.append((dict(region=region, road=road.id, segment=1,
                                   lat=first[0], lon=first[1]),
                              dict(region=region, road=road.id, segment=len(road.xs)-2,
                                   lat=last[0], lon=last[1])))
        regions[region] = points
        shorts[region] = short
        print(region, 'roads', len(index.roads), 'car_candidates', len(eligible),
              'sampled', len(points), 'short', len(short), flush=True)
        # Release each decoded region before loading the next one.
        del index, eligible, selected, road
        gc.collect()

    cases = []
    def add(kind, a, b):
        distance = km((a['lat'], a['lon']), (b['lat'], b['lon']))
        group = len(cases) // 2
        for suffix, start, end in [('f', a, b), ('r', b, a)]:
            cases.append(dict(id=f'{kind}_{group:03d}_{suffix}', kind=kind,
                              start=start, end=end, air_km=distance))
    for region, points in regions.items():
        for _ in range(10000):
            a, b = rng.sample(points, 2)
            if 2 <= km((a['lat'], a['lon']), (b['lat'], b['lon'])) <= 40:
                add('regional', a, b)
                break
        else:
            raise RuntimeError('no regional pair in ' + region)
    names = list(regions)
    rng.shuffle(names)
    for i, region in enumerate(names):
        other = names[(i + 1) % len(names)]
        add('cross', rng.choice(regions[region]), rng.choice(regions[other]))
    for region in rng.sample([n for n in names if shorts[n]], 12):
        add('short', *rng.choice(shorts[region]))
    (output / 'cases.json').write_text(json.dumps(dict(seed=seed, regions=regions, cases=cases),
                                                ensure_ascii=False, indent=2))
    with (output / 'cases.tsv').open('w') as stream:
        for case in cases:
            a, b = case['start'], case['end']
            stream.write(f"{case['id']} {a['lat']:.8f} {a['lon']:.8f} {b['lat']:.8f} {b['lon']:.8f}\n")
    print('cases', len(cases), 'seed', seed, flush=True)


if __name__ == '__main__':
    sample(*sys.argv[1:4], int(sys.argv[4]) if len(sys.argv) > 4 else 20261028)
