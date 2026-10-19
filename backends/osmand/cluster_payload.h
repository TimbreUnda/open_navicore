#pragma once

#include "ui/navigation_model.h"

#include <open_navicore/backend.h>

#include <ctime>
#include <string>

namespace navicore_osmand {

// The instrument-cluster payload for the current navigation state, using
// only the recovered fields (include/open_navicore/cluster.h semantic mask;
// no lanes). During guidance it always carries a route-guidance symbol and
// a valid bar fill: the cluster draws its turn view only with both. The road
// spans point into the strings, so keep the payload object alive (and
// unmoved) while publishing.
struct ClusterPayload {
    OpenNaviBackendClusterV1 cluster{};
    std::string current_road;
    std::string next_road;
    // Points the spans at the strings; call after any copy or move.
    void bind() noexcept;
    bool same_as(const ClusterPayload& other) const noexcept;
};

struct ClusterInputs {
    ui::Screen screen = ui::Screen::FreeDrive;
    bool calculating_route = false;
    ui::GuidanceState guidance;
    bool heading_valid = false;  // vehicle moving with a known heading
    double heading_degrees = 0.0;
    std::time_t now = 0;         // for the arrival clock time
};

ClusterPayload build_cluster_payload(const ClusterInputs& inputs);

// The cluster symbol (OpenNaviClusterRouteSymbol, 1..61) for a guidance
// state: arrived 27, recalculating 16, destination next 15, otherwise the
// next manoeuvre's picture: the junction-shape picture when its geometry
// gives one (junction_shape.h), else the turn-type picture.
uint8_t cluster_symbol(const ui::GuidanceState& guidance) noexcept;
// The picture for one manoeuvre (what cluster_symbol uses during guidance).
uint8_t maneuver_symbol(const ui::Maneuver& maneuver) noexcept;
// A roundabout exit at turn_angle_degrees (positive clockwise from the entry
// direction) as the nearest clock position the cluster has (12, 1, 3, 4,
// U-turn, 7, 9, 11), from the right- or left-hand-traffic set.
uint8_t roundabout_symbol(double turn_angle_degrees, bool left_hand_traffic) noexcept;
// A different valid symbol close in meaning to `symbol`, sent for one
// publication so that the cluster redraws (see apply_text_redraw).
uint8_t redraw_symbol(uint8_t symbol) noexcept;
// The cluster redraws its turn view only when the symbol changes, so a new
// road text with the same symbol would not appear. When `next` keeps the
// symbol of `last` but changes a road text, this replaces next's symbol with
// redraw_symbol() and returns true; the caller publishes it and then the
// real payload at its next publication, which differs again by the symbol.
bool apply_text_redraw(ClusterPayload& next, const ClusterPayload& last) noexcept;

} // namespace navicore_osmand
