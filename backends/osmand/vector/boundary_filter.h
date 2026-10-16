#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace navicore_vector {

// Crimea is part of Ukraine. OSM draws it as a country of its own (a de facto
// admin_level=2 boundary along the whole coast and across Perekop), and the
// world basemap and the regional files carry no tag that tells that line from
// a real border, so country borders are filtered by place: segments inside
// the box below (the peninsula and its waters, ending west of the Kerch
// strait so the Ukraine-Russia border through the strait is kept) are not
// drawn. Region borders (admin_level 3-4) are untouched.
struct Box31 {
    double x0, y0, x1, y1;  // x: west to east, y: north to south (31-bit Mercator)

    bool contains(double x, double y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
};

inline double lon_to_x31(double lon_degrees)
{
    return (lon_degrees + 180.0) / 360.0 * 2147483648.0;
}

inline double lat_to_y31(double lat_degrees)
{
    const double pi = 3.14159265358979323846;
    const double phi = lat_degrees * pi / 180.0;
    return (0.5 - std::log(std::tan(pi / 4.0 + phi / 2.0)) / (2.0 * pi)) * 2147483648.0;
}

inline const Box31& crimea_box()
{
    static const Box31 box{lon_to_x31(31.9), lat_to_y31(46.3), lon_to_x31(36.62), lat_to_y31(43.9)};
    return box;
}

// OSM also carries Crimea as the Russian "Республика Крым" (name:uk
// "Республіка Крим", name:en "Republic of Crimea"), next to the Ukrainian
// "Автономна Республіка Крим": a second place label and a second region
// border. The Russian-entity object is not drawn at all.
inline bool is_russian_crimea_entity(const std::string& native, const std::string& uk, const std::string& en)
{
    return native == "Республика Крым" || uk == "Республіка Крим" || en == "Republic of Crimea";
}

// Splits a polyline (31-bit points, any type with x and y members) into the
// runs whose segments lie outside the box; a segment is dropped when its
// midpoint is inside. Runs with fewer than two points are discarded.
template <typename Point>
std::vector<std::vector<Point>> split_outside_box(const Box31& box, const std::vector<Point>& points)
{
    std::vector<std::vector<Point>> runs;
    std::vector<Point> run;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const double mx = (static_cast<double>(points[i].x) + static_cast<double>(points[i + 1].x)) / 2.0;
        const double my = (static_cast<double>(points[i].y) + static_cast<double>(points[i + 1].y)) / 2.0;
        if (box.contains(mx, my)) {
            if (run.size() >= 2)
                runs.push_back(run);
            run.clear();
            continue;
        }
        if (run.empty())
            run.push_back(points[i]);
        run.push_back(points[i + 1]);
    }
    if (run.size() >= 2)
        runs.push_back(run);
    return runs;
}

} // namespace navicore_vector
