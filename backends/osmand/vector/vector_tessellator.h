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

// A sprite vertex is a corner of a small shape (one-way arrow, zebra
// stripe) at a map point. The shader places it at offset (u along the
// direction, v across it) in pixels at zoom 16, scaled with zoom like line
// widths, so the shape keeps its size on screen and turns with the map.
// The shape is hidden while the zoom scale is below min_scale.
struct SpriteVertex {
    float x, y;
    float dx, dy;  // direction, tile-local (any length)
    float u, v;
    std::uint8_t rgba[4];
    float min_scale;
};
static_assert(sizeof(SpriteVertex) == 32, "packed for GL");

struct TileMesh {
    std::vector<AreaVertex> area_vertices;
    std::vector<std::uint32_t> area_indices;
    std::vector<LineVertex> line_vertices;
    std::vector<std::uint32_t> line_indices;
    std::vector<SpriteVertex> sprite_vertices;
    std::vector<std::uint32_t> sprite_indices;
    // Level i owns indices [start[i], start[i + 1]) of its index array.
    std::array<std::uint32_t, kLineLevels + 1> line_level_start{};
    std::array<std::uint32_t, kLineLevels + 1> sprite_level_start{};
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
    // One-way arrows along a road (unclipped path, tile-local), pointing
    // along it (direction 1) or against it (-1), every `spacing` units;
    // arrows whose centre is outside the tile are left to its neighbour.
    void add_oneway_arrows(const Ring& path, const LineStyle& road, int direction,
                           Rgba colour, float spacing);
    // Zebra stripes across a road of fill width `road_width` (px at z16)
    // at `at`, the road running along `along`.
    void add_zebra(const Point& at, const Point& along, float road_width,
                   std::uint8_t level, Rgba colour, float min_scale);
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
    struct SpriteBucket {
        std::vector<SpriteVertex> vertices;
        std::vector<std::uint32_t> indices;
    };
    void extrude(const Ring& run, const LineStyle& style, LineBucket& out);
    LineBucket& line_bucket(const LineStyle& style);
    // A rectangle (u0..u1 along, v0..v1 across) or a triangle in sprite
    // offsets around `at`.
    void sprite_quad(std::uint8_t level, const Point& at, const Point& dir, float u0,
                     float v0, float u1, float v1, Rgba colour, float min_scale);
    void sprite_triangle(std::uint8_t level, const Point& at, const Point& dir,
                         const float (&uv)[3][2], Rgba colour, float min_scale);

    Bucket areas_[kAreaLayers];
    LineBucket lines_[kLineLevels][kLineLayers];
    SpriteBucket sprites_[kLineLevels];
    Ring run_;  // extrude() scratch
    std::size_t clipped_away_ = 0;
};

// Exposed for tests.
Ring clip_ring(const Ring& ring, float min, float max);
std::vector<Ring> clip_polyline(const Ring& line, float min, float max);

} // namespace navicore_vector
