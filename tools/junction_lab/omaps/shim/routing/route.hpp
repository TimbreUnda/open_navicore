// Trimmed Organic Maps routing/route.hpp + route.cpp: RouteJunctions and the parts of
// RouteSegment the turn generator touches (turn item, junction, road name info,
// IsSegregatedTurn, MergeLanes). Speed limits, traffic, transit, cameras and
// distances/times are removed. The kept member functions are copied unchanged.
#pragma once

#include "routing/lanes/lane_info.hpp"
#include "routing/routing_settings.hpp"
#include "routing/segment.hpp"
#include "routing/turns.hpp"

#include "indexer/feature_decl.hpp"

#include "geometry/point_with_altitude.hpp"

#include "base/assert.hpp"
#include "base/math.hpp"

#include <string>
#include <utility>
#include <vector>

namespace routing
{
using RouteJunctions = std::vector<geometry::PointWithAltitude>;

class RouteSegment final
{
public:
  struct RoadNameInfo
  {
    FeatureID m_mwmId;
    // This is for street/road. |m_ref| |m_name|.
    std::string m_name;             // E.g "Johnson Ave.".
    std::string m_destination_ref;  // Number of next road, e.g. "CA 85", Sometimes "CA 85 South". Usually match |m_ref|
                                    // of next main road.
    // This is for 1st segment of link after junction. Exit |junction_ref| to |m_destination_ref| for |m_destination|.
    std::string m_junction_ref;  // Number of junction e.g. "398B".
    std::string m_destination;   // E.g. "Cupertino".
    std::string m_ref;           // Number of street/road e.g. "CA 85".
    bool m_isLink = false;

    RoadNameInfo() = default;
    RoadNameInfo(std::string name) : m_name(std::move(name)) {}

    void ClearUselessStringsForTailSegments()
    {
      // Keep 'name' since it is used in turns comparison.
      m_destination_ref.clear();
      m_junction_ref.clear();
      m_destination.clear();
      m_ref.clear();
    }

    bool HasBasicTextInfo() const { return !m_ref.empty() || !m_name.empty(); }
    bool HasExitInfo() const { return m_isLink || HasExitTextInfo(); }
    bool HasExitTextInfo() const
    {
      return !m_junction_ref.empty() || !m_destination_ref.empty() || !m_destination.empty();
    }
    bool empty() const
    {
      return m_name.empty() && m_ref.empty() && m_junction_ref.empty() && m_destination_ref.empty() &&
             m_destination.empty();
    }

    bool operator==(RoadNameInfo const & rni) const
    {
      return m_name == rni.m_name && m_ref == rni.m_ref && m_junction_ref == rni.m_junction_ref &&
             m_destination_ref == rni.m_destination_ref && m_destination == rni.m_destination;
    }
  };

  RouteSegment(Segment const & segment, turns::TurnItem const & turn, geometry::PointWithAltitude const & junction,
               RoadNameInfo const & roadNameInfo)
    : m_segment(segment)
    , m_turn(turn)
    , m_junction(junction)
    , m_roadNameInfo(roadNameInfo)
  {}

  void ClearTurn()
  {
    m_turn.m_turn = turns::CarDirection::None;
    m_turn.m_pedestrianTurn = turns::PedestrianDirection::None;
  }

  void SetTurnExits(uint32_t exitNum) { m_turn.m_exitNum = exitNum; }

  turns::lanes::LanesInfo & GetTurnLanes() { return m_turn.m_lanes; }

  Segment const & GetSegment() const { return m_segment; }
  geometry::PointWithAltitude const & GetJunction() const { return m_junction; }
  RoadNameInfo const & GetRoadNameInfo() const { return m_roadNameInfo; }
  turns::TurnItem const & GetTurn() const { return m_turn; }
  void ClearTurnLanes() { m_turn.m_lanes.clear(); }

  /// Is 'from' a segregated turn before this turn.
  bool IsSegregatedTurn(RouteSegment const & from) const
  {
    // Treat 'from' -> this turn is segregated candidate when (distance is checked by a caller):
    // - "st A" -> "st A" or
    // - Any -> unnamed link

    if (!m_roadNameInfo.m_name.empty())
      return m_roadNameInfo.m_name == from.m_roadNameInfo.m_name;
    else
      return m_roadNameInfo.m_isLink;
  }

  /// Merge lanes info from segregated 'from' into this if needed.
  void MergeLanes(RouteSegment & from)
  {
    /// @todo Keep 'from' lanes now. Probably, can move and clear.
    if (m_turn.m_lanes.empty())
      m_turn.m_lanes = from.m_turn.m_lanes;
  }

private:
  Segment m_segment;
  turns::TurnItem m_turn;
  geometry::PointWithAltitude m_junction;
  RoadNameInfo m_roadNameInfo;
};
}  // namespace routing
