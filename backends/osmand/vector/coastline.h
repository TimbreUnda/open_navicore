#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace navicore_vector {

// A closed ring in 31-bit map coordinates cut out of coastline polylines.
struct CoastlineRing {
    std::vector<std::pair<std::int32_t, std::int32_t>> points;
    bool water = false;  // sea (true) or an island inside it
};

// Builds sea polygons and islands from the natural=coastline polylines of a
// tile, closing the open ones along the tile border (OsmAnd's own algorithm,
// the core's getCoastlines, ported). `left..bottom` is the tile in 31-bit
// coordinates, inclusive (right = (x + 1) * size - 1). False when the coastlines do not form a usable surface (the
// caller then falls back to the OBF ocean flags of the tile).
bool polygonize_coastlines(
    const std::vector<std::vector<std::pair<std::int32_t, std::int32_t>>>& lines,
    std::int32_t left, std::int32_t right, std::int32_t bottom, std::int32_t top, int zoom,
    std::vector<CoastlineRing>& out);

} // namespace navicore_vector
