#pragma once

// The turn instructions of an OsmAnd route, decided by Organic Maps' car turn
// generator (third_party/organicmaps-routing: CarDirectionsEngine::MakeTurnAnnotation
// and FixupCarTurns). OsmAnd's router finds the route; its segments become OM
// LoadedPathSegments (split at OM joints) and every junction gets OM turn
// candidates made from all roads the routing context has at that node. OM measures
// a turn over the curve of the road, not its first 10 m, announces a link that
// leaves the road, and skips a road that only continues.
// third_party/organicmaps-routing/README.md lists the OsmAnd -> OM mapping.

#include "ui/navigation_model.h"
#include "route_roads.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct RoutingContext;
struct RouteSegmentResult;

namespace navicore_osmand::omaps {

struct Turn {
    // Index into the route's points: every segment's points from its start to its end
    // point, consecutive duplicates once (the worker's RouteAlternative::geometry).
    std::size_t route_point = 0;
    std::size_t segment = 0;  // the OsmAnd segment that leaves that point
    double latitude = 0.0;
    double longitude = 0.0;
    std::string direction;    // OM CarDirection name (TurnRight, ExitHighwayToLeft, ...)
    ui::ManeuverKind kind = ui::ManeuverKind::Straight;
    bool left_side = false;   // UTurnRight (as OsmAnd's TRU)
    bool roundabout = false;  // Enter/Leave/StayOnRoundAbout
    std::uint32_t exit = 0;
    std::vector<ui::Lane> lanes;  // left to right, from turn:lanes only
    // Degrees from the incoming direction, positive clockwise: the route as OM
    // compares it (the larger of the curve and the first-segment angle) and every
    // other road a car may leave the junction by.
    double route_angle = 0.0;
    std::vector<double> other_roads;
    std::string road;         // name (or ref) of the road after the turn
};

struct Result {
    bool ok = false;
    std::string error;
    std::vector<Turn> turns;  // in route order; no start, no arrival
};

// |context| is the RoutingContext the route was computed with: its loaded tiles and
// car profile give the roads at each node (prepareResult loaded them). The start and
// finish are the requested points: OM's fake start/finish edges join them to the
// route like OM's own router does. Never throws: an OM check failure is an error.
Result annotate(RoutingContext& context, const std::vector<std::shared_ptr<RouteSegmentResult>>& route,
                double start_latitude, double start_longitude, double finish_latitude,
                double finish_longitude, const RouteRoads* roads = nullptr);

} // namespace navicore_osmand::omaps
