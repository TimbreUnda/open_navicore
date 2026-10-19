"""Print what the car profile makes of some OBF roads.

    python3 road_info.py <routing.xml> <map.obf> <road id>...
"""
import sys

import obf_routing
import routing_rules


def main(rules_path, obf_path, ids):
    index = obf_routing.RoutingIndex(obf_path)
    profile = routing_rules.Profile(rules_path)
    for road_id in ids:
        road = index.roads.get(road_id)
        if not road:
            print(road_id, 'not in this map')
            continue
        tags = {index.rules[t][0]: index.rules[t][1] for t in road.types if t in index.rules}
        access, oneway, speed, priority = profile.road(tags)
        shown = {k: tags[k] for k in ('highway', 'access', 'motor_vehicle', 'motorcar', 'oneway') if k in tags}
        print(road_id, 'car access:', access, 'oneway:', oneway, 'speed:', speed, shown)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], [int(a) for a in sys.argv[3:]])
