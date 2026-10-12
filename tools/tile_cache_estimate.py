#!/usr/bin/env python3
"""Estimate a pre-rendered tile cache and write its pre-cache manifest.

Uses the same Web-Mercator tile ranges as backends/osmand/tile_cache.cpp and
the same 16x16-tile pack layout. Tile payload size is an assumption until it
is measured on real tiles: pass --avg-kb from a sample pre-cache run
(compressed bytes / tiles in "<manifest>.progress" + pack sizes).

Examples:
  tile_cache_estimate.py --region kyiv --zooms 10-17
  tile_cache_estimate.py --bbox 50.2 30.2 50.6 30.9 --zooms 12-17 \
      --manifest precache.manifest
"""

import argparse
import math
import sys

REGIONS = {
    # min_lat, min_lon, max_lat, max_lon
    "kyiv": (50.21, 30.24, 50.59, 30.83),
    "kyiv-oblast": (49.18, 29.27, 51.56, 32.16),
    "ukraine": (44.38, 22.14, 52.38, 40.23),
}
PACK_HEADER_BYTES = 32 + 256 * 16


def tile_x(longitude, zoom):
    n = 1 << zoom
    return min(n - 1, max(0, int((longitude + 180.0) / 360.0 * n)))


def tile_y(latitude, zoom):
    n = 1 << zoom
    latitude = max(-85.0511, min(85.0511, latitude))
    r = math.radians(latitude)
    y = (1.0 - math.log(math.tan(r) + 1.0 / math.cos(r)) / math.pi) / 2.0 * n
    return min(n - 1, max(0, int(y)))


def tile_range(zoom, min_lat, min_lon, max_lat, max_lon):
    return (tile_x(min_lon, zoom), tile_y(max_lat, zoom),
            tile_x(max_lon, zoom), tile_y(min_lat, zoom))


def parse_zooms(text):
    first, _, last = text.partition("-")
    first = int(first)
    last = int(last) if last else first
    if not 0 <= first <= last <= 21:
        raise argparse.ArgumentTypeError("zooms must be within 0-21")
    return first, last


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    where = parser.add_mutually_exclusive_group(required=True)
    where.add_argument("--region", choices=sorted(REGIONS))
    where.add_argument("--bbox", nargs=4, type=float,
                       metavar=("MIN_LAT", "MIN_LON", "MAX_LAT", "MAX_LON"))
    parser.add_argument("--zooms", type=parse_zooms, default=(10, 17))
    parser.add_argument("--avg-kb", type=float, default=14.0,
                        help="assumed compressed KiB per tile (default 14)")
    parser.add_argument("--manifest", help="write a pre-cache manifest here")
    args = parser.parse_args(argv)

    box = REGIONS[args.region] if args.region else tuple(args.bbox)
    min_lat, min_lon, max_lat, max_lon = box
    if not (min_lat < max_lat and min_lon < max_lon):
        parser.error("bbox must be min < max")
    first, last = args.zooms

    total_tiles = 0
    total_packs = 0
    print(f"{'zoom':>4} {'tiles':>12} {'packs':>9} {'size':>10}")
    for zoom in range(first, last + 1):
        x0, y0, x1, y1 = tile_range(zoom, *box)
        tiles = (x1 - x0 + 1) * (y1 - y0 + 1)
        packs = ((x1 >> 4) - (x0 >> 4) + 1) * ((y1 >> 4) - (y0 >> 4) + 1)
        size = tiles * args.avg_kb * 1024 + packs * PACK_HEADER_BYTES
        total_tiles += tiles
        total_packs += packs
        print(f"{zoom:>4} {tiles:>12,} {packs:>9,} {size / 2**30:>9.2f}G")
    total = total_tiles * args.avg_kb * 1024 + total_packs * PACK_HEADER_BYTES
    print(f"{'all':>4} {total_tiles:>12,} {total_packs:>9,} {total / 2**30:>9.2f}G")
    print(f"assumes {args.avg_kb:g} KiB per compressed tile (unmeasured)")

    if args.manifest:
        with open(args.manifest, "w") as output:
            output.write("# min_zoom max_zoom min_lat min_lon max_lat max_lon\n")
            output.write(f"{first} {last} {min_lat} {min_lon} {max_lat} {max_lon}\n")
        print(f"manifest written to {args.manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
