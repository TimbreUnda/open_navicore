#include "guidance_tracker.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace navicore_osmand {
namespace {

constexpr double earth_metres_per_degree = 111320.0;
constexpr double arrival_radius_metres = 25.0;

double radians(double degrees) noexcept
{
    return degrees * 3.14159265358979323846 / 180.0;
}

double distance(const ui::GeoPoint& a, const ui::GeoPoint& b) noexcept
{
    const double latitude = radians((a.latitude + b.latitude) * 0.5);
    const double x = (b.longitude - a.longitude) *
                     earth_metres_per_degree * std::cos(latitude);
    const double y = (b.latitude - a.latitude) * earth_metres_per_degree;
    return std::hypot(x, y);
}

// Direction of the route geometry at `along` metres of its own length.
bool bearing_at(const std::vector<ui::GeoPoint>& geometry, double along,
                double& bearing_degrees) noexcept
{
    double walked = 0.0;
    for (std::size_t index = 1; index < geometry.size(); ++index) {
        const auto& a = geometry[index - 1];
        const auto& b = geometry[index];
        const double latitude = radians((a.latitude + b.latitude) * 0.5);
        const double east = (b.longitude - a.longitude) *
                            earth_metres_per_degree * std::cos(latitude);
        const double north = (b.latitude - a.latitude) * earth_metres_per_degree;
        const double length = std::hypot(east, north);
        if (length <= 0.0)
            continue;
        walked += length;
        if (walked >= along || index + 1 == geometry.size()) {
            bearing_degrees = std::atan2(east, north) * 180.0 / 3.14159265358979323846;
            if (bearing_degrees < 0.0)
                bearing_degrees += 360.0;
            return true;
        }
    }
    return false;
}

void choose_maneuvers(const ui::RouteAlternative& route,
                      ui::GuidanceState& state) noexcept
{
    state.maneuver = {};
    state.following_maneuver = {};
    state.has_following_maneuver = false;
    const ui::Maneuver* next = nullptr;
    const ui::Maneuver* following = nullptr;
    for (const auto& maneuver : route.maneuvers) {
        if (maneuver.distance_from_start_metres + 5.0 < state.progress_metres)
            continue;
        if (!next) next = &maneuver;
        else { following = &maneuver; break; }
    }
    if (next) {
        state.maneuver = *next;
        state.distance_to_maneuver_metres = std::max(
            0.0, next->distance_from_start_metres - state.progress_metres);
    } else {
        state.maneuver.kind = ui::ManeuverKind::Arrive;
        state.distance_to_maneuver_metres = state.remaining_metres;
    }
    if (following) {
        state.following_maneuver = *following;
        state.has_following_maneuver = true;
    }
}

} // namespace

ui::GuidanceState GuidanceTracker::unavailable(
    const ui::RouteAlternative& route) const
{
    ui::GuidanceState state;
    state.progress_metres = progress_metres_;
    state.remaining_metres = std::max(0.0, route.distance_metres - progress_metres_);
    const double fraction = route.distance_metres > 0.0
        ? state.remaining_metres / route.distance_metres : 0.0;
    state.remaining_seconds = route.duration_seconds * fraction;
    choose_maneuvers(route, state);
    return state;
}

ui::GuidanceState GuidanceTracker::update(const ui::RouteAlternative& route,
                                          const ui::GeoPoint& position) noexcept
{
    ui::GuidanceState state = unavailable(route);
    state.position_available = true;
    state.position = position;
    if (route.geometry.size() < 2)
        return state;

    double nearest_squared = std::numeric_limits<double>::max();
    double nearest_along = 0.0;
    double geometry_length = 0.0;
    for (std::size_t index = 1; index < route.geometry.size(); ++index) {
        const auto& a = route.geometry[index - 1];
        const auto& b = route.geometry[index];
        const double latitude = radians((a.latitude + b.latitude +
                                         position.latitude) / 3.0);
        const double scale_x = earth_metres_per_degree * std::cos(latitude);
        const double ax = (a.longitude - position.longitude) * scale_x;
        const double ay = (a.latitude - position.latitude) * earth_metres_per_degree;
        const double bx = (b.longitude - position.longitude) * scale_x;
        const double by = (b.latitude - position.latitude) * earth_metres_per_degree;
        const double dx = bx - ax;
        const double dy = by - ay;
        const double length_squared = dx * dx + dy * dy;
        const double segment_length = std::sqrt(length_squared);
        const double projection = length_squared > 0.0
            ? std::clamp(-(ax * dx + ay * dy) / length_squared, 0.0, 1.0)
            : 0.0;
        const double px = ax + projection * dx;
        const double py = ay + projection * dy;
        const double squared = px * px + py * py;
        if (squared < nearest_squared) {
            nearest_squared = squared;
            nearest_along = geometry_length + projection * segment_length;
        }
        geometry_length += segment_length;
    }
    if (geometry_length <= 0.0)
        return state;
    state.off_route_metres = std::sqrt(nearest_squared);

    const double route_progress = nearest_along / geometry_length *
        route.distance_metres;
    // Vehicle jitter must not make guidance count backwards. A future reroute
    // replaces the route and resets this tracker explicitly.
    progress_metres_ = std::max(progress_metres_, route_progress);
    state.progress_metres = progress_metres_;
    state.remaining_metres = std::max(0.0, route.distance_metres - progress_metres_);
    const double fraction = route.distance_metres > 0.0
        ? state.remaining_metres / route.distance_metres : 0.0;
    state.remaining_seconds = route.duration_seconds * fraction;
    // At the progress point rather than the nearest segment: a route that
    // comes back along the other carriageway is near but points the other way.
    state.route_bearing_known = route.distance_metres > 0.0 &&
        bearing_at(route.geometry, progress_metres_ / route.distance_metres * geometry_length,
                   state.route_bearing_degrees);
    state.arrived = state.remaining_metres <= arrival_radius_metres ||
        distance(position, route.geometry.back()) <= arrival_radius_metres;
    choose_maneuvers(route, state);
    for (const auto& road : route.roads) {
        if (road.start_metres > progress_metres_ + 1.0)
            break;
        state.road = road.name;
        state.speed_limit_kmh = road.speed_limit_kmh;
    }
    if (state.arrived) {
        state.maneuver.kind = ui::ManeuverKind::Arrive;
        state.distance_to_maneuver_metres = 0.0;
        state.remaining_metres = 0.0;
        state.remaining_seconds = 0.0;
    }
    return state;
}

} // namespace navicore_osmand
