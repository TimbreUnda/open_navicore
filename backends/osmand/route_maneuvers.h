#pragma once

#include "omaps_turns.h"
#include "ui/navigation_model.h"

#include <cstddef>
#include <memory>
#include <vector>

// Core and core-legacy both ship a Logging.h with the same public names but
// different include guards. The provider uses Core's logger consistently.
#include <OsmAndCore/Logging.h>
#define _OSMAND_LOGGING_H_
#include "routeSegmentResult.h"
#include "turnType.h"

namespace navicore_osmand {

// The manoeuvres of an OsmAnd route, as the navigation worker publishes
// them (and tools/junction_lab shows them on the host): turn kind, lanes and
// the junction shape the cluster picture is chosen from.
using Segments = std::vector<SHARED_PTR<RouteSegmentResult>>;

ui::ManeuverKind maneuver_kind(int turn_type);
std::vector<ui::Lane> lanes_of(const SHARED_PTR<TurnType>& turn);
ui::JunctionShape junction_of(Segments& segments, std::size_t index);

// The manoeuvre at the start of segments[index], or false when the segment
// carries none (no turn type, or OsmAnd's "continue"). Fills everything but
// the road name and the route position, which the caller knows.
bool maneuver_at(Segments& segments, std::size_t index, ui::Maneuver& maneuver);

// A manoeuvre and where it is: the OsmAnd segment that leaves it and its index in
// the route's points (every segment's points, consecutive duplicates once: the
// worker's RouteAlternative::geometry).
struct PlacedManeuver {
    std::size_t segment = 0;
    std::size_t route_point = 0;
    ui::Maneuver maneuver;
};

// The index of each segment's start point among the route's points.
std::vector<std::size_t> segment_start_points(const Segments& segments);

// The manoeuvre for one of Organic Maps' turns: its kind and lanes, and the junction
// shape from OM's route angle and other roads (the cluster picture).
ui::Maneuver maneuver_from(const omaps::Turn& turn);

// The route's manoeuvres in route order, without the arrival: Organic Maps' turns
// with OsmAnd's roundabouts (OsmAnd gives the exit and its angle). |turns| null:
// Organic Maps is switched off, OsmAnd's own (maneuver_at). An OM result that
// failed gives none: no silent fallback, the route visibly has no turns (the
// worker traces the error).
std::vector<PlacedManeuver> route_maneuvers(Segments& segments, const omaps::Result* turns);

} // namespace navicore_osmand
