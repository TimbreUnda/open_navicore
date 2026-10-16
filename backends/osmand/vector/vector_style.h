#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace navicore_vector {

struct Rgba {
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
};

// Draw order inside a tile: lower first. Areas and lines have separate
// orders because all areas are drawn before any line.
enum class AreaLayer : std::uint8_t {
    Sea = 0,      // sea built from coastlines (below everything)
    Land = 1,     // islands inside that sea
    Landuse = 2,  // hospital/school grounds, residential tint
    Sand = 3,
    Forest = 4,
    Park = 5,
    Water = 6,
    Plaza = 7,
    Building = 8,
    BridgeDeck = 9,  // man_made=bridge outlines, above water
};

enum class LineLayer : std::uint8_t {
    BuildingOutline = 0,
    Waterway = 1,
    Boundary = 2,  // country and region borders
    Path = 3,
    Rail = 4,
    Service = 5,
    Minor = 6,
    Tertiary = 7,
    Secondary = 8,
    Primary = 9,
    Trunk = 10,
    Motorway = 11,
};
constexpr int kAreaLayers = 10;
constexpr int kLineLayers = 12;

// Vertical levels from bridge/tunnel/layer tags. Each level is drawn whole
// (casings, fills, then its arrows and markings) before the next, so a
// bridge covers the roads beneath it.
constexpr int kLineLevels = 4;
constexpr std::uint8_t kTunnelLevel = 0;  // tunnels and layer < 0
constexpr std::uint8_t kGroundLevel = 1;
constexpr std::uint8_t kBridgeLevel = 2;  // bridges and layer 1
constexpr std::uint8_t kUpperLevel = 3;   // layer 2 and above

struct AreaStyle {
    AreaLayer layer = AreaLayer::Landuse;
    Rgba fill;
};

// Widths are screen pixels at zoom 16; the renderer scales them with zoom.
// A zero-alpha casing is not drawn.
struct LineStyle {
    LineLayer layer = LineLayer::Minor;
    Rgba fill;
    Rgba casing;
    float width = 0.0f;
    float casing_width = 0.0f;
    std::uint8_t level = kGroundLevel;
};

struct FeatureStyle {
    bool has_area = false;
    AreaStyle area;
    bool has_line = false;  // a road, or an area's outline
    LineStyle line;
};

using Tags = std::vector<std::pair<std::string, std::string>>;

// OBF data flags many closed polygons (buildings, grass, ...) as lines.
// A closed shape is an area unless it is a linear feature drawn as a ring
// (a roundabout, a fence, a rail loop); highways need area=yes.
bool treat_as_area(const Tags& tags, bool is_area_flag, bool closed);

// Google-Maps-like automotive palette. Returns nothing (both flags false)
// for features the vector map does not draw at this zoom.
FeatureStyle classify(const Tags& tags, bool is_area, int zoom, bool night = false);

// Point of interest, Google style: a coloured disc with a white pictogram
// and the name in the category colour. Lower rank = more important; car
// services come first. Churches, libraries, benches etc. are not shown.
struct PoiStyle {
    bool show = false;
    int rank = 0;
    Rgba colour;
    const char* icon = "";  // OsmAnd map icon name, e.g. "amenity_fuel"
    int min_zoom = 15;
    // Traffic control on the road (signals, stop, give way, level
    // crossing): OsmAnd's own colour icon, no disc and no name.
    bool control = false;
};
PoiStyle classify_poi(const Tags& tags, int zoom);
// Only the traffic controls (classify_poi checks them first).
PoiStyle classify_control(const Tags& tags, int zoom);

// Level of a line feature (kTunnelLevel..kUpperLevel).
std::uint8_t line_level(const Tags& tags);

// Driving direction of a one-way road: 1 along the way, -1 against it, 0
// for two-way roads.
int oneway_direction(const Tags& tags);

// A pedestrian crossing node drawn as a zebra (highway=crossing, unless
// tagged unmarked).
bool marked_crossing(const Tags& tags);

// Colours of the one-way arrows on a road of `layer` and of zebra stripes.
Rgba oneway_arrow_colour(LineLayer layer, bool night);
Rgba zebra_colour(bool night);

// Street-name priority by highway class (lower = more important); -1 for
// roads whose names are not shown (paths, service roads).
int street_label_rank(const std::string& highway);

// Place names (country, state, city, town, suburb, ...): rank (lower = more
// important) at a zoom; rank -1 when not shown.
int place_label_rank(const std::string& place, int zoom);

// Fill of the sea and of islands cut out of it (sea built from coastlines
// or from an all-water basemap tile).
Rgba sea_colour(bool night = false);
Rgba island_colour(bool night = false);

// Text size (px) and ARGB colour of a place name by class (country, state,
// city, town, ...).
struct PlaceFont {
    float size;
    std::uint32_t argb;
};
PlaceFont place_font(const std::string& place, bool night);

// True for natural=coastline (sea/land outlines the tile build turns into
// polygons; nothing is drawn from the line itself).
bool is_coastline(const Tags& tags);

// Background (land) colour.
Rgba land_colour(bool night = false);

} // namespace navicore_vector
