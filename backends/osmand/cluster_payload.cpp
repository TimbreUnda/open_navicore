#include "cluster_payload.h"

#include "junction_shape.h"

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
    OpenNaviBackendClusterV1 a = cluster, b = other.cluster;
    a.current_road = b.current_road = {};
    a.next_road = b.next_road = {};
    return std::memcmp(&a, &b, sizeof(a)) == 0 && current_road == other.current_road &&
           next_road == other.next_road;
}

namespace {

// Clock positions the cluster draws for a roundabout exit: angle from the
// entry direction (positive clockwise) and the right-/left-hand-traffic
// symbol. The LHT set 36..43 is the RHT set 19..26 in the same order.
struct ClockExit {
    double angle;
    uint8_t rht;
};
constexpr ClockExit clock_exits[] = {
    {0.0, OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_12},
    {30.0, OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_1},
    {90.0, OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_3},
    {120.0, OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_4},
    {180.0, OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_U},
    {-150.0, OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_7},
    {-90.0, OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_9},
    {-30.0, OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_11},
};
constexpr int lht_offset =
    OPEN_NAVI_CLUSTER_SYMBOL_LHT_ROUNDABOUT_12 - OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_12;

// The turn-type picture (b1044e1), used when no junction picture applies.
uint8_t turn_type_symbol(const ui::Maneuver& m) noexcept
{
    using K = ui::ManeuverKind;
    switch (m.kind) {
    case K::Straight: return OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT;
    case K::Left: return OPEN_NAVI_CLUSTER_SYMBOL_LEFT;
    case K::SlightLeft: return OPEN_NAVI_CLUSTER_SYMBOL_SLIGHT_LEFT;
    case K::SharpLeft: return OPEN_NAVI_CLUSTER_SYMBOL_SHARP_LEFT;
    case K::Right: return OPEN_NAVI_CLUSTER_SYMBOL_RIGHT;
    case K::SlightRight: return OPEN_NAVI_CLUSTER_SYMBOL_SLIGHT_RIGHT;
    case K::SharpRight: return OPEN_NAVI_CLUSTER_SYMBOL_SHARP_RIGHT;
    case K::KeepLeft: return OPEN_NAVI_CLUSTER_SYMBOL_FORK_LEFT;
    case K::KeepRight: return OPEN_NAVI_CLUSTER_SYMBOL_FORK_RIGHT;
    // OsmAnd TU is the left U-turn, TRU (left_side) the right one.
    case K::UTurn:
        return m.left_side ? OPEN_NAVI_CLUSTER_SYMBOL_U_TURN_RIGHT
                           : OPEN_NAVI_CLUSTER_SYMBOL_U_TURN_LEFT;
    case K::Roundabout: return roundabout_symbol(m.turn_angle_degrees, m.left_side);
    case K::Arrive: return OPEN_NAVI_CLUSTER_SYMBOL_APPROACHING_DESTINATION;
    }
    return OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT;
}

} // namespace

// The junction-shape picture when the geometry gives one, else the
// turn-type picture. Roundabouts, U-turns and arrival always use the
// turn type (junction_symbol returns 0 for them).
uint8_t maneuver_symbol(const ui::Maneuver& m) noexcept
{
    const uint8_t shaped = junction_symbol(m);
    return shaped ? shaped : turn_type_symbol(m);
}

uint8_t roundabout_symbol(double turn_angle_degrees, bool left_hand_traffic) noexcept
{
    double angle = std::isfinite(turn_angle_degrees)
        ? std::fmod(turn_angle_degrees, 360.0) : 0.0;
    uint8_t best = clock_exits[0].rht;
    double best_distance = 1000.0;
    for (const auto& exit : clock_exits) {
        double d = std::fabs(std::fmod(angle - exit.angle + 720.0, 360.0));
        d = std::min(d, 360.0 - d);
        if (d < best_distance) {
            best_distance = d;
            best = exit.rht;
        }
    }
    return static_cast<uint8_t>(left_hand_traffic ? best + lht_offset : best);
}

uint8_t cluster_symbol(const ui::GuidanceState& g) noexcept
{
    if (g.arrived) return OPEN_NAVI_CLUSTER_SYMBOL_DESTINATION_REACHED;
    if (g.recalculating) return OPEN_NAVI_CLUSTER_SYMBOL_RECALCULATING;
    return maneuver_symbol(g.maneuver);
}

uint8_t redraw_symbol(uint8_t symbol) noexcept
{
    switch (symbol) {
    case OPEN_NAVI_CLUSTER_SYMBOL_LEFT: return OPEN_NAVI_CLUSTER_SYMBOL_T_TURN_LEFT;
    case OPEN_NAVI_CLUSTER_SYMBOL_RIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_T_TURN_RIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_SLIGHT_LEFT: return OPEN_NAVI_CLUSTER_SYMBOL_EXIT_LEFT;
    case OPEN_NAVI_CLUSTER_SYMBOL_SLIGHT_RIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_EXIT_RIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_SHARP_LEFT: return OPEN_NAVI_CLUSTER_SYMBOL_LEFT;
    case OPEN_NAVI_CLUSTER_SYMBOL_SHARP_RIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_RIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_FORK_LEFT: return OPEN_NAVI_CLUSTER_SYMBOL_SPLIT_LEFT;
    case OPEN_NAVI_CLUSTER_SYMBOL_FORK_RIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_SPLIT_RIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_CROSSING_STRAIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_U_TURN_LEFT: return OPEN_NAVI_CLUSTER_SYMBOL_DIVIDED_U_TURN_LEFT;
    case OPEN_NAVI_CLUSTER_SYMBOL_U_TURN_RIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_DIVIDED_U_TURN_RIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_DIVIDED_U_TURN_LEFT: return OPEN_NAVI_CLUSTER_SYMBOL_U_TURN_LEFT;
    case OPEN_NAVI_CLUSTER_SYMBOL_DIVIDED_U_TURN_RIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_U_TURN_RIGHT;
    // Junction pictures: the plain arrow with the same meaning, or the
    // nearest junction picture.
    case OPEN_NAVI_CLUSTER_SYMBOL_T_END_LEFT: return OPEN_NAVI_CLUSTER_SYMBOL_LEFT;
    case OPEN_NAVI_CLUSTER_SYMBOL_T_END_RIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_RIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_T_TURN_LEFT: return OPEN_NAVI_CLUSTER_SYMBOL_LEFT;
    case OPEN_NAVI_CLUSTER_SYMBOL_T_TURN_RIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_RIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_CROSSING_STRAIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_SIDE_LEFT_STRAIGHT:
    case OPEN_NAVI_CLUSTER_SYMBOL_SIDE_RIGHT_STRAIGHT:
        return OPEN_NAVI_CLUSTER_SYMBOL_CROSSING_STRAIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_CROSS_SLASH_RIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_SLIGHT_RIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_CROSS_SLASH_LEFT: return OPEN_NAVI_CLUSTER_SYMBOL_SHARP_LEFT;
    case OPEN_NAVI_CLUSTER_SYMBOL_CROSS_BACKSLASH_RIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_SHARP_RIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_CROSS_BACKSLASH_LEFT: return OPEN_NAVI_CLUSTER_SYMBOL_SLIGHT_LEFT;
    case OPEN_NAVI_CLUSTER_SYMBOL_CROSS_SLASH_STRAIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT_PAST_RIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_CROSS_BACKSLASH_STRAIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT_PAST_LEFT;
    case OPEN_NAVI_CLUSTER_SYMBOL_EXIT_LEFT: return OPEN_NAVI_CLUSTER_SYMBOL_SLIGHT_LEFT;
    case OPEN_NAVI_CLUSTER_SYMBOL_EXIT_RIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_SLIGHT_RIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_LANE_CHANGE_LEFT: return OPEN_NAVI_CLUSTER_SYMBOL_EXIT_LEFT;
    case OPEN_NAVI_CLUSTER_SYMBOL_LANE_CHANGE_RIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_EXIT_RIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT_PAST_LEFT:
    case OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT_PAST_RIGHT:
        return OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_FAN_STRAIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_SPLIT_STRAIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_FAN_RIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_SPLIT_RIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_FAN_LEFT: return OPEN_NAVI_CLUSTER_SYMBOL_SPLIT_LEFT;
    case OPEN_NAVI_CLUSTER_SYMBOL_SPLIT_STRAIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_FAN_STRAIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_SPLIT_RIGHT: return OPEN_NAVI_CLUSTER_SYMBOL_FAN_RIGHT;
    case OPEN_NAVI_CLUSTER_SYMBOL_SPLIT_LEFT: return OPEN_NAVI_CLUSTER_SYMBOL_FAN_LEFT;
    default: break;
    }
    // A roundabout: the same exit in the other traffic direction's set.
    if (symbol >= OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_12 &&
        symbol <= OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_7)
        return static_cast<uint8_t>(symbol + lht_offset);
    if (symbol >= OPEN_NAVI_CLUSTER_SYMBOL_LHT_ROUNDABOUT_12 &&
        symbol <= OPEN_NAVI_CLUSTER_SYMBOL_LHT_ROUNDABOUT_7)
        return static_cast<uint8_t>(symbol - lht_offset);
    // Text symbols (15, 16, 27) and the rest: straight, or the crossing.
    return symbol == OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT ? OPEN_NAVI_CLUSTER_SYMBOL_CROSSING_STRAIGHT
                                                       : OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT;
}

bool apply_text_redraw(ClusterPayload& next, const ClusterPayload& last) noexcept
{
    constexpr uint16_t roads = OPEN_NAVI_CLUSTER_CURRENT_ROAD | OPEN_NAVI_CLUSTER_NEXT_ROAD;
    const auto& n = next.cluster;
    const auto& l = last.cluster;
    if (!(n.valid_attributes & l.valid_attributes & OPEN_NAVI_CLUSTER_ROUTE_SYMBOL) ||
        n.route_guidance_symbol != l.route_guidance_symbol)
        return false;
    const bool text_changed = (n.valid_attributes & roads) != (l.valid_attributes & roads) ||
                              next.current_road != last.current_road ||
                              next.next_road != last.next_road;
    if (!text_changed)
        return false;
    next.cluster.route_guidance_symbol = redraw_symbol(n.route_guidance_symbol);
    return true;
}

// Outside guidance: "no route", or "route calculation" while one is being
// calculated. Guidance without a position: "direction not available".
ClusterPayload build_cluster_payload(const ClusterInputs& in)
{
    ClusterPayload out;
    auto& c = out.cluster;
    c.abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
    c.struct_size = sizeof(c);
    uint16_t valid = 0;
    const auto& g = in.guidance;
    if (in.screen != ui::Screen::Guidance) {
        valid |= OPEN_NAVI_CLUSTER_NAVIGATION_STATUS;
        c.navigation_status = in.calculating_route ? OPEN_NAVI_CLUSTER_NAV_ROUTE_CALCULATION
                                                   : OPEN_NAVI_CLUSTER_NAV_NO_ROUTE;
    } else if (!g.position_available) {
        valid |= OPEN_NAVI_CLUSTER_NAVIGATION_STATUS;
        c.navigation_status = OPEN_NAVI_CLUSTER_NAV_DIRECTION_NOT_AVAILABLE;
    } else {
        const auto kind = g.maneuver.kind;
        const bool turn = kind != ui::ManeuverKind::Straight && kind != ui::ManeuverKind::Arrive;
        const bool near = turn && !g.arrived && g.distance_to_maneuver_metres <= 400.0;
        valid |= OPEN_NAVI_CLUSTER_ROUTE_GUIDANCE_STATE;
        c.route_guidance_state =
            g.arrived ? OPEN_NAVI_CLUSTER_ROUTE_DESTINATION_REACHED
            : g.recalculating ? OPEN_NAVI_CLUSTER_ROUTE_RECALCULATION
            : near && g.distance_to_maneuver_metres <= 50.0 ? OPEN_NAVI_CLUSTER_ROUTE_MANOEUVRE
            : near ? OPEN_NAVI_CLUSTER_ROUTE_MANOEUVRE_ANNOUNCEMENT
                   : OPEN_NAVI_CLUSTER_ROUTE_FOLLOW_STREET;
        if (g.recalculating) {
            valid |= OPEN_NAVI_CLUSTER_NAVIGATION_STATUS;
            c.navigation_status = OPEN_NAVI_CLUSTER_NAV_ROUTE_CALCULATION;
        }
        if (!g.arrived) {
            // Destination: whole km from 1 km, else metres.
            const double remaining = std::max(0.0, g.remaining_metres);
            valid |= OPEN_NAVI_CLUSTER_DISTANCE_DESTINATION;
            if (remaining >= 1000.0) {
                c.distance_destination = static_cast<uint32_t>(
                    std::min(65535.0, std::round(remaining / 1000.0)));
                c.distance_destination_unit = OPEN_NAVI_CLUSTER_DISTANCE_KILOMETRES;
            } else {
                c.distance_destination = static_cast<uint32_t>(std::round(remaining));
                c.distance_destination_unit = OPEN_NAVI_CLUSTER_DISTANCE_METRES;
            }
            // Next manoeuvre in tenths: km from 1 km, else metres rounded
            // to 10 m.
            const double to_turn = std::max(0.0, g.distance_to_maneuver_metres);
            valid |= OPEN_NAVI_CLUSTER_DISTANCE_MANEUVER;
            if (to_turn >= 1000.0) {
                c.distance_maneuver_tenths = static_cast<uint32_t>(
                    std::min(65535.0, std::round(to_turn / 100.0)));
                c.distance_maneuver_unit = OPEN_NAVI_CLUSTER_DISTANCE_KILOMETRES;
            } else {
                c.distance_maneuver_tenths =
                    static_cast<uint32_t>(std::round(to_turn / 10.0) * 100.0);
                c.distance_maneuver_unit = OPEN_NAVI_CLUSTER_DISTANCE_METRES;
            }
            // Arrival clock time (local) and the time left.
            const double seconds = std::max(0.0, g.remaining_seconds);
            const std::time_t arrival = in.now + static_cast<std::time_t>(seconds);
            std::tm local{};
            if (localtime_r(&arrival, &local)) {
                valid |= OPEN_NAVI_CLUSTER_ETA;
                c.eta_hour = static_cast<uint8_t>(local.tm_hour);
                c.eta_minute = static_cast<uint8_t>(local.tm_min);
            }
            const long minutes = std::lround(seconds / 60.0);
            valid |= OPEN_NAVI_CLUSTER_RTA;
            c.rta_hour = static_cast<uint8_t>(std::min(99L, minutes / 60));
            c.rta_minute = static_cast<uint8_t>(minutes % 60);
        }
        // The turn view needs a valid symbol and a valid bar fill (bit 10;
        // its value is not drawn). Bar: the turn countdown, 100 from 400 m
        // to 0 at the turn; 0 once arrived.
        valid |= OPEN_NAVI_CLUSTER_ROUTE_SYMBOL | OPEN_NAVI_CLUSTER_BAR_PERCENT;
        c.route_guidance_symbol = cluster_symbol(g);
        c.bar_percent = g.arrived ? 0 : static_cast<uint8_t>(
            std::clamp(std::lround(g.distance_to_maneuver_metres / 4.0), 0L, 100L));
        if (!g.road.empty()) {
            out.current_road = g.road;
            valid |= OPEN_NAVI_CLUSTER_CURRENT_ROAD;
        }
        if (!g.maneuver.road.empty()) {
            out.next_road = g.maneuver.road;
            valid |= OPEN_NAVI_CLUSTER_NEXT_ROAD;
        }
        if (g.speed_limit_kmh > 0 && g.speed_limit_kmh < 256) {
            valid |= OPEN_NAVI_CLUSTER_SPEED_LIMIT;
            c.speed_limit = static_cast<uint8_t>(g.speed_limit_kmh);
            c.speed_unit = OPEN_NAVI_CLUSTER_SPEED_KMH;
        }
        if (in.heading_valid) {
            // N=1 .. NW=8: 45 degree sectors centred on each direction.
            const int sector = static_cast<int>(
                std::floor(std::fmod(std::fmod(in.heading_degrees, 360.0) + 382.5, 360.0) / 45.0));
            valid |= OPEN_NAVI_CLUSTER_COMPASS;
            c.compass = static_cast<uint8_t>(OPEN_NAVI_CLUSTER_COMPASS_N + sector);
        }
    }
    c.valid_attributes = valid;
    out.bind();
    return out;
}

} // namespace navicore_osmand
