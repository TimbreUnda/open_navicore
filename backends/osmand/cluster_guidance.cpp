#include "cluster_guidance.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace navicore_osmand {

double cluster_vehicle_speed_mps(const OpenNaviBackendVehicleV2& v) noexcept
{
    const double unknown = std::numeric_limits<double>::quiet_NaN();
    if (v.struct_size < sizeof(v) || !(v.valid_fields & OPEN_NAVI_BACKEND_VEHICLE_SPEED))
        return unknown;
    float raw;
    std::memcpy(&raw, &v.displayed_vehicle_speed_bits, sizeof(raw));
    double kmh = raw;
    if (v.displayed_vehicle_speed_unit == 2u)
        kmh *= 1.609344;
    else if (v.displayed_vehicle_speed_unit != 1u)
        return unknown;
    if (!std::isfinite(kmh) || kmh < 0 || kmh > 325.0)
        return unknown;
    return kmh / 3.6;
}

double cluster_approach_metres(double speed_mps) noexcept
{
    if (!std::isfinite(speed_mps) || speed_mps < 0)
        return 150.0;
    const double kmh = speed_mps * 3.6;
    // 25 seconds through 50 km/h, increasing continuously to 45 at 90.
    const double seconds = 25.0 + 20.0 * std::clamp((kmh - 50.0) / 40.0, 0.0, 1.0);
    return std::clamp(speed_mps * seconds, 150.0, 2000.0);
}

ClusterPhase ClusterGuidance::update(ui::Screen screen, const ui::GuidanceState& g,
                                    double speed_mps, double now) noexcept
{
    if (screen != ui::Screen::Guidance || g.recalculating || g.arrived) {
        reset();
        return ClusterPhase::Preview; // status/arrival payloads remain visible
    }
    if (!g.position_available)
        return ClusterPhase::Quiet; // preserve the timer through a lost fix
    if (!std::isfinite(now))
        return ClusterPhase::Preview;
    if (have_maneuver_)
        now = std::max(now, last_time_);
    last_time_ = now;
    const auto& m = g.maneuver;
    const double threshold = cluster_approach_metres(speed_mps);
    const bool distance_valid = std::isfinite(g.distance_to_maneuver_metres) &&
                                g.distance_to_maneuver_metres >= 0;
    if (!have_maneuver_ || geometry_index_ != m.geometry_index ||
        route_distance_ != m.distance_from_start_metres || kind_ != m.kind ||
        exit_ != m.roundabout_exit) {
        // Keep the view through a short pair of turns, including a stop
        // between them. Road text and distance updates are not new actions.
        approaching_ = approaching_ && distance_valid &&
                        g.distance_to_maneuver_metres <= std::max(300.0, threshold);
        have_maneuver_ = true;
        geometry_index_ = m.geometry_index;
        route_distance_ = m.distance_from_start_metres;
        kind_ = m.kind;
        exit_ = m.roundabout_exit;
        preview_started_ = now;
    }
    if (distance_valid && g.distance_to_maneuver_metres <= threshold)
        approaching_ = true;
    if (approaching_)
        return ClusterPhase::Approach;
    // Without usable speed/distance we cannot decide that a turn is safely
    // far away. Keep it visible, without latching an artificial approach.
    if (!distance_valid || !std::isfinite(speed_mps) || speed_mps < 0 ||
        now - preview_started_ < 12.0)
        return ClusterPhase::Preview;
    return ClusterPhase::Quiet;
}

} // namespace navicore_osmand
