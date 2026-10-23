"""Show where an HH route and a CH route diverge (route_bench compare dump).

    python3 diff_routes.py <routing.xml> <dump.txt> <map.obf> <pair numbers>...

For every pair prints the two routes as runs of "class (road id) metres", then
the cost the car profile gives each (distance / (speed * priority) along the
roads, plus the class-transition penalties), so a systematic gap between the
cost model and OsmAnd's choice shows up.
"""
import sys

import obf_routing
import routing_rules


def parse(dump_path):
    routes = {}
    for line in open(dump_path):
        fields = line.replace('welcome to uconnect ;)', '').split()   # the unit's ssh banner
        if len(fields) < 2 or fields[0] not in ('H', 'C') or not fields[1].isdigit():
            continue
        which, pair = fields[0], int(fields[1])
        roads = [(int(f.split(':')[0]), float(f.split(':')[1])) for f in fields[2:] if ':' in f]
        routes.setdefault(pair, {})[which] = roads
    return routes


def describe(index, profile, road_id):
    road = index.roads.get(road_id)
    if road is None:
        return None
    tags = {index.rules[t][0]: index.rules[t][1] for t in road.types if t in index.rules}
    access, oneway, speed, priority = profile.road(tags)
    return {'highway': tags.get('highway', '?'), 'speed': speed, 'priority': priority,
            'ts': profile.transition_penalty(tags), 'maxspeed': tags.get('maxspeed', ''),
            'roundabout': profile.roundabout(tags)}


def summarize(index, profile, roads):
    runs, cost, transitions, previous = [], 0.0, 0.0, None
    for road_id, metres in roads:
        info = describe(index, profile, road_id)
        if info is None:
            runs.append('?(%d) %dm' % (road_id, metres))
            continue
        cost += metres / (info['speed'] / 3.6 * info['priority'])
        if previous is not None and previous['ts'] != info['ts']:
            transitions += abs(previous['ts'] - info['ts']) / 2
        previous = info
        label = info['highway'] + (' ' + info['maxspeed'] if info['maxspeed'] else '')
        if runs and runs[-1].startswith(label + ' '):
            continue
        runs.append('%s (%d) %dm' % (label, road_id, metres))
    return runs, cost, transitions


def score_all(profile, index, routes):
    """The profile's cost of both routes of every pair; how much costlier the
    reference (H) route is than the CH (C) one under this model."""
    ratios = []
    for pair in sorted(routes):
        both = routes[pair]
        if 'H' not in both or 'C' not in both:
            continue
        _, road_h, trans_h = summarize(index, profile, both['H'])
        _, road_c, trans_c = summarize(index, profile, both['C'])
        if road_c + trans_c > 0:
            ratios.append((pair, (road_h + trans_h) / (road_c + trans_c)))
    values = sorted(r for _, r in ratios)
    n = len(values)
    print('reference cost / CH cost under the model, %d pairs: min %.3f  p25 %.3f  median %.3f  '
          'p75 %.3f  p90 %.3f  max %.3f' % (n, values[0], values[n // 4], values[n // 2],
                                          values[3 * n // 4], values[int(n * 0.9)], values[-1]))
    worst = sorted(ratios, key=lambda x: -x[1])[:8]
    print('largest gaps (pair: ratio): ' + ', '.join('%d: %.3f' % w for w in worst))


def main(rules_path, dump_path, obf_path, pairs):
    profile = routing_rules.Profile(rules_path)
    index = obf_routing.RoutingIndex(obf_path)
    routes = parse(dump_path)
    if not pairs:
        score_all(profile, index, routes)
        return
    for pair in pairs:
        both = routes.get(pair)
        if not both or 'H' not in both or 'C' not in both:
            print('pair %d: not in the dump' % pair)
            continue
        print('== pair %d' % pair)
        for which, name in (('H', 'HH'), ('C', 'CH')):
            runs, cost, transitions = summarize(index, profile, both[which])
            print('%s: %d roads, road cost %.0fs + class transitions %.0fs = %.0fs' % (
                name, len(both[which]), cost, transitions, cost + transitions))
            print('   ' + ' > '.join(runs[:14]) + (' ...' if len(runs) > 14 else ''))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3], [int(a) for a in sys.argv[4:]])
