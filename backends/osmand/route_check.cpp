#include "route_check.h"

#include <algorithm>
#include <cmath>

namespace navicore_osmand {

namespace {

double metres_between(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double kDegrees = 3.14159265358979323846 / 180.0;
    const double mean = (lat1 + lat2) * 0.5 * kDegrees;
    const double dy = (lat2 - lat1) * 111320.0;
    const double dx = (lon2 - lon1) * 111320.0 * std::cos(mean);
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

RouteExtentCheck check_route_extent(ui::GeoPoint origin, ui::GeoPoint destination,
                                   ui::GeoPoint start, ui::GeoPoint end, double length_metres,
                                   double max_start_gap, double max_end_gap)
{
    RouteExtentCheck check;
    check.start_gap_metres = metres_between(origin.latitude, origin.longitude, start.latitude, start.longitude);
    check.end_gap_metres = metres_between(destination.latitude, destination.longitude, end.latitude, end.longitude);
    check.usable = std::isfinite(length_metres) && length_metres >= 1.0 &&
        std::isfinite(check.start_gap_metres) && check.start_gap_metres <= max_start_gap &&
        std::isfinite(check.end_gap_metres) && check.end_gap_metres <= max_end_gap;
    return check;
}

RouteCheck check_route_legs(const std::vector<RouteLeg>& legs, double gap_tolerance_metres)
{
    RouteCheck check;
    check.legs = legs.size();
    for (std::size_t index = 0; index < legs.size(); ++index) {
        const RouteLeg& leg = legs[index];
        if (leg.road_id == 0) ++check.filler_legs;
        if (index == 0) continue;
        const RouteLeg& previous = legs[index - 1];
        const double gap = metres_between(previous.end_latitude, previous.end_longitude,
                                          leg.start_latitude, leg.start_longitude);
        check.max_gap_metres = std::max(check.max_gap_metres, gap);
        if (gap > gap_tolerance_metres) ++check.gaps;
    }
    return check;
}

} // namespace navicore_osmand
