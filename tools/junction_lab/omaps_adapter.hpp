// omaps_adapter: runs Organic Maps' car turn generator (tools/junction_lab/omaps,
// CarDirectionsEngine::MakeTurnAnnotation + FixupCarTurns) on a route computed by
// OsmAnd's router. The route becomes OM LoadedPathSegments (split at OM joints) and
// every junction gets OM turn candidates made from all roads the OsmAnd routing
// context has at that node.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct RoutingContext;
struct RouteSegmentResult;

namespace junction_lab::omaps
{
struct Lane
{
  bool active = false;              // OM recommended this lane for the manoeuvre
  std::vector<std::string> turns;   // turn:lanes values of the lane (OSM spelling)
};

struct Maneuver
{
  long segment = -1;   // OsmAnd route segment that starts at the junction (-1: start/finish point)
  double lat = 0.0;
  double lon = 0.0;
  std::string turn;    // OM CarDirection name, e.g. TurnRight, ExitHighwayToRight
  std::uint32_t exit = 0;
  std::vector<Lane> lanes;
  std::string road;    // name (or ref) of the road after the manoeuvre
};

struct Result
{
  bool ok = false;
  std::string error;
  std::vector<Maneuver> maneuvers;
};

/// |context| is the RoutingContext the route was computed with (its loaded tiles and
/// car profile give the roads at each node). Start/finish are the requested points:
/// OM's fake start/finish edges join them to the route like OM's own router does.
Result annotate(RoutingContext & context, std::vector<std::shared_ptr<RouteSegmentResult>> const & route,
                double startLat, double startLon, double finishLat, double finishLon);
}  // namespace junction_lab::omaps
