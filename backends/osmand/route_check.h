#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "ui/navigation_model.h"

namespace navicore_osmand {

// One routed leg as the router returned it: the road it follows and the
// positions it starts and ends at. Pure data so the check is host-testable.
struct RouteLeg {
    int64_t road_id = 0;  // 0: not a map road (the router's straight-line filler)
    double start_latitude = 0.0;
    double start_longitude = 0.0;
    double end_latitude = 0.0;
    double end_longitude = 0.0;
};

struct RouteCheck {
    std::size_t legs = 0;
    std::size_t filler_legs = 0;  // legs without a map road
    std::size_t gaps = 0;         // joins where the next leg starts away from the last end
    double max_gap_metres = 0.0;

    bool ok() const { return legs > 0 && filler_legs == 0 && gaps == 0; }
};

// Highway-hierarchy results fall back to a straight line between two network
// nodes where a leg's detailed road geometry could not be loaded. Such a route
// is drawn across water and forest, so it must never be shown as a route.
RouteCheck check_route_legs(const std::vector<RouteLeg>& legs, double gap_tolerance_metres = 150.0);

struct RouteExtentCheck {
    bool usable = false;
    double start_gap_metres = 0;
    double end_gap_metres = 0;
};

// The selected router must actually reach both endpoints within its snap radius,
// and return a travelled path, not two copies of the same junction.
RouteExtentCheck check_route_extent(ui::GeoPoint origin, ui::GeoPoint destination,
                                   ui::GeoPoint start, ui::GeoPoint end, double length_metres,
                                   double max_start_gap = 1000.0, double max_end_gap = 1000.0);

} // namespace navicore_osmand
