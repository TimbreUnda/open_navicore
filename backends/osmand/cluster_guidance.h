#pragma once

#include "ui/navigation_model.h"
#include <open_navicore/backend.h>

namespace navicore_osmand {

enum class ClusterPhase { Quiet, Preview, Approach };

// Returns NaN when the vehicle sample is missing or invalid. No GPS
// substitution: callers keep the instruction visible and report the missing input.
double cluster_vehicle_speed_mps(const OpenNaviBackendVehicleV2& vehicle) noexcept;
double cluster_approach_metres(double speed_mps) noexcept;

class ClusterGuidance {
public:
    // now_seconds is monotonic. Reset when the route is replaced, even if
    // the new route happens to have an identical first manoeuvre.
    ClusterPhase update(ui::Screen screen, const ui::GuidanceState& guidance,
                        double speed_mps, double now_seconds) noexcept;
    void reset() noexcept { *this = {}; }

private:
    bool have_maneuver_ = false;
    bool approaching_ = false;
    std::size_t geometry_index_ = 0;
    double route_distance_ = 0;
    ui::ManeuverKind kind_ = ui::ManeuverKind::Straight;
    int exit_ = 0;
    double preview_started_ = 0;
    double last_time_ = 0;
};

} // namespace navicore_osmand
