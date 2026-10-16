#include "vector_style.h"

#include <cmath>
#include <cstdlib>
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
        motorway, motorway_casing, path, rail, bridge_casing, bridge_deck, arrow,
        zebra;
};

// Day: Google-Maps-like. Night: the dark navigation look (blue-grey land,
// dim roads, amber highways) so the screen does not glare after dusk.
constexpr Palette kDay = {
    rgb(0xF5F4F1), rgb(0xAAD3F7), rgb(0xC5E8C4), rgb(0xAEDCAE), rgb(0xF6EDCB),
    rgb(0xFBE5E3), rgb(0xF2ECDF), rgb(0xFCFBF9), rgb(0xE5E3DF), rgb(0xD3D0CB),
    rgb(0xFFFFFF), rgb(0xD5D2CC), rgb(0xDEDBD6), rgb(0xFFE7A1), rgb(0xF0C25C),
    rgb(0xFDD877), rgb(0xE9A93A), rgb(0xE1DED8), rgb(0xBDBCC2), rgb(0xA9A49B),
    rgb(0xE8E6E1), rgb(0x8C9199), rgb(0xB9BDC4)};
constexpr Palette kNight = {
    rgb(0x1F2835), rgb(0x0E1A2B), rgb(0x22343A), rgb(0x1D3133), rgb(0x2B2C2A),
    rgb(0x30282F), rgb(0x2A2D33), rgb(0x2A3240), rgb(0x2A323F), rgb(0x343D4B),
    rgb(0x3F4A5A), rgb(0x18202B), rgb(0x1B232E), rgb(0x6A6150), rgb(0x2A2620),
    rgb(0x8A7247), rgb(0x3A2F1C), rgb(0x343C48), rgb(0x46505E), rgb(0x0B1017),
    rgb(0x2E3644), rgb(0x8592A3), rgb(0x65707F)};

const Palette& palette(bool night)
{
    return night ? kNight : kDay;
}

// Overview colours (zoom 4-10, from the world basemap): roads are one or two
// pixels wide there, so they take the darker outline tone instead of white,
// which would vanish on the land colour.
struct OverviewPalette {
    Rgba wetland, glacier, urban, border_country, border_region, motorway, trunk,
        primary, secondary, tertiary, rail;
};
constexpr OverviewPalette kDayOverview = {
    rgb(0xCDE6DA), rgb(0xEAF2F8), rgb(0xE9E6E0), rgb(0x8F82B0), rgb(0xB4A9CC),
    rgb(0xE0952A), rgb(0xE9B23E), rgb(0xE3BE72), rgb(0xCFC8B8), rgb(0xD9D4C8),
    rgb(0xB4B3BA)};
constexpr OverviewPalette kNightOverview = {
    rgb(0x1E3338), rgb(0x3A4757), rgb(0x252E3B), rgb(0x7886A3), rgb(0x56637B),
    rgb(0xB48A3C), rgb(0x9A7D45), rgb(0x7D7050), rgb(0x586270), rgb(0x485262),
    rgb(0x4E5868)};

const OverviewPalette& overview(bool night)
{
    return night ? kNightOverview : kDayOverview;
}

// Line widths are pixels at zoom 16 and the renderer scales them by
// 2^((zoom - 16) / 2) below that, never under one pixel: the width that
// shows as `px` on screen at `zoom`.
float screen_width(float px, int zoom)
{
    return px / std::exp2((zoom - 16) * 0.5f);
}

// Tile zoom up to which the overview styling applies (the basemap's own
// levels; the regional files start at 11).
constexpr int kOverviewMaxZoom = 10;

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

// Overview roads (zoom <= kOverviewMaxZoom): thin coloured lines by class,
// the minor classes appearing as the basemap gets denser (it holds primary
// and up from level 6, secondary from 8, tertiary from 10).
bool overview_road_style(const std::string& base, int zoom, bool night, LineStyle& out)
{
    const OverviewPalette& o = overview(night);
    const bool near = zoom >= 8;
    if (base == "motorway")
        out = road(LineLayer::Motorway, o.motorway, kNone,
                   screen_width(near ? 2.4f : 1.8f, zoom), 0.0f);
    else if (base == "trunk")
        out = road(LineLayer::Trunk, o.trunk, kNone,
                   screen_width(near ? 2.1f : 1.6f, zoom), 0.0f);
    else if (base == "primary")
        out = road(LineLayer::Primary, o.primary, kNone,
                   screen_width(near ? 1.8f : 1.4f, zoom), 0.0f);
    else if (base == "secondary" && zoom >= 8)
        out = road(LineLayer::Secondary, o.secondary, kNone, screen_width(1.4f, zoom), 0.0f);
    else if (base == "tertiary" && zoom >= 10)
        out = road(LineLayer::Tertiary, o.tertiary, kNone, screen_width(1.1f, zoom), 0.0f);
    else
        return false;
    return true;
}

// Country (admin_level 2) and region (3-4) borders; the finer boundaries of
// the regional files are not drawn.
bool boundary_style(const std::string& admin_level, int zoom, bool night, LineStyle& out)
{
    const OverviewPalette& o = overview(night);
    if (admin_level == "2" && zoom <= 12)
        out = road(LineLayer::Boundary, o.border_country, kNone,
                   screen_width(zoom >= 7 ? 1.6f : 1.3f, zoom), 0.0f);
    else if ((admin_level == "3" || admin_level == "4") && zoom <= kOverviewMaxZoom)
        out = road(LineLayer::Boundary, o.border_region, kNone, screen_width(1.1f, zoom), 0.0f);
    else
        return false;
    return true;
}

// Road style by highway class; *_link roads are drawn like their class but
// narrower.
bool road_style(const std::string& highway, int zoom, bool night, const Palette& p,
                LineStyle& out)
{
    std::string base = highway;
    bool link = false;
    if (base.size() > 5 && base.compare(base.size() - 5, 5, "_link") == 0) {
        base.resize(base.size() - 5);
        link = true;
    }
    if (zoom <= kOverviewMaxZoom)
        return !link && overview_road_style(base, zoom, night, out);
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
                               "route", "admin_level"})
        if (find(tags, linear))
            return false;
    return true;
}

PoiStyle classify_control(const Tags& tags, int zoom)
{
    struct Rule {
        const char* tag;
        const char* value;
        int rank;
        const char* icon;
        int min_zoom;
    };
    // Close in only; ranked among themselves (they have their own
    // placement budget).
    static const Rule rules[] = {
        {"highway", "traffic_signals", 3, "c_highway_traffic_signals_map", 16},
        {"railway", "level_crossing", 4, "c_level_crossing_map", 15},
        {"highway", "stop", 5, "c_stop", 16},
        {"highway", "give_way", 5, "c_give_way", 16},
    };
    PoiStyle style;
    for (const auto& rule : rules) {
        if (!is(find(tags, rule.tag), rule.value))
            continue;
        // Matched even when hidden at this zoom: not also a POI.
        style.control = true;
        style.show = zoom >= rule.min_zoom;
        style.rank = rule.rank;
        style.icon = rule.icon;
        style.min_zoom = rule.min_zoom;
        return style;
    }
    return style;
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
    // Ranked for a driver (lower first when POIs compete for space): car
    // essentials, then what a trip needs (toilets, sleep, food, cash), then
    // sights. Colours Google-like: car services blue, auto shops blue-grey,
    // health and hazards red, lodging pink, food orange, shopping indigo,
    // transit blue, sights teal.
    static const Rule rules[] = {
        // Car essentials.
        {"amenity", "fuel", 0, 0x1A73E8, "amenity_fuel", 13},
        {"amenity", "charging_station", 1, 0x1A73E8, "charging_station", 14},
        {"highway", "services", 1, 0x1A73E8, "highway_services", 13},
        {"highway", "rest_area", 2, 0x1A73E8, "rest_area", 13},
        {"amenity", "hospital", 2, 0xD93025, "amenity_hospital", 14},
        {"highway", "speed_camera", 3, 0xD93025, "highway_speed_camera", 15},
        {"amenity", "parking", 4, 0x5B6FD0, "amenity_parking", 15},
        {"amenity", "parking_entrance", 4, 0x5B6FD0, "amenity_parking_entrance", 16},
        {"shop", "car_repair", 5, 0x546E7A, "shop_car_repair", 15},
        {"shop", "tyres", 5, 0x546E7A, "shop_tyres", 15},
        {"amenity", "car_wash", 6, 0x546E7A, "amenity_car_wash", 15},
        {"amenity", "compressed_air", 7, 0x546E7A, "compressed_air", 16},
        {"amenity", "vehicle_inspection", 7, 0x546E7A, "vehicle_inspection", 16},
        {"shop", "car_parts", 8, 0x546E7A, "shop_car_parts", 16},
        {"shop", "car", 9, 0x546E7A, "shop_car", 16},
        {"amenity", "police", 6, 0x3C4043, "amenity_police", 15},
        {"amenity", "pharmacy", 7, 0xD93025, "amenity_pharmacy", 15},
        {"aeroway", "aerodrome", 3, 0x1967D2, "aeroway_aerodrome", 11},
        {"railway", "station", 5, 0x1967D2, "railway_station", 14},
        {"amenity", "ferry_terminal", 6, 0x1967D2, "amenity_ferry_terminal", 14},
        // What a trip needs.
        {"amenity", "toilets", 8, 0x5F6368, "amenity_toilets", 16},
        {"tourism", "motel", 9, 0xC2185B, "tourism_motel", 14},
        {"tourism", "hotel", 10, 0xC2185B, "tourism_hotel", 15},
        {"tourism", "guest_house", 12, 0xC2185B, "tourism_guest_house", 16},
        {"tourism", "camp_site", 12, 0x0F9D58, "tourism_camp_site", 15},
        {"shop", "supermarket", 10, 0x3949AB, "shop_supermarket", 15},
        {"shop", "convenience", 12, 0x3949AB, "shop_convenience", 16},
        {"shop", "mall", 11, 0x3949AB, "shop_mall", 15},
        {"amenity", "atm", 11, 0x5F6368, "amenity_atm", 16},
        {"amenity", "bank", 14, 0x5F6368, "amenity_bank", 17},
        {"amenity", "fast_food", 11, 0xE8710A, "amenity_fast_food", 16},
        {"amenity", "restaurant", 12, 0xE8710A, "amenity_restaurant", 16},
        {"amenity", "cafe", 13, 0xE8710A, "amenity_cafe", 16},
        {"amenity", "clinic", 9, 0xD93025, "clinic", 16},
        {"amenity", "doctors", 13, 0xD93025, "amenity_doctors", 17},
        {"amenity", "bus_station", 12, 0x1967D2, "amenity_bus_station", 15},
        {"railway", "subway_entrance", 12, 0x1967D2, "subway_entrance", 16},
        {"amenity", "marketplace", 13, 0x3949AB, "amenity_marketplace", 16},
        // Sights and culture.
        {"tourism", "museum", 14, 0x0F9D98, "tourism_museum", 15},
        {"tourism", "attraction", 14, 0x0F9D98, "attraction", 15},
        {"amenity", "theatre", 15, 0x0F9D98, "amenity_theatre", 16},
        {"amenity", "cinema", 15, 0x0F9D98, "amenity_cinema", 16},
    };
    PoiStyle style = classify_control(tags, zoom);
    if (style.control)
        return style;
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
    // Basemap overview names: countries, then regions, cities, towns.
    if (place == "country" && zoom >= 3 && zoom <= 7) return 0;
    if (place == "state" && zoom >= 3 && zoom <= 8) return 1;
    if (place == "city" && zoom >= 3 && zoom <= 13) return 2;
    if (place == "town" && zoom >= 8 && zoom <= 12) return 3;
    if (place == "borough" && zoom <= 14) return 4;
    if (place == "suburb" && zoom >= 12 && zoom <= 15) return 5;
    if (place == "quarter" && zoom >= 14 && zoom <= 16) return 6;
    if (place == "neighbourhood" && zoom >= 15) return 7;
    if (place == "ocean" && zoom >= 3 && zoom <= 6) return 8;
    if (place == "sea" && zoom >= 4 && zoom <= 8) return 9;
    return -1;
}

PlaceFont place_font(const std::string& place, bool night)
{
    if (place == "ocean" || place == "sea")
        return {17.0f, night ? 0xFF6F8FB5u : 0xFF4F7FB0u};
    if (place == "country")
        return {22.0f, night ? 0xFFB4BCC8u : 0xFF3C4043u};
    if (place == "state")
        return {18.0f, night ? 0xFF8F99A6u : 0xFF80868Bu};
    if (place == "town")
        return {17.0f, night ? 0xFF9AA3AEu : 0xFF70757Au};
    return {20.0f, night ? 0xFF9AA3AEu : 0xFF70757Au};
}

bool is_coastline(const Tags& tags)
{
    for (const auto& entry : tags)
        if (entry.first == "natural" && entry.second == "coastline")
            return true;
    return false;
}

Rgba sea_colour(bool night)
{
    return palette(night).water;
}

Rgba island_colour(bool night)
{
    return palette(night).land;
}

std::uint8_t line_level(const Tags& tags)
{
    const auto* bridge = find(tags, "bridge");
    const auto* tunnel = find(tags, "tunnel");
    const auto* layer_tag = find(tags, "layer");
    const int layer = layer_tag ? std::atoi(layer_tag->c_str()) : 0;
    // A building passage is a short gap under a house, not a tunnel.
    if ((tunnel && !is(tunnel, "no") && !is(tunnel, "building_passage")) || layer < 0)
        return kTunnelLevel;
    if ((bridge && !is(bridge, "no")) || layer > 0)
        return layer >= 2 ? kUpperLevel : kBridgeLevel;
    return kGroundLevel;
}

int oneway_direction(const Tags& tags)
{
    const auto* oneway = find(tags, "oneway");
    if (any_of(oneway, {"yes", "true", "1"}))
        return 1;
    if (any_of(oneway, {"-1", "reverse"}))
        return -1;
    return 0;
}

bool marked_crossing(const Tags& tags)
{
    return is(find(tags, "highway"), "crossing") &&
           !any_of(find(tags, "crossing"), {"unmarked", "no"});
}

Rgba oneway_arrow_colour(LineLayer layer, bool night)
{
    const Palette& p = palette(night);
    // Yellow highways take their own outline colour; white roads grey.
    if (layer == LineLayer::Motorway)
        return p.motorway_casing;
    if (layer == LineLayer::Trunk)
        return p.trunk_casing;
    return p.arrow;
}

Rgba zebra_colour(bool night)
{
    return palette(night).zebra;
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
        } else if (is(natural, "desert") && zoom <= 13) {
            style.has_area = true;
            style.area = {AreaLayer::Sand, p.sand};
        } else if (is(natural, "glacier") && zoom <= 13) {
            style.has_area = true;
            style.area = {AreaLayer::Sand, overview(night).glacier};
        } else if (is(natural, "wetland") && zoom <= 13) {
            style.has_area = true;
            style.area = {AreaLayer::Forest, overview(night).wetland};
        } else if (is(landuse, "residential") && zoom <= kOverviewMaxZoom) {
            // Built-up tint on the overview maps only.
            style.has_area = true;
            style.area = {AreaLayer::Landuse, overview(night).urban};
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
        } else if (is(find(tags, "man_made"), "bridge") && zoom >= 14) {
            style.has_area = true;
            style.area = {AreaLayer::BridgeDeck, p.bridge_deck};
        }
        return style;
    }

    const std::uint8_t level = line_level(tags);
    const auto on_bridge = [&](LineStyle& line) {
        line.level = level;
        if (level < kBridgeLevel || zoom <= kOverviewMaxZoom)
            return;
        // Bridges: a darker, wider outline, also for paths and rails.
        line.casing = p.bridge_casing;
        line.casing_width = line.width + (line.width >= 4.0f ? 3.0f : 2.0f);
    };

    if (highway) {
        if (road_style(*highway, zoom, night, p, style.line)) {
            style.has_line = true;
            if (is(find(tags, "tunnel"), "yes")) {
                // Tunnels: faded, no outline.
                style.line.fill.a = 110;
                style.line.casing = kNone;
                style.line.casing_width = 0.0f;
            }
            on_bridge(style.line);
        }
        return style;
    }
    if (const auto* level_tag = find(tags, "admin_level")) {
        if (boundary_style(*level_tag, zoom, night, style.line)) {
            style.has_line = true;
            return style;
        }
    }
    if (is(railway, "rail") && zoom >= kOverviewMaxZoom && zoom < 13 &&
        !is(find(tags, "tunnel"), "yes")) {
        style.has_line = true;
        style.line = road(LineLayer::Rail, overview(night).rail, kNone,
                          screen_width(1.0f, zoom), 0.0f);
        return style;
    }
    if (any_of(railway, {"rail", "light_rail", "narrow_gauge"}) && zoom >= 13 &&
        !is(find(tags, "tunnel"), "yes")) {
        style.has_line = true;
        style.line = road(LineLayer::Rail, p.rail, kNone, 1.6f, 0.0f);
        on_bridge(style.line);
        return style;
    }
    if (is(railway, "tram") && zoom >= 15) {
        style.has_line = true;
        style.line = road(LineLayer::Rail, p.rail, kNone, 1.0f, 0.0f);
        on_bridge(style.line);
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
