#include "routing/routing_settings.hpp"

#include "platform/measurement_utils.hpp"  // junction lab: was routing/routing_helpers.hpp

#include "base/assert.hpp"

namespace routing
{
namespace
{
// junction lab: C++17 port. Upstream returns C++20 designated initializers; this
// helper takes the same values in the same (declaration) order.
RoutingSettings MakeSettings(bool useDirectionForRouteBuilding, bool matchRoute, bool soundDirection,
                             double matchingThresholdM, bool showTurnAfterNext, double minSpeedForRouteRebuildMpS,
                             double finishToleranceM, size_t maxOutgoingPointsCount, double minOutgoingDistMeters,
                             size_t maxIngoingPointsCount, double minIngoingDistMeters)
{
  RoutingSettings s;
  s.m_useDirectionForRouteBuilding = useDirectionForRouteBuilding;
  s.m_matchRoute = matchRoute;
  s.m_soundDirection = soundDirection;
  s.m_matchingThresholdM = matchingThresholdM;
  s.m_showTurnAfterNext = showTurnAfterNext;
  s.m_minSpeedForRouteRebuildMpS = minSpeedForRouteRebuildMpS;
  s.m_finishToleranceM = finishToleranceM;
  s.m_maxOutgoingPointsCount = maxOutgoingPointsCount;
  s.m_minOutgoingDistMeters = minOutgoingDistMeters;
  s.m_maxIngoingPointsCount = maxIngoingPointsCount;
  s.m_minIngoingDistMeters = minIngoingDistMeters;
  return s;
}
}  // namespace

RoutingSettings GetRoutingSettings(VehicleType vehicleType)
{
  switch (vehicleType)
  {
  case VehicleType::Pedestrian:
    return MakeSettings(false, false, true, 20.0, false, -1, 15.0, 6, 5.0, 2, 4.0);
  case VehicleType::Transit:
    return MakeSettings(false, true, false, 40.0, false, -1, 15.0, 6, 5.0, 2, 4.0);
  case VehicleType::Bicycle:
    return MakeSettings(false, true, true, 30.0, false, -1, 15.0, 9, 10.0, 2, 10.0);
  case VehicleType::Car:
    return MakeSettings(true, true, true, 50.0, true, measurement_utils::KmphToMps(3.0), 20.0, 9, 120.0, 2, 100.0);
  case VehicleType::Count: CHECK(false, ("Can't create GetRoutingSettings for", vehicleType));
  }
  UNREACHABLE();
}
}  // namespace routing
