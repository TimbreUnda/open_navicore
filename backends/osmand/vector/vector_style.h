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
    Landuse = 0,  // hospital/school grounds, residential tint
    Sand = 1,
    Forest = 2,
    Park = 3,
    Water = 4,
    Plaza = 5,
    Building = 6,
};

enum class LineLayer : std::uint8_t {
    BuildingOutline = 0,
    Waterway = 1,
    Path = 2,
    Rail = 3,
    Service = 4,
    Minor = 5,
    Tertiary = 6,
    Secondary = 7,
    Primary = 8,
    Trunk = 9,
    Motorway = 10,
};
constexpr int kAreaLayers = 7;
constexpr int kLineLayers = 11;

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
FeatureStyle classify(const Tags& tags, bool is_area, int zoom);

// Background (land) colour.
Rgba land_colour();

} // namespace navicore_vector
