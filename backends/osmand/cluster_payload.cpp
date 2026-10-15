#include "cluster_payload.h"

#include <open_navicore/cluster.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace navicore_osmand {

void ClusterPayload::bind() noexcept
{
    cluster.current_road = {current_road.data(), static_cast<uint32_t>(current_road.size())};
    cluster.next_road = {next_road.data(), static_cast<uint32_t>(next_road.size())};
}

bool ClusterPayload::same_as(const ClusterPayload& other) const noexcept
{
    MockNavBackendClusterV1 a = cluster, b = other.cluster;
    a.current_road = b.current_road = {};
    a.next_road = b.next_road = {};
    return std::memcmp(&a, &b, sizeof(a)) == 0 && current_road == other.current_road &&
           next_road == other.next_road;
}

// Outside guidance: "no route", or "route calculation" while one is being
// calculated. Guidance without a position: "direction not available".
ClusterPayload build_cluster_payload(const ClusterInputs& in)
{
    ClusterPayload out;
    auto& c = out.cluster;
    c.abi_version = MOCK_NAV_BACKEND_PAYLOAD_ABI_V1;
    c.struct_size = sizeof(c);
    uint16_t valid = 0;
    const auto& g = in.guidance;
    if (in.screen != ui::Screen::Guidance) {
        valid |= MOCK_CLUSTER_NAVIGATION_STATUS;
        c.navigation_status = in.calculating_route ? MOCK_CLUSTER_NAV_ROUTE_CALCULATION
                                                   : MOCK_CLUSTER_NAV_NO_ROUTE;
    } else if (!g.position_available) {
        valid |= MOCK_CLUSTER_NAVIGATION_STATUS;
        c.navigation_status = MOCK_CLUSTER_NAV_DIRECTION_NOT_AVAILABLE;
    } else {
        const auto kind = g.maneuver.kind;
        const bool turn = kind != ui::ManeuverKind::Straight && kind != ui::ManeuverKind::Arrive;
        const bool near = turn && !g.arrived && g.distance_to_maneuver_metres <= 400.0;
        valid |= MOCK_CLUSTER_ROUTE_GUIDANCE_STATE;
        c.route_guidance_state =
            g.arrived ? MOCK_CLUSTER_ROUTE_DESTINATION_REACHED
            : g.recalculating ? MOCK_CLUSTER_ROUTE_RECALCULATION
            : near && g.distance_to_maneuver_metres <= 50.0 ? MOCK_CLUSTER_ROUTE_MANOEUVRE
            : near ? MOCK_CLUSTER_ROUTE_MANOEUVRE_ANNOUNCEMENT
                   : MOCK_CLUSTER_ROUTE_FOLLOW_STREET;
        if (g.recalculating) {
            valid |= MOCK_CLUSTER_NAVIGATION_STATUS;
            c.navigation_status = MOCK_CLUSTER_NAV_ROUTE_CALCULATION;
        }
        if (!g.arrived) {
            // Destination: whole km from 1 km, else metres.
            const double remaining = std::max(0.0, g.remaining_metres);
            valid |= MOCK_CLUSTER_DISTANCE_DESTINATION;
            if (remaining >= 1000.0) {
                c.distance_destination = static_cast<uint32_t>(
                    std::min(65535.0, std::round(remaining / 1000.0)));
                c.distance_destination_unit = MOCK_CLUSTER_DISTANCE_KILOMETRES;
            } else {
                c.distance_destination = static_cast<uint32_t>(std::round(remaining));
                c.distance_destination_unit = MOCK_CLUSTER_DISTANCE_METRES;
            }
            // Next manoeuvre in tenths: km from 1 km, else metres rounded
            // to 10 m.
            const double to_turn = std::max(0.0, g.distance_to_maneuver_metres);
            valid |= MOCK_CLUSTER_DISTANCE_MANEUVER;
            if (to_turn >= 1000.0) {
                c.distance_maneuver_tenths = static_cast<uint32_t>(
                    std::min(65535.0, std::round(to_turn / 100.0)));
                c.distance_maneuver_unit = MOCK_CLUSTER_DISTANCE_KILOMETRES;
            } else {
                c.distance_maneuver_tenths =
                    static_cast<uint32_t>(std::round(to_turn / 10.0) * 100.0);
                c.distance_maneuver_unit = MOCK_CLUSTER_DISTANCE_METRES;
            }
            // Arrival clock time (local) and the time left.
            const double seconds = std::max(0.0, g.remaining_seconds);
            const std::time_t arrival = in.now + static_cast<std::time_t>(seconds);
            std::tm local{};
            if (localtime_r(&arrival, &local)) {
                valid |= MOCK_CLUSTER_ETA;
                c.eta_hour = static_cast<uint8_t>(local.tm_hour);
                c.eta_minute = static_cast<uint8_t>(local.tm_min);
            }
            const long minutes = std::lround(seconds / 60.0);
            valid |= MOCK_CLUSTER_RTA;
            c.rta_hour = static_cast<uint8_t>(std::min(99L, minutes / 60));
            c.rta_minute = static_cast<uint8_t>(minutes % 60);
        }
        if (near) {
            // The turn countdown: 100 at 400 m to 0 at the turn.
            valid |= MOCK_CLUSTER_BAR_PERCENT;
            c.bar_percent = static_cast<uint8_t>(
                std::clamp(std::lround(g.distance_to_maneuver_metres / 4.0), 0L, 100L));
        }
        if (!g.road.empty()) {
            out.current_road = g.road;
            valid |= MOCK_CLUSTER_CURRENT_ROAD;
        }
        if (!g.maneuver.road.empty()) {
            out.next_road = g.maneuver.road;
            valid |= MOCK_CLUSTER_NEXT_ROAD;
        }
        if (g.speed_limit_kmh > 0 && g.speed_limit_kmh < 256) {
            valid |= MOCK_CLUSTER_SPEED_LIMIT;
            c.speed_limit = static_cast<uint8_t>(g.speed_limit_kmh);
            c.speed_unit = MOCK_CLUSTER_SPEED_KMH;
        }
        if (in.heading_valid) {
            // N=1 .. NW=8: 45 degree sectors centred on each direction.
            const int sector = static_cast<int>(
                std::floor(std::fmod(std::fmod(in.heading_degrees, 360.0) + 382.5, 360.0) / 45.0));
            valid |= MOCK_CLUSTER_COMPASS;
            c.compass = static_cast<uint8_t>(MOCK_CLUSTER_COMPASS_N + sector);
        }
    }
    c.valid_attributes = valid;
    out.bind();
    return out;
}

} // namespace navicore_osmand
