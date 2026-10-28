#pragma once

// junction lab: trimmed Organic Maps routing/directions_engine.hpp.
// Kept: the DirectionsEngine interface CarDirectionsEngine derives from and
// MakeTurnAnnotation. Removed: Generate(), FillPathSegmentsAndAdjacentEdgesMap(),
// GetSegmentRangeAndAdjacentEdges() and LoadPathAttributes(), which read OM's
// IndexRoadGraph and mwm features; tools/junction_lab/omaps_adapter.cpp builds the
// same LoadedPathSegments and turn candidates from OsmAnd's route instead.
// MakeTurnAnnotation is public and takes the IRoutingResult directly.

#include "routing/loaded_path_segment.hpp"
#include "routing/route.hpp"
#include "routing/routing_result_graph.hpp"
#include "routing/routing_settings.hpp"
#include "routing/vehicle_mask.hpp"

#include "routing_common/num_mwm_id.hpp"

#include <memory>
#include <vector>

namespace routing
{
namespace turns
{
class IRoutingResult;
struct TurnItem;
}  // namespace turns

// junction lab: no mwm data source; features come from the adapter.
class MwmDataSource
{
};

class DirectionsEngine
{
public:
  DirectionsEngine(MwmDataSource & dataSource, std::shared_ptr<NumMwmIds> numMwmIds);
  virtual ~DirectionsEngine() = default;

  void SetVehicleType(VehicleType const & vehicleType) { m_vehicleType = vehicleType; }

  /// junction lab: upstream is private and builds RoutingEngineResult from route edges.
  void MakeTurnAnnotation(turns::IRoutingResult const & result, std::vector<RouteSegment> & routeSegments);

protected:
  /*!
   * \brief GetTurnDirection makes a primary decision about turns on the route.
   * \param outgoingSegmentIndex index of an outgoing segments in vector result.GetSegments().
   * \param turn is used for keeping the result of turn calculation.
   */
  virtual size_t GetTurnDirection(turns::IRoutingResult const & result, size_t const outgoingSegmentIndex,
                                  NumMwmIds const & numMwmIds, RoutingSettings const & vehicleSettings,
                                  turns::TurnItem & turn) = 0;
  virtual void FixupTurns(std::vector<RouteSegment> & routeSegments) = 0;

  MwmDataSource & m_dataSource;
  std::shared_ptr<NumMwmIds> m_numMwmIds;
  VehicleType m_vehicleType = VehicleType::Count;
};
}  // namespace routing
