"""Zoom levels of the map index (rendering data) of OBF files.

    python3 map_levels.py <map.obf>...

Prints, per map section, the (min zoom - max zoom) of every root level, so a renderer that
asks for a zoom the file does not carry can be told from one that finds no data.
"""
import sys

import obf_reader

MAP_INDEX = 6


def levels(path):
    out = []
    for field, offset, length in obf_reader.sections(path):
        if field != MAP_INDEX:
            continue
        buf = obf_reader.read_section(path, offset, length)
        name = ''
        found = []
        for f, wire, value in obf_reader.fields(buf):
            if f == 2:
                name = buf[value[0]:value[1]].decode(errors='replace')
            elif f == 5:
                low = high = None
                for g, _, v in obf_reader.fields(buf, value[0], value[1], int6=(5, 6)):
                    if g == 1:
                        high = v
                    elif g == 2:
                        low = v
                found.append((low, high))
        out.append((name, found))
    return out


if __name__ == '__main__':
    for path in sys.argv[1:]:
        for name, found in levels(path):
            print('%s [%s]: %s' % (path.rsplit('/', 1)[-1], name,
                                    ', '.join('%s-%s' % level for level in found)))
