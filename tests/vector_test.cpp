#include "vector/boundary_filter.h"
#include "vector/vector_style.h"
#include "vector/vector_tessellator.h"

#include <algorithm>
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

struct P31 {
    std::int32_t x, y;
};

P31 at(double lat, double lon)
{
    return {static_cast<std::int32_t>(lon_to_x31(lon)), static_cast<std::int32_t>(lat_to_y31(lat))};
}

void crimea_border_checks()
{
    const Box31& box = crimea_box();
    const P31 perekop = at(46.15, 33.7), simferopol = at(44.95, 34.1), kyiv = at(50.45, 30.52);
    const P31 kherson = at(46.64, 32.62), kerch_strait = at(45.35, 36.7), azov_far = at(46.5, 37.5);
    check(box.contains(perekop.x, perekop.y), "Perekop is inside the Crimea box");
    check(box.contains(simferopol.x, simferopol.y), "Simferopol is inside the Crimea box");
    check(!box.contains(kyiv.x, kyiv.y), "Kyiv is outside the Crimea box");
    check(!box.contains(kherson.x, kherson.y), "Kherson is outside the Crimea box");
    check(!box.contains(kerch_strait.x, kerch_strait.y), "the Kerch strait border stays");
    check(!box.contains(azov_far.x, azov_far.y), "the Azov border far east stays");
    // A line from Kherson through Perekop to the Azov coast keeps the outside parts only.
    const std::vector<P31> line{kherson, at(46.5, 32.0), at(46.6, 31.5)};
    check(split_outside_box(box, line).size() == 1, "a line outside Crimea is kept whole");
    const std::vector<P31> through{at(47.0, 33.0), at(46.8, 33.5), perekop, simferopol, at(45.0, 35.0),
                                   kerch_strait, azov_far};
    const auto runs = split_outside_box(box, through);
    check(runs.size() == 2, "a line through Crimea splits into two outside runs");
    check(runs.size() == 2 && runs[0].size() == 3 && runs[1].size() == 2, "the Crimea middle is removed");
    check(split_outside_box(box, std::vector<P31>{perekop, simferopol, at(45.0, 35.0)}).empty(),
          "a line entirely inside Crimea is dropped");
    check(is_russian_crimea_entity("Республика Крым", "", ""), "Republic of Crimea (ru name) is dropped");
    check(is_russian_crimea_entity("", "Республіка Крим", ""), "Republic of Crimea (uk name) is dropped");
    check(is_russian_crimea_entity("", "", "Republic of Crimea"), "Republic of Crimea (en name) is dropped");
    check(!is_russian_crimea_entity("Автономна Республіка Крим", "Автономна Республіка Крим",
                                    "Autonomous Republic of Crimea"),
          "the Ukrainian Autonomous Republic of Crimea stays");
    check(!is_russian_crimea_entity("Севастополь", "Севастополь", "Sevastopol"), "Sevastopol stays");
    check(is_country_border({{"admin_level", "2"}}), "admin_level 2 is a country border");
    check(!is_country_border({{"admin_level", "4"}}), "admin_level 4 is not a country border");
}

int main()
{
    crimea_border_checks();
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

    // Reuse: finish_into() leaves the tessellator empty and the same input
    // gives the same mesh in the same buffers.
    {
        TileTessellator reused;
        TileMesh into;
        const Ring route{{0, 0}, {300, 0}, {300, 300}, {600, 300}};
        reused.add_line_unclipped(route, road);
        reused.finish_into(into);
        const std::size_t vertices = into.line_vertices.size();
        const auto* data = into.line_vertices.data();
        reused.add_line_unclipped(route, road);
        reused.finish_into(into);
        check(vertices == 8 && into.line_vertices.size() == vertices &&
                  into.line_indices.size() == 18, "finish_into same mesh");
        check(into.line_vertices.data() == data, "finish_into keeps capacity");
        reused.clear();
        reused.finish_into(into);
        check(into.line_vertices.empty() && into.line_indices.empty(), "clear empties");
    }

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

    // POIs: car services shown early, food only close in, churches never.
    check(classify_poi(tags({{"amenity", "fuel"}}), 14).show, "fuel at z14");
    check(!classify_poi(tags({{"amenity", "cafe"}}), 15).show, "cafe hidden at z15");
    check(classify_poi(tags({{"amenity", "cafe"}}), 16).show, "cafe at z16");
    // Driver priorities: car services before trip needs before sights.
    check(classify_poi(tags({{"highway", "speed_camera"}}), 15).show, "speed camera at z15");
    check(classify_poi(tags({{"highway", "rest_area"}}), 13).show, "rest area at z13");
    check(classify_poi(tags({{"shop", "car_repair"}}), 15).rank <
              classify_poi(tags({{"amenity", "restaurant"}}), 16).rank, "repair outranks food");
    check(classify_poi(tags({{"amenity", "toilets"}}), 16).rank <
              classify_poi(tags({{"tourism", "museum"}}), 16).rank, "toilets outrank museums");
    check(!classify_poi(tags({{"amenity", "place_of_worship"}}), 18).show, "no churches");
    check(!classify_poi(tags({{"amenity", "library"}}), 18).show, "no libraries");
    check(classify_poi(tags({{"amenity", "fuel"}}), 15).rank <
              classify_poi(tags({{"amenity", "restaurant"}}), 17).rank, "fuel outranks food");
    check(street_label_rank("primary") < street_label_rank("residential"), "street rank");
    check(street_label_rank("footway") < 0, "no footway names");
    check(place_label_rank("suburb", 13) >= 0 && place_label_rank("suburb", 17) < 0,
          "suburb names zoomed out only");

    // Overview maps (basemap, zoom 4-10): visible roads, borders, landcover,
    // sea, and place names by class.
    for (const bool night : {false, true}) {
        const auto primary = classify(tags({{"highway", "primary"}}), false, 7, night);
        check(primary.has_line && primary.line.casing_width == 0.0f,
              "overview primary has a line");
        // At least a pixel and a half on screen: width * 2^((z - 16) / 2).
        check(primary.line.width * std::exp2((7 - 16) * 0.5f) >= 1.3f,
              "overview primary visible");
        check(!classify(tags({{"highway", "secondary"}}), false, 6, night).has_line &&
                  classify(tags({{"highway", "secondary"}}), false, 8, night).has_line,
              "secondary from z8");
        check(!classify(tags({{"highway", "tertiary"}}), false, 9, night).has_line &&
                  classify(tags({{"highway", "tertiary"}}), false, 10, night).has_line,
              "tertiary from z10");
        check(!classify(tags({{"highway", "motorway_link"}}), false, 8, night).has_line,
              "no links in the overview");
        check(classify(tags({{"highway", "motorway"}}), false, 6, night).line.fill.r !=
                  land_colour(night).r ||
                  classify(tags({{"highway", "motorway"}}), false, 6, night).line.fill.b !=
                      land_colour(night).b,
              "overview motorway differs from land");
        check(classify(tags({{"admin_level", "2"}}), false, 5, night).line.layer ==
                  LineLayer::Boundary, "country border");
        check(classify(tags({{"admin_level", "4"}}), false, 6, night).has_line &&
                  !classify(tags({{"admin_level", "4"}}), false, 11, night).has_line,
              "region border only in the overview");
        check(!classify(tags({{"admin_level", "8"}, {"boundary", "administrative"}}), false, 8,
                        night).has_line, "no municipal borders");
    }
    check(classify(tags({{"natural", "glacier"}}), true, 6).has_area, "glacier");
    check(classify(tags({{"natural", "wetland"}}), true, 8).has_area, "wetland");
    check(classify(tags({{"landuse", "residential"}}), true, 8).has_area &&
              !classify(tags({{"landuse", "residential"}}), true, 13).has_area,
          "residential tint in the overview only");
    check(!classify(tags({{"natural", "coastline"}}), false, 6).has_line &&
              is_coastline(tags({{"natural", "coastline"}})) &&
              !is_coastline(tags({{"natural", "water"}})),
          "coastlines are not drawn as lines");
    check(!treat_as_area(tags({{"admin_level", "2"}}), false, true), "closed border stays a line");
    check(AreaLayer::Sea < AreaLayer::Land && AreaLayer::Land < AreaLayer::Landuse &&
              AreaLayer::Landuse < AreaLayer::Water,
          "sea below everything");
    check(place_label_rank("country", 5) == 0 && place_label_rank("country", 9) < 0,
          "countries at low zoom");
    check(place_label_rank("state", 6) >= 0 && place_label_rank("state", 10) < 0, "states");
    check(place_label_rank("city", 4) >= 0 && place_label_rank("city", 12) >= 0, "cities");
    check(place_label_rank("town", 6) < 0 && place_label_rank("town", 9) >= 0, "towns from z8");
    check(place_label_rank("country", 5) < place_label_rank("state", 5) &&
              place_label_rank("state", 5) < place_label_rank("city", 5) &&
              place_label_rank("city", 9) < place_label_rank("town", 9),
          "place ranks order");
    check(place_font("country", false).size > place_font("town", false).size, "country larger");

    // Levels: tunnels below ground, bridges above it, layer 2 above those.
    check(line_level(tags({{"highway", "primary"}})) == kGroundLevel, "ground level");
    check(line_level(tags({{"highway", "primary"}, {"tunnel", "yes"}})) == kTunnelLevel,
          "tunnel level");
    check(line_level(tags({{"highway", "service"}, {"tunnel", "building_passage"}})) ==
              kGroundLevel, "building passage stays on the ground");
    check(line_level(tags({{"highway", "primary"}, {"bridge", "yes"}})) == kBridgeLevel,
          "bridge level");
    check(line_level(tags({{"highway", "primary"}, {"bridge", "yes"}, {"layer", "2"}})) ==
              kUpperLevel, "layer 2");
    check(line_level(tags({{"highway", "primary"}, {"layer", "-1"}})) == kTunnelLevel,
          "layer -1 below ground");
    const auto bridge = classify(tags({{"highway", "residential"}, {"bridge", "yes"}}), false, 16);
    const auto street = classify(tags({{"highway", "residential"}}), false, 16);
    check(bridge.line.level == kBridgeLevel && bridge.line.casing_width > street.line.casing_width,
          "bridge outline wider");
    check(classify(tags({{"highway", "footway"}, {"bridge", "yes"}}), false, 16).line.casing_width > 0.0f,
          "footbridge outlined");
    check(classify(tags({{"man_made", "bridge"}}), true, 16).area.layer == AreaLayer::BridgeDeck,
          "bridge deck");
    {
        TileTessellator levels;
        LineStyle over = road;
        over.level = kBridgeLevel;
        levels.add_line({{10, 10}, {500, 10}}, over);
        levels.add_line({{10, 50}, {500, 50}}, road);
        const TileMesh m = levels.finish();
        check(m.line_level_start[kGroundLevel] == 0 && m.line_level_start[kBridgeLevel] == 6 &&
                  m.line_level_start[kLineLevels] == 12, "level ranges");
        check(m.line_vertices[m.line_indices[6]].y == 10.0f, "bridge after ground");
    }

    // One-way arrows: direction, spacing, only inside the tile.
    check(oneway_direction(tags({{"oneway", "yes"}})) == 1 &&
              oneway_direction(tags({{"oneway", "-1"}})) == -1 &&
              oneway_direction(tags({{"oneway", "no"}})) == 0 &&
              oneway_direction(tags({{"highway", "primary"}})) == 0, "oneway direction");
    {
        TileTessellator arrows;
        // 1200 units, spacing 200: arrows every 200 from 100 along the path,
        // at x = -150, 50, ..., 850; the one outside the tile is dropped.
        arrows.add_oneway_arrows({{-250, 20}, {950, 20}}, road, -1, {1, 2, 3, 255}, 200.0f);
        const TileMesh m = arrows.finish();
        // A shaft (4 vertices) and a head (3) each.
        check(m.sprite_vertices.size() == 5 * 7, "arrow count");
        check(m.sprite_vertices.front().dx < 0.0f && m.sprite_vertices.front().x == 50.0f,
              "arrow against the way");
        check(m.sprite_level_start[kGroundLevel + 1] == m.sprite_indices.size(),
              "arrows on the road's level");
    }
    check(marked_crossing(tags({{"highway", "crossing"}})) &&
              marked_crossing(tags({{"highway", "crossing"}, {"crossing", "traffic_signals"}})) &&
              !marked_crossing(tags({{"highway", "crossing"}, {"crossing", "unmarked"}})),
          "marked crossings");
    {
        TileTessellator zebra;
        zebra.add_zebra({100, 100}, {1, 0}, 8.0f, kBridgeLevel, {9, 9, 9, 255}, 1.45f);
        const TileMesh m = zebra.finish();
        check(m.sprite_vertices.size() == 5 * 4 && m.sprite_level_start[kBridgeLevel] == 0 &&
                  m.sprite_level_start[kBridgeLevel + 1] == 30, "zebra stripes");
        float v_min = 1e9f, v_max = -1e9f;
        for (const auto& v : m.sprite_vertices) {
            v_min = std::min(v_min, v.v);
            v_max = std::max(v_max, v.v);
        }
        check(v_max - v_min < 8.0f && v_max - v_min > 6.0f, "zebra inside the road");
    }

    // Traffic controls: icon only, not POIs, close in.
    const auto signals = classify_poi(tags({{"highway", "traffic_signals"}}), 16);
    check(signals.show && signals.control, "traffic signals");
    check(!classify_poi(tags({{"highway", "traffic_signals"}}), 15).show, "signals hidden at z15");
    check(classify_control(tags({{"highway", "stop"}}), 17).control, "stop sign");
    check(!classify_control(tags({{"amenity", "fuel"}}), 17).control, "fuel is no control");

    // Night palette: dark land, roads darker than by day, same layers.
    const auto luma = [](Rgba c) { return 0.3 * c.r + 0.59 * c.g + 0.11 * c.b; };
    check(luma(land_colour(true)) < 80.0 && luma(land_colour(false)) > 200.0, "night land dark");
    const auto day_road = classify(tags({{"highway", "primary"}}), false, 15, false);
    const auto night_road = classify(tags({{"highway", "primary"}}), false, 15, true);
    check(night_road.has_line && night_road.line.layer == day_road.line.layer &&
              night_road.line.width == day_road.line.width &&
              luma(night_road.line.fill) < luma(day_road.line.fill),
          "night road");
    check(luma(night_road.line.fill) > luma(land_colour(true)), "night road above land");

    if (failures == 0)
        std::puts("vector-test: ok");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
