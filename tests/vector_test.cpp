#include "vector/vector_style.h"
#include "vector/vector_tessellator.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace navicore_vector;

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

double ring_area(const Ring& ring)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const auto& a = ring[i];
        const auto& b = ring[(i + 1) % ring.size()];
        sum += static_cast<double>(a[0]) * b[1] - static_cast<double>(b[0]) * a[1];
    }
    return std::fabs(sum) / 2.0;
}

double mesh_area(const TileMesh& mesh)
{
    double sum = 0.0;
    for (std::size_t i = 0; i + 2 < mesh.area_indices.size(); i += 3) {
        const auto& a = mesh.area_vertices[mesh.area_indices[i]];
        const auto& b = mesh.area_vertices[mesh.area_indices[i + 1]];
        const auto& c = mesh.area_vertices[mesh.area_indices[i + 2]];
        sum += std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2.0;
    }
    return sum;
}

Tags tags(std::initializer_list<std::pair<const char*, const char*>> list)
{
    Tags out;
    for (const auto& entry : list)
        out.emplace_back(entry.first, entry.second);
    return out;
}
} // namespace

int main()
{
    // A square half outside the tile keeps only its inside half.
    const Ring square{{-100, 100}, {100, 100}, {100, 300}, {-100, 300}};
    const Ring clipped = clip_ring(square, 0, kTileExtent);
    check(std::fabs(ring_area(clipped) - 100.0 * 200.0) < 1.0, "clip_ring area");
    check(clip_ring({{-10, -10}, {-5, -10}, {-5, -5}}, 0, kTileExtent).empty(),
          "outside ring vanishes");

    // A line leaving and re-entering the tile becomes two runs.
    const auto runs = clip_polyline(
        {{100, 100}, {100, -100}, {300, -100}, {300, 100}}, 0, kTileExtent);
    check(runs.size() == 2, "two runs");
    check(runs.size() == 2 && runs[0].back()[1] == 0.0f && runs[1].front()[1] == 0.0f,
          "runs end on the border");
    check(clip_polyline({{10, 10}, {20, 20}, {30, 10}}, 0, kTileExtent).size() == 1,
          "inside line is one run");

    // Area with a hole: triangles cover outer minus hole.
    TileTessellator tess;
    tess.add_area({{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}},
                  {{{250, 250}, {750, 250}, {750, 750}, {250, 750}}},
                  {AreaLayer::Park, {1, 2, 3, 255}});
    // Polyline of three points: 6 vertices, 4 triangles.
    LineStyle road{LineLayer::Primary, {255, 255, 255, 255}, {200, 200, 200, 255},
                   8.0f, 10.0f};
    tess.add_line({{10, 10}, {500, 10}, {500, 500}}, road);
    const TileMesh mesh = tess.finish();
    check(std::fabs(mesh_area(mesh) - (1000.0 * 1000 - 500.0 * 500)) < 1.0,
          "earcut covers outer minus hole");
    check(mesh.line_vertices.size() == 6, "line vertices");
    check(mesh.line_indices.size() == 12, "line indices");
    // The corner vertex is mitered: |normal| = sqrt(2) for a right angle.
    const auto& corner = mesh.line_vertices[2];
    check(std::fabs(std::sqrt(corner.nx * corner.nx + corner.ny * corner.ny) -
                    std::sqrt(2.0f)) < 1e-3f, "miter length");

    // Layer order: water before buildings; motorway lines after minor roads.
    TileTessellator ordered;
    ordered.add_area({{0, 0}, {10, 0}, {10, 10}}, {}, {AreaLayer::Building, {9, 9, 9, 255}});
    ordered.add_area({{0, 0}, {10, 0}, {10, 10}}, {}, {AreaLayer::Water, {7, 7, 7, 255}});
    const TileMesh order_mesh = ordered.finish();
    check(order_mesh.area_vertices.front().rgba[0] == 7, "water drawn first");

    // Classification.
    check(classify(tags({{"highway", "motorway"}}), false, 15).line.layer ==
              LineLayer::Motorway, "motorway");
    check(classify(tags({{"highway", "primary_link"}}), false, 15).line.width < 8.0f,
          "link narrower");
    check(!classify(tags({{"highway", "footway"}}), false, 15).has_line,
          "footway hidden at z15");
    check(classify(tags({{"building", "yes"}}), true, 16).has_line,
          "building outline at z16");
    check(!classify(tags({{"building", "yes"}}), true, 14).has_area,
          "no buildings at z14");
    check(classify(tags({{"natural", "water"}}), true, 12).area.layer == AreaLayer::Water,
          "water");
    check(!classify(tags({{"amenity", "place_of_worship"}}), true, 16).has_area,
          "churches are not drawn");
    const auto tunnel = classify(tags({{"highway", "primary"}, {"tunnel", "yes"}}), false, 15);
    check(tunnel.line.casing_width == 0.0f && tunnel.line.fill.a < 255, "tunnel faded");

    // Closed shapes: buildings are areas, roundabouts stay lines.
    check(treat_as_area(tags({{"building", "residential"}}), false, true),
          "closed building is an area");
    check(!treat_as_area(tags({{"highway", "primary"}}), false, true),
          "roundabout stays a line");
    check(treat_as_area(tags({{"highway", "pedestrian"}, {"area", "yes"}}), false, true),
          "pedestrian area");
    check(!treat_as_area(tags({{"barrier", "fence"}}), false, true), "fence stays a line");
    check(!treat_as_area(tags({{"building", "yes"}}), false, false), "open line");

    if (failures == 0)
        std::puts("vector-test: ok");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
