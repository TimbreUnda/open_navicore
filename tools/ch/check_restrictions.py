"""Check a route (ch_tool routeedges output) against the OBF turn restrictions.

    python3 check_restrictions.py <routing.xml> <routeedges.txt> <map.obf>...

Semantics of the core's binaryRoutePlanner.cpp (processIntersections /
processRestriction): at the point where the route leaves road R, the
candidates are the roads that touch that point.  A restriction stored on R
names a destination road id and a type; NO_* forbids that candidate; an ONLY_*
restriction whose target is among the candidates forbids every other
candidate.  Continuing on R itself is never restricted.  Restrictions with a
via road also look at the road before R; they are counted but not checked.
"""
import sys

import obf_routing
import routing_rules

NO_RIGHT, NO_LEFT, NO_U, NO_STRAIGHT, ONLY_RIGHT, ONLY_LEFT, ONLY_STRAIGHT = 1, 2, 3, 4, 5, 6, 7
NO_TYPES = (NO_RIGHT, NO_LEFT, NO_U, NO_STRAIGHT)
ONLY_TYPES = (ONLY_RIGHT, ONLY_LEFT, ONLY_STRAIGHT)
NAMES = {1: 'no_right_turn', 2: 'no_left_turn', 3: 'no_u_turn', 4: 'no_straight_on',
         5: 'only_right_turn', 6: 'only_left_turn', 7: 'only_straight_on'}


def load(rules_path, obf_paths):
    profile = routing_rules.Profile(rules_path)
    restrictions = {}   # from road -> [(to, via, type)]
    at_point = {}       # (x, y) -> set of car-accessible road ids touching it
    via_count = 0
    for path in obf_paths:
        index = obf_routing.RoutingIndex(path)
        for from_id, to_id, via_id, kind in index.restrictions:
            restrictions.setdefault(from_id, []).append((to_id, via_id, kind))
            via_count += 1 if via_id else 0
        cache = {}
        for road in index.roads.values():
            # the core only loads roads its profile accepts (acceptLine)
            accepted = cache.get(road.types)
            if accepted is None:
                tags = {index.rules[t][0]: index.rules[t][1] for t in road.types if t in index.rules}
                accepted = cache[road.types] = profile.road(tags)[0]
            if not accepted:
                continue
            for x, y in zip(road.xs, road.ys):
                at_point.setdefault((x, y), set()).add(road.id)
    return restrictions, at_point, via_count


def forbidden(before, after, junction, restrictions, at_point):
    """Why the move before -> after at junction is forbidden, or None."""
    direct = [r for r in restrictions.get(before, []) if not r[1]]
    candidates = at_point.get((junction), set()) - {before}
    for to, _, kind in direct:
        if to == after and kind in NO_TYPES:
            return NAMES[kind]
    exclusive = [(to, kind) for to, _, kind in direct if kind in ONLY_TYPES and to in candidates]
    if exclusive and not any(to == after for to, _ in exclusive):
        return 'only restriction %s' % [(to, NAMES[k]) for to, k in exclusive]
    return None


def read_routes(route_path):
    """Routes from a routeedges file, or from a pairedges file (ROUTE lines)."""
    routes, current = [], []
    for line in open(route_path):
        fields = line.split()
        if fields and fields[0] == 'ROUTE':
            if current:
                routes.append(current)
            current = []
        elif len(fields) == 9 and int(fields[1]) >= 0:   # road -1 is a junction transition
            current.append(tuple(int(f) for f in fields))
    if current:
        routes.append(current)
    return routes


def check_route(edges, restrictions, at_point):
    bad = []
    changes = 0
    for previous, current in zip(edges, edges[1:]):
        if previous[1] == current[1]:
            continue
        changes += 1
        junction = (previous[7], previous[8])
        why = forbidden(previous[1], current[1], junction, restrictions, at_point)
        if why:
            bad.append((previous[1], current[1], why))
    return len(edges), changes, bad


def main(rules_path, route_path, obf_paths):
    restrictions, at_point, via_count = load(rules_path, obf_paths)
    print('restrictions: %d roads carry %d (%d with a via road)' % (
        len(restrictions), sum(len(v) for v in restrictions.values()), via_count))
    routes = read_routes(route_path)
    total_changes = total_bad = routes_bad = 0
    for edges in routes:
        count, changes, bad = check_route(edges, restrictions, at_point)
        total_changes += changes
        total_bad += len(bad)
        routes_bad += 1 if bad else 0
        for before, after, why in bad[:3]:
            print('VIOLATION %d -> %d: %s' % (before, after, why))
    print('%d routes, %d road changes checked, %d violations in %d routes' % (
        len(routes), total_changes, total_bad, routes_bad))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
