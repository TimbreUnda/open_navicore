#pragma once

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

} // namespace navicore_osmand
