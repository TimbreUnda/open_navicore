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

struct Palette {
    Rgba land, water, park, forest, sand, hospital, education, plaza, building,
        building_outline, road, road_casing, minor_casing, trunk, trunk_casing,
        motorway, motorway_casing, path, rail;
};

// Day: Google-Maps-like. Night: the dark navigation look (blue-grey land,
// dim roads, amber highways) so the screen does not glare after dusk.
constexpr Palette kDay = {
    rgb(0xF5F4F1), rgb(0xAAD3F7), rgb(0xC5E8C4), rgb(0xAEDCAE), rgb(0xF6EDCB),
    rgb(0xFBE5E3), rgb(0xF2ECDF), rgb(0xFCFBF9), rgb(0xE5E3DF), rgb(0xD3D0CB),
    rgb(0xFFFFFF), rgb(0xD5D2CC), rgb(0xDEDBD6), rgb(0xFFE7A1), rgb(0xF0C25C),
    rgb(0xFDD877), rgb(0xE9A93A), rgb(0xE1DED8), rgb(0xBDBCC2)};
constexpr Palette kNight = {
    rgb(0x1F2835), rgb(0x0E1A2B), rgb(0x22343A), rgb(0x1D3133), rgb(0x2B2C2A),
    rgb(0x30282F), rgb(0x2A2D33), rgb(0x2A3240), rgb(0x2A323F), rgb(0x343D4B),
    rgb(0x3F4A5A), rgb(0x18202B), rgb(0x1B232E), rgb(0x6A6150), rgb(0x2A2620),
    rgb(0x8A7247), rgb(0x3A2F1C), rgb(0x343C48), rgb(0x46505E)};

const Palette& palette(bool night)
{
    return night ? kNight : kDay;
}

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
bool road_style(const std::string& highway, int zoom, const Palette& p, LineStyle& out)
{
    std::string base = highway;
    bool link = false;
    if (base.size() > 5 && base.compare(base.size() - 5, 5, "_link") == 0) {
        base.resize(base.size() - 5);
        link = true;
    }
    if (base == "motorway")
        out = road(LineLayer::Motorway, p.motorway, p.motorway_casing, 9.0f, 2.0f);
    else if (base == "trunk")
        out = road(LineLayer::Trunk, p.trunk, p.trunk_casing, 8.5f, 2.0f);
    else if (base == "primary")
        out = road(LineLayer::Primary, p.road, p.road_casing, 8.0f, 2.0f);
    else if (base == "secondary")
        out = road(LineLayer::Secondary, p.road, p.road_casing, 7.0f, 2.0f);
    else if (base == "tertiary" && zoom >= 12)
        out = road(LineLayer::Tertiary, p.road, p.road_casing, 6.0f, 1.5f);
    else if ((base == "residential" || base == "unclassified" ||
              base == "living_street" || base == "road") && zoom >= 13)
        out = road(LineLayer::Minor, p.road, p.minor_casing, 5.0f, 1.5f);
    else if (base == "pedestrian" && zoom >= 14)
        out = road(LineLayer::Minor, p.plaza, p.minor_casing, 4.0f, 1.5f);
    else if ((base == "service" || base == "track") && zoom >= 15)
        out = road(LineLayer::Service, p.road, p.minor_casing, 3.0f, 1.0f);
    else if ((base == "footway" || base == "path" || base == "cycleway" ||
              base == "steps") && zoom >= 16)
        out = road(LineLayer::Path, p.path, kNone, 1.2f, 0.0f);
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

PoiStyle classify_poi(const Tags& tags, int zoom)
{
    struct Rule {
        const char* tag;
        const char* value;
        int rank;
        std::uint32_t colour;
        const char* icon;
        int min_zoom;
    };
    // Google-like category colours: car services blue, food orange,
    // shopping indigo, health red, lodging pink, sights teal, transit blue.
    static const Rule rules[] = {
        {"amenity", "fuel", 0, 0x1A73E8, "amenity_fuel", 14},
        {"amenity", "charging_station", 1, 0x1A73E8, "charging_station", 14},
        {"amenity", "hospital", 2, 0xD93025, "amenity_hospital", 14},
        {"amenity", "parking", 3, 0x5B6FD0, "amenity_parking", 16},
        {"railway", "station", 4, 0x1967D2, "railway_station", 14},
        {"railway", "subway_entrance", 6, 0x1967D2, "subway_entrance", 16},
        {"amenity", "police", 5, 0x3C4043, "amenity_police", 15},
        {"amenity", "pharmacy", 7, 0xD93025, "amenity_pharmacy", 16},
        {"amenity", "clinic", 7, 0xD93025, "clinic", 16},
        {"tourism", "museum", 8, 0x0F9D98, "tourism_museum", 15},
        {"tourism", "attraction", 8, 0x0F9D98, "attraction", 15},
        {"amenity", "theatre", 9, 0x0F9D98, "amenity_theatre", 16},
        {"amenity", "cinema", 9, 0x0F9D98, "amenity_cinema", 16},
        {"tourism", "hotel", 10, 0xC2185B, "tourism_hotel", 16},
        {"shop", "mall", 10, 0x3949AB, "shop_mall", 15},
        {"shop", "supermarket", 11, 0x3949AB, "shop_supermarket", 16},
        {"amenity", "marketplace", 11, 0x3949AB, "amenity_marketplace", 16},
        {"amenity", "restaurant", 12, 0xE8710A, "amenity_restaurant", 17},
        {"amenity", "cafe", 13, 0xE8710A, "amenity_cafe", 17},
        {"amenity", "fast_food", 13, 0xE8710A, "amenity_fast_food", 17},
        {"amenity", "bank", 14, 0x5F6368, "amenity_bank", 17},
    };
    PoiStyle style;
    for (const auto& rule : rules) {
        if (!is(find(tags, rule.tag), rule.value))
            continue;
        if (zoom < rule.min_zoom)
            return style;
        style.show = true;
        style.rank = rule.rank;
        style.colour = rgb(rule.colour);
        style.icon = rule.icon;
        style.min_zoom = rule.min_zoom;
        return style;
    }
    return style;
}

int street_label_rank(const std::string& highway)
{
    static const char* const order[] = {"motorway", "trunk", "primary",
                                        "secondary", "tertiary", "residential",
                                        "unclassified", "living_street",
                                        "pedestrian", "road"};
    for (int i = 0; i < static_cast<int>(sizeof(order) / sizeof(order[0])); ++i)
        if (highway == order[i])
            return i;
    return -1;
}

int place_label_rank(const std::string& place, int zoom)
{
    if (place == "city" && zoom <= 13) return 0;
    if (place == "borough" && zoom <= 14) return 1;
    if (place == "suburb" && zoom >= 12 && zoom <= 15) return 2;
    if (place == "quarter" && zoom >= 14 && zoom <= 16) return 3;
    if (place == "neighbourhood" && zoom >= 15) return 4;
    return -1;
}

Rgba land_colour(bool night)
{
    return palette(night).land;
}

FeatureStyle classify(const Tags& tags, bool is_area, int zoom, bool night)
{
    const Palette& p = palette(night);
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
            style.area = {AreaLayer::Building, p.building};
            if (zoom >= 16) {
                style.has_line = true;
                style.line = road(LineLayer::BuildingOutline, p.building_outline,
                                  kNone, 1.0f, 0.0f);
            }
        } else if (is(natural, "water") || is(waterway, "riverbank") ||
                   any_of(landuse, {"reservoir", "basin"})) {
            style.has_area = true;
            style.area = {AreaLayer::Water, p.water};
        } else if (any_of(landuse, {"forest"}) || is(natural, "wood")) {
            style.has_area = true;
            style.area = {AreaLayer::Forest, p.forest};
        } else if (any_of(leisure, {"park", "garden", "nature_reserve",
                                    "recreation_ground", "pitch",
                                    "golf_course"}) ||
                   any_of(landuse, {"grass", "meadow", "recreation_ground",
                                    "village_green", "cemetery", "allotments"}) ||
                   any_of(natural, {"grassland", "scrub", "heath"})) {
            style.has_area = true;
            style.area = {AreaLayer::Park, p.park};
        } else if (any_of(natural, {"beach", "sand"})) {
            style.has_area = true;
            style.area = {AreaLayer::Sand, p.sand};
        } else if (any_of(amenity, {"hospital", "clinic"})) {
            style.has_area = true;
            style.area = {AreaLayer::Landuse, p.hospital};
        } else if (any_of(amenity, {"school", "university", "college",
                                    "kindergarten"})) {
            style.has_area = true;
            style.area = {AreaLayer::Landuse, p.education};
        } else if (highway && zoom >= 15) {
            style.has_area = true;
            style.area = {AreaLayer::Plaza, p.plaza};
        }
        return style;
    }

    if (highway) {
        if (road_style(*highway, zoom, p, style.line)) {
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
        style.line = road(LineLayer::Rail, p.rail, kNone, 1.6f, 0.0f);
        return style;
    }
    if (is(railway, "tram") && zoom >= 15) {
        style.has_line = true;
        style.line = road(LineLayer::Rail, p.rail, kNone, 1.0f, 0.0f);
        return style;
    }
    if (any_of(waterway, {"river", "canal"})) {
        style.has_line = true;
        style.line = road(LineLayer::Waterway, p.water, kNone, 5.0f, 0.0f);
        return style;
    }
    if (any_of(waterway, {"stream", "ditch"}) && zoom >= 15) {
        style.has_line = true;
        style.line = road(LineLayer::Waterway, p.water, kNone, 1.5f, 0.0f);
    }
    return style;
}

} // namespace navicore_vector
