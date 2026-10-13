#include "vector_style.h"

#include <cstring>

namespace navicore_vector {
namespace {

constexpr Rgba rgb(std::uint32_t hex, std::uint8_t a = 255)
{
    return {static_cast<std::uint8_t>(hex >> 16),
            static_cast<std::uint8_t>(hex >> 8), static_cast<std::uint8_t>(hex),
            a};
}

constexpr Rgba kLand = rgb(0xF5F4F1);
constexpr Rgba kWater = rgb(0xAAD3F7);
constexpr Rgba kPark = rgb(0xC5E8C4);
constexpr Rgba kForest = rgb(0xAEDCAE);
constexpr Rgba kSand = rgb(0xF6EDCB);
constexpr Rgba kHospital = rgb(0xFBE5E3);
constexpr Rgba kEducation = rgb(0xF2ECDF);
constexpr Rgba kPlaza = rgb(0xFCFBF9);
constexpr Rgba kBuilding = rgb(0xE5E3DF);
constexpr Rgba kBuildingOutline = rgb(0xD3D0CB);
constexpr Rgba kRoad = rgb(0xFFFFFF);
constexpr Rgba kRoadCasing = rgb(0xD5D2CC);
constexpr Rgba kMinorCasing = rgb(0xDEDBD6);
constexpr Rgba kTrunk = rgb(0xFFE7A1);
constexpr Rgba kTrunkCasing = rgb(0xF0C25C);
constexpr Rgba kMotorway = rgb(0xFDD877);
constexpr Rgba kMotorwayCasing = rgb(0xE9A93A);
constexpr Rgba kPath = rgb(0xE1DED8);
constexpr Rgba kRail = rgb(0xBDBCC2);
constexpr Rgba kNone{};

const std::string* find(const Tags& tags, const char* tag)
{
    for (const auto& entry : tags)
        if (entry.first == tag)
            return &entry.second;
    return nullptr;
}

bool is(const std::string* value, const char* expected)
{
    return value && *value == expected;
}

bool any_of(const std::string* value, std::initializer_list<const char*> list)
{
    if (!value)
        return false;
    for (const char* item : list)
        if (*value == item)
            return true;
    return false;
}

LineStyle road(LineLayer layer, Rgba fill, Rgba casing, float width,
               float casing_extra)
{
    return {layer, fill, casing, width,
            casing.a ? width + casing_extra : 0.0f};
}

// Road style by highway class; *_link roads are drawn like their class but
// narrower.
bool road_style(const std::string& highway, int zoom, LineStyle& out)
{
    std::string base = highway;
    bool link = false;
    if (base.size() > 5 && base.compare(base.size() - 5, 5, "_link") == 0) {
        base.resize(base.size() - 5);
        link = true;
    }
    if (base == "motorway")
        out = road(LineLayer::Motorway, kMotorway, kMotorwayCasing, 9.0f, 2.0f);
    else if (base == "trunk")
        out = road(LineLayer::Trunk, kTrunk, kTrunkCasing, 8.5f, 2.0f);
    else if (base == "primary")
        out = road(LineLayer::Primary, kRoad, kRoadCasing, 8.0f, 2.0f);
    else if (base == "secondary")
        out = road(LineLayer::Secondary, kRoad, kRoadCasing, 7.0f, 2.0f);
    else if (base == "tertiary" && zoom >= 12)
        out = road(LineLayer::Tertiary, kRoad, kRoadCasing, 6.0f, 1.5f);
    else if ((base == "residential" || base == "unclassified" ||
              base == "living_street" || base == "road") && zoom >= 13)
        out = road(LineLayer::Minor, kRoad, kMinorCasing, 5.0f, 1.5f);
    else if (base == "pedestrian" && zoom >= 14)
        out = road(LineLayer::Minor, kPlaza, kMinorCasing, 4.0f, 1.5f);
    else if ((base == "service" || base == "track") && zoom >= 15)
        out = road(LineLayer::Service, kRoad, kMinorCasing, 3.0f, 1.0f);
    else if ((base == "footway" || base == "path" || base == "cycleway" ||
              base == "steps") && zoom >= 16)
        out = road(LineLayer::Path, kPath, kNone, 1.2f, 0.0f);
    else
        return false;
    if (link) {
        out.width *= 0.7f;
        if (out.casing_width > 0.0f)
            out.casing_width = out.width + 1.5f;
    }
    return true;
}

} // namespace

bool treat_as_area(const Tags& tags, bool is_area_flag, bool closed)
{
    if (is_area_flag)
        return true;
    if (!closed)
        return false;
    if (find(tags, "highway"))
        return is(find(tags, "area"), "yes");
    for (const char* linear : {"railway", "barrier", "waterway", "power",
                               "route", "natural=coastline"})
        if (find(tags, linear))
            return false;
    return true;
}

Rgba land_colour()
{
    return kLand;
}

FeatureStyle classify(const Tags& tags, bool is_area, int zoom)
{
    FeatureStyle style;
    const auto* highway = find(tags, "highway");
    const auto* building = find(tags, "building");
    const auto* natural = find(tags, "natural");
    const auto* landuse = find(tags, "landuse");
    const auto* leisure = find(tags, "leisure");
    const auto* waterway = find(tags, "waterway");
    const auto* amenity = find(tags, "amenity");
    const auto* railway = find(tags, "railway");

    if (is_area) {
        if (building && zoom >= 15) {
            style.has_area = true;
            style.area = {AreaLayer::Building, kBuilding};
            if (zoom >= 16) {
                style.has_line = true;
                style.line = road(LineLayer::BuildingOutline, kBuildingOutline,
                                  kNone, 1.0f, 0.0f);
            }
        } else if (is(natural, "water") || is(waterway, "riverbank") ||
                   any_of(landuse, {"reservoir", "basin"})) {
            style.has_area = true;
            style.area = {AreaLayer::Water, kWater};
        } else if (any_of(landuse, {"forest"}) || is(natural, "wood")) {
            style.has_area = true;
            style.area = {AreaLayer::Forest, kForest};
        } else if (any_of(leisure, {"park", "garden", "nature_reserve",
                                    "recreation_ground", "pitch",
                                    "golf_course"}) ||
                   any_of(landuse, {"grass", "meadow", "recreation_ground",
                                    "village_green", "cemetery", "allotments"}) ||
                   any_of(natural, {"grassland", "scrub", "heath"})) {
            style.has_area = true;
            style.area = {AreaLayer::Park, kPark};
        } else if (any_of(natural, {"beach", "sand"})) {
            style.has_area = true;
            style.area = {AreaLayer::Sand, kSand};
        } else if (any_of(amenity, {"hospital", "clinic"})) {
            style.has_area = true;
            style.area = {AreaLayer::Landuse, kHospital};
        } else if (any_of(amenity, {"school", "university", "college",
                                    "kindergarten"})) {
            style.has_area = true;
            style.area = {AreaLayer::Landuse, kEducation};
        } else if (highway && zoom >= 15) {
            style.has_area = true;
            style.area = {AreaLayer::Plaza, kPlaza};
        }
        return style;
    }

    if (highway) {
        if (road_style(*highway, zoom, style.line)) {
            style.has_line = true;
            if (is(find(tags, "tunnel"), "yes")) {
                // Tunnels: faded, no outline.
                style.line.fill.a = 110;
                style.line.casing = kNone;
                style.line.casing_width = 0.0f;
            }
        }
        return style;
    }
    if (any_of(railway, {"rail", "light_rail", "narrow_gauge"}) && zoom >= 13 &&
        !is(find(tags, "tunnel"), "yes")) {
        style.has_line = true;
        style.line = road(LineLayer::Rail, kRail, kNone, 1.6f, 0.0f);
        return style;
    }
    if (is(railway, "tram") && zoom >= 15) {
        style.has_line = true;
        style.line = road(LineLayer::Rail, kRail, kNone, 1.0f, 0.0f);
        return style;
    }
    if (any_of(waterway, {"river", "canal"})) {
        style.has_line = true;
        style.line = road(LineLayer::Waterway, kWater, kNone, 5.0f, 0.0f);
        return style;
    }
    if (any_of(waterway, {"stream", "ditch"}) && zoom >= 15) {
        style.has_line = true;
        style.line = road(LineLayer::Waterway, kWater, kNone, 1.5f, 0.0f);
    }
    return style;
}

} // namespace navicore_vector
