"""Decode the routing section of an OBF file into roads.

Layout recovered from OsmAnd-core-legacy native/src/binaryRead.cpp
(readRoutingIndex, readRouteTree, readRouteTreeData, readRouteDataObject):

  section 9 (OsmAndRoutingIndex)
    2  rules      repeated {3 tag, 5 value, 7 id}; ids count up from 1
    3  rootBoxes  repeated box, 4-byte length
    box           1 left, 2 right, 3 top, 4 bottom (sint32, relative to the
                  parent box), 5 shiftToData (fixed32, from the box start to
                  a varint length + RouteDataBlock), 7 child boxes
    block         5 idTable {1 sint64 delta routeId}, 6 dataObjects,
                  7 restrictions, 8 stringTable
    dataObject    1 points (sint32 pairs, deltas of x>>4 / y>>4 starting at
                  the box left/top), 7 types (rule ids), 12 routeId (index
                  into idTable)

Coordinates are 31-bit tile numbers.
"""
from array import array
from collections import namedtuple

import obf_reader

Road = namedtuple('Road', 'id xs ys types')

POINT_SHIFT = 4


def _zigzag(n):
    return (n >> 1) ^ -(n & 1)


def _varints(buf, start, end):
    out = []
    position = start
    while position < end:
        value = shift = 0
        while True:
            byte = buf[position]
            position += 1
            value |= (byte & 0x7F) << shift
            shift += 7
            if not byte & 0x80:
                break
        out.append(value)
    return out


class RoutingIndex:
    def __init__(self, path):
        self.path = path
        self.rules = {}   # id -> (tag, value)
        self.roads = {}   # route id -> Road
        self.restrictions = []  # (from route id, to route id, via route id, type)
        for field, offset, length in obf_reader.sections(path):
            if field == obf_reader.ROUTING:
                self._read(obf_reader.read_section(path, offset, length))
                break
        else:
            raise ValueError('%s has no routing index' % path)

    def _read(self, buf):
        next_id = 1
        roots = []
        for field, wire, value in obf_reader.fields(buf):
            if field == 2:
                tag = text = rule_id = None
                for f, _, v in obf_reader.fields(buf, value[0], value[1]):
                    if f == 3:
                        tag = buf[v[0]:v[1]].decode()
                    elif f == 5:
                        text = buf[v[0]:v[1]].decode()
                    elif f == 7:
                        rule_id = v
                self.rules[rule_id if rule_id is not None else next_id] = (tag, text)
                next_id += 1
            elif field == 3:
                roots.append(value)
        for start, end in roots:
            self._box(buf, start, end, (0, 0, 0, 0))

    def _box(self, buf, start, end, parent):
        left, right, top, bottom = parent
        shift = None
        children = []
        for field, wire, value in obf_reader.fields(buf, start, end, int6=(5,)):
            if field == 1:
                left = parent[0] + _zigzag(value)
            elif field == 2:
                right = parent[1] + _zigzag(value)
            elif field == 3:
                top = parent[2] + _zigzag(value)
            elif field == 4:
                bottom = parent[3] + _zigzag(value)
            elif field == 5:
                shift = value
            elif field == 7:
                children.append(value)
        if shift is not None:
            self._block(buf, start + shift, left, top)
        for child_start, child_end in children:
            self._box(buf, child_start, child_end, (left, right, top, bottom))

    def _block(self, buf, position, left, top):
        length = shift = 0
        while True:
            byte = buf[position]
            position += 1
            length |= (byte & 0x7F) << shift
            shift += 7
            if not byte & 0x80:
                break
        ids = []
        objects = []
        restrictions = []
        for field, wire, value in obf_reader.fields(buf, position, position + length):
            if field == 5:
                route_id = 0
                for f, _, v in obf_reader.fields(buf, value[0], value[1]):
                    if f == 1:
                        route_id += _zigzag(v)
                        ids.append(route_id)
            elif field == 6:
                objects.append(value)
            elif field == 7:
                info = {1: 0, 2: 0, 3: 0, 4: 0}
                for f, _, v in obf_reader.fields(buf, value[0], value[1]):
                    info[f] = v
                restrictions.append(info)
        for start, end in objects:
            xs, ys, types, index = array('I'), array('I'), (), 0
            for field, wire, value in obf_reader.fields(buf, start, end):
                if field == 1:
                    px, py = left >> POINT_SHIFT, top >> POINT_SHIFT
                    numbers = _varints(buf, value[0], value[1])
                    for i in range(0, len(numbers), 2):
                        px += _zigzag(numbers[i])
                        py += _zigzag(numbers[i + 1])
                        xs.append(px << POINT_SHIFT)
                        ys.append(py << POINT_SHIFT)
                elif field == 7:
                    types = tuple(_varints(buf, value[0], value[1]))
                elif field == 12:
                    index = value
            route_id = ids[index]
            self.roads.setdefault(route_id, Road(route_id, xs, ys, types))
        for info in restrictions:
            # 1 type, 2 from, 3 to, 4 via: id table indexes (via 0 = none)
            self.restrictions.append((ids[info[2]], ids[info[3]],
                                      ids[info[4]] if info[4] else 0, info[1]))
