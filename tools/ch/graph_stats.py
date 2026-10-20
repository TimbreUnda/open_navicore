"""Degree statistics of a CHG1 graph: how big an edge-based (turn-cost) expansion would be.

    python3 graph_stats.py <in.graph>
"""
import struct
import sys

import numpy as np


def main(path):
    with open(path, 'rb') as f:
        magic, n, m = struct.unpack('<4sII', f.read(12))
        assert magic == b'CHG1'
        f.seek(12 + 8 * n)                                  # skip node coordinates
        e_from = np.frombuffer(f.read(4 * m), dtype='<u4')
        e_to = np.frombuffer(f.read(4 * m), dtype='<u4')
        f.seek(8 * m + 4 * m, 1)                            # cost, time
        f.seek(0, 1)
        f.read(4 * m)                                       # length
        road = np.frombuffer(f.read(8 * m), dtype='<i8')
    real = road >= 0
    indeg = np.bincount(e_to[real], minlength=n)
    outdeg = np.bincount(e_from[real], minlength=n)
    pairs = indeg.astype(np.int64) * outdeg
    print('nodes %d, real edges %d' % (n, int(real.sum())))
    for low in (1, 2, 3, 4):
        mask = (indeg >= low) & (outdeg >= low)
        print('nodes with in and out degree >= %d: %d (%.0f%%)' % (low, mask.sum(), 100.0 * mask.mean()))
    print('transition pairs if every junction were expanded: %d (%.1f per edge)' % (pairs.sum(), pairs.sum() / real.sum()))
    three = (indeg + outdeg) >= 5     # a real intersection (not a chain node or a way split)
    print('transition pairs at intersections only (in+out >= 5): %d on %d nodes (%.1f per edge)' % (
        pairs[three].sum(), three.sum(), pairs[three].sum() / real.sum()))


if __name__ == '__main__':
    main(sys.argv[1])
