#pragma once

#include "vector_style.h"

#include <array>
#include <cstdint>
#include <vector>

namespace navicore_vector {

// Tile-local coordinates run 0..kTileExtent on both axes (y down).
constexpr float kTileExtent = 4096.0f;

using Point = std::array<float, 2>;
using Ring = std::vector<Point>;

struct AreaVertex {
    float x, y;
    std::uint8_t rgba[4];
};
static_assert(sizeof(AreaVertex) == 12, "packed for GL");

// A line vertex sits on the centre line; the shader pushes it out by
// normal * half width (a per-vertex width scaled by zoom). side is -1/+1 and
// gives the edge distance for anti-aliasing.
struct LineVertex {
    float x, y;
    float nx, ny;  // miter-scaled unit normal
    float side;
    float width;         // fill width, px at zoom 16
    float casing_width;  // 0: no casing
    std::uint8_t fill[4];
    std::uint8_t casing[4];
};
static_assert(sizeof(LineVertex) == 36, "packed for GL");

struct TileMesh {
    std::vector<AreaVertex> area_vertices;
    std::vector<std::uint32_t> area_indices;
    std::vector<LineVertex> line_vertices;
    std::vector<std::uint32_t> line_indices;
};

// Collects features per draw layer, then emits one sorted mesh.
class TileTessellator {
public:
    // outer + holes, tile-local; clipped to the tile.
    void add_area(const Ring& outer, const std::vector<Ring>& holes,
                  const AreaStyle& style);
    // Polyline (or a closed ring for outlines), clipped to the tile.
    void add_line(const Ring& points, const LineStyle& style);
    // Same, without clipping (a route in its own coordinate frame).
    void add_line_unclipped(const Ring& points, const LineStyle& style);
    TileMesh finish();
    // Same into `mesh`, reusing its capacity; the tessellator is left empty
    // (with its buffers kept) for the next mesh.
    void finish_into(TileMesh& mesh);
    // Drops collected features, keeping the buffers.
    void clear();

    std::size_t clipped_away() const { return clipped_away_; }

private:
    struct Bucket {
        std::vector<AreaVertex> vertices;
        std::vector<std::uint32_t> indices;
    };
    struct LineBucket {
        std::vector<LineVertex> vertices;
        std::vector<std::uint32_t> indices;
    };
    void extrude(const Ring& run, const LineStyle& style, LineBucket& out);

    Bucket areas_[kAreaLayers];
    LineBucket lines_[kLineLayers];
    Ring run_;  // extrude() scratch
    std::size_t clipped_away_ = 0;
};

// Exposed for tests.
Ring clip_ring(const Ring& ring, float min, float max);
std::vector<Ring> clip_polyline(const Ring& line, float min, float max);

} // namespace navicore_vector
