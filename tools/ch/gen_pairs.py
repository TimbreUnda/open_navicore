"""Random start/end pairs on road nodes of a CHG1 graph, for route_bench compare.

    python3 gen_pairs.py <in.graph> <count> <min km> <max km> [seed] > pairs.txt

Prints "lat1 lon1 lat2 lon2" per line.
"""
import math
import random
import struct
import sys

import numpy as np


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


def main(path, count, low, high, seed):
    with open(path, 'rb') as f:
        magic, n, m = struct.unpack('<4sII', f.read(12))
        assert magic == b'CHG1'
        x = np.frombuffer(f.read(4 * n), dtype='<i4')
        y = np.frombuffer(f.read(4 * n), dtype='<i4')
    rng = random.Random(seed)
    out = 0
    while out < count:
        i, j = rng.randrange(n), rng.randrange(n)
        a, b = to_lat_lon(int(x[i]), int(y[i])), to_lat_lon(int(x[j]), int(y[j]))
        if low <= km(a, b) <= high:
            print('%.6f %.6f %.6f %.6f' % (a[0], a[1], b[0], b[1]))
            out += 1


if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4]),
         int(sys.argv[5]) if len(sys.argv) > 5 else 1)
