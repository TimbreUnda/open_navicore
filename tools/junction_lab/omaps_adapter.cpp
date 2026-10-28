// omaps_adapter: OsmAnd route -> Organic Maps IRoutingResult -> OM car turns.
// See omaps_adapter.hpp. What the OM code reads from mwm features is read here from
// OsmAnd's RouteDataObject; tools/junction_lab/omaps/README.md lists the mapping
// and the defaults used where OsmAnd has no equivalent.

#include "omaps_adapter.hpp"

// OsmAnd first: the OM shims define ASSERT/CHECK/LOG macros.
#include "binaryRead.h"
#include "binaryRoutePlanner.h"
#include "generalRouter.h"
#include "routeSegmentResult.h"
#include "routingConfiguration.h"
#include "routingContext.h"

#include "routing/car_directions.hpp"
#include "routing/lanes/lane_way.hpp"
#include "routing/lanes/lanes_parser.hpp"
#include "routing/loaded_path_segment.hpp"
#include "routing/route.hpp"
#include "routing/routing_result_graph.hpp"
#include "routing/turn_candidate.hpp"
#include "routing/turns.hpp"
#include "routing/turns_generator.hpp"
#include "routing/turns_generator_utils.hpp"

#include "geometry/mercator.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>

namespace junction_lab::omaps
{
namespace
{
using ftypes::HighwayClass;
using RoadPtr = SHARED_PTR<RouteDataObject>;

// ---- road attributes (what OM's LoadPathAttributes reads from a feature) ----------------

// ftypes::GetHighwayClass (indexer/ftypes_matcher.cpp) on OsmAnd's tags.
HighwayClass HighwayClassOf(RouteDataObject & road)
{
  std::string const highway = road.getHighway();
  if (highway == "motorway" || highway == "motorway_link" || highway == "trunk" || highway == "trunk_link")
    return HighwayClass::Trunk;
  if (highway == "primary" || highway == "primary_link")
    return HighwayClass::Primary;
  if (highway == "secondary" || highway == "secondary_link")
    return HighwayClass::Secondary;
  if (highway == "tertiary" || highway == "tertiary_link")
    return HighwayClass::Tertiary;
  if (highway == "unclassified" || highway == "residential" || highway == "living_street" || highway == "road")
    return HighwayClass::LivingStreet;
  if (highway == "service")
  {
    // OM checks the 3-level types highway-service-driveway/parking_aisle first.
    std::string const service = road.getValue("service");
    if (service == "driveway" || service == "parking_aisle")
      return HighwayClass::ServiceMinor;
    return HighwayClass::Service;
  }
  if (highway == "track" || highway == "busway")
    return HighwayClass::Service;
  if (highway == "pedestrian" || highway == "platform" || highway == "footway" || highway == "bridleway" ||
      highway == "ladder" || highway == "steps" || highway == "cycleway" || highway == "path" ||
      highway == "construction")
    return HighwayClass::Pedestrian;
  std::string const route = road.getValue("route");
  if (route == "ferry" || route == "shuttle_train")
    return HighwayClass::Transported;
  if (road.getValue("man_made") == "pier")
    return HighwayClass::Service;
  return HighwayClass::Undefined;
}

// ftypes::IsLinkChecker.
bool IsLink(RouteDataObject & road)
{
  std::string const highway = road.getHighway();
  return highway == "motorway_link" || highway == "trunk_link" || highway == "primary_link" ||
         highway == "secondary_link" || highway == "tertiary_link";
}

// turn:lanes for the travel direction as OsmAnd's getTurnLanesString reads it
// (oneway: turn:lanes; else turn:lanes:forward/backward). When that is empty, OM's
// fallback (GetLanesMetadataTag: plain turn:lanes) is used.
std::string TurnLanesOf(RouteDataObject & road, int oneway, bool forward)
{
  std::string lanes;
  if (oneway != 0)
    lanes = road.getValue("turn:lanes");
  else
    lanes = road.getValue(forward ? "turn:lanes:forward" : "turn:lanes:backward");
  if (lanes.empty())
    lanes = road.getValue("turn:lanes");
  return lanes;
}

// ---- graph edges (OM's Edge between two junctions of one feature) -----------------------

struct LabEdge
{
  FeatureID feature;  // invalid for OM's fake start/finish edges
  uint32_t segId = 0;
  bool forward = true;
  m2::PointD start;
  m2::PointD end;
  RoadPtr road;

  bool IsFake() const { return !feature.IsValid(); }
};

routing::Segment ToSegment(LabEdge const & edge)
{
  if (edge.IsFake())
    return routing::Segment();
  return routing::Segment(0 /* mwm */, edge.feature.m_index, edge.segId, edge.forward);
}

bool IsUTurnPair(LabEdge const & a, LabEdge const & b)
{
  return a.feature == b.feature && a.segId == b.segId && a.forward != b.forward;
}

// directions_engine_helpers.cpp IsJoint(), unchanged logic on LabEdge.
bool IsJoint(std::vector<LabEdge> const & ingoingEdges, std::vector<LabEdge> const & outgoingEdges,
             LabEdge const & ingoingRouteEdge, LabEdge const & outgoingRouteEdge)
{
  if (ingoingRouteEdge.feature != outgoingRouteEdge.feature)
    return true;

  FeatureID const & featureId = ingoingRouteEdge.feature;
  uint32_t const segOut = outgoingRouteEdge.segId;
  for (LabEdge const & e : ingoingEdges)
    if (e.feature != featureId || std::abs(static_cast<int32_t>(segOut - e.segId)) != 1)
      return true;

  uint32_t const segIn = ingoingRouteEdge.segId;
  for (LabEdge const & e : outgoingEdges)
  {
    // It's necessary to compare segments for cases when |featureId| is a loop.
    if (e.feature != featureId || std::abs(static_cast<int32_t>(segIn - e.segId)) != 1)
      return true;
  }
  return false;
}

// ---- the IRoutingResult OM's generator reads ----------------------------------------------

struct AdjacentEdges
{
  explicit AdjacentEdges(size_t ingoingTurnsCount = 0) : m_ingoingTurnsCount(ingoingTurnsCount) {}
  routing::turns::TurnCandidates m_outgoingTurns;
  size_t m_ingoingTurnsCount;
};

using AdjacentEdgesMap = std::map<routing::SegmentRange, AdjacentEdges>;

// RoutingEngineResult (directions_engine_helpers.cpp) without IRoadGraph.
class LabRoutingResult : public routing::turns::IRoutingResult
{
public:
  LabRoutingResult(routing::RouteJunctions const & path, AdjacentEdgesMap const & adjacentEdges,
                   routing::TUnpackedPathSegments const & pathSegments)
    : m_path(path)
    , m_adjacentEdges(adjacentEdges)
    , m_pathSegments(pathSegments)
  {
    for (size_t i = 1; i < path.size(); ++i)
      m_routeLength += mercator::DistanceOnEarth(path[i - 1].GetPoint(), path[i].GetPoint());
  }

  routing::TUnpackedPathSegments const & GetSegments() const override { return m_pathSegments; }

  void GetPossibleTurns(routing::SegmentRange const & segmentRange, m2::PointD const & junctionPoint,
                        size_t & ingoingCount, routing::turns::TurnCandidates & outgoingTurns) const override
  {
    CHECK(!segmentRange.IsEmpty(), ("SegmentRange presents a fake feature."));
    ingoingCount = 0;
    outgoingTurns.candidates.clear();
    auto const adjacentEdges = m_adjacentEdges.find(segmentRange);
    if (adjacentEdges == m_adjacentEdges.cend())
      return;
    ingoingCount = adjacentEdges->second.m_ingoingTurnsCount;
    outgoingTurns = adjacentEdges->second.m_outgoingTurns;
  }

  double GetPathLength() const override { return m_routeLength; }
  geometry::PointWithAltitude GetStartPoint() const override { return m_path.front(); }
  geometry::PointWithAltitude GetEndPoint() const override { return m_path.back(); }

private:
  routing::RouteJunctions const & m_path;
  AdjacentEdgesMap const & m_adjacentEdges;
  routing::TUnpackedPathSegments const & m_pathSegments;
  double m_routeLength = 0.0;
};

// OM's Generate() is protected/pure-virtual-based; this exposes the car engine.
class LabCarDirectionsEngine : public routing::CarDirectionsEngine
{
public:
  LabCarDirectionsEngine(routing::MwmDataSource & dataSource)
    : routing::CarDirectionsEngine(dataSource, std::make_shared<routing::NumMwmIds>())
  {
    SetVehicleType(routing::VehicleType::Car);
  }
};

// ---- building it from OsmAnd ---------------------------------------------------------------

m2::PointD Mercator31(uint32_t x31, uint32_t y31)
{
  return mercator::FromLatLon(get31LatitudeY(static_cast<int>(y31)), get31LongitudeX(static_cast<int>(x31)));
}

struct RoadAtNode
{
  RoadPtr road;
  int index = 0;
};

struct RoutePoint
{
  uint32_t x31 = 0;
  uint32_t y31 = 0;
  m2::PointD point;
  long osmandSegment = -1;  // segment whose edge leaves this point
};

struct RouteEdgeSource
{
  RoadPtr road;  // the route's road object (may be a copy with an inserted point)
  int copyFrom = 0;
  int copyTo = 0;
};

class Builder
{
public:
  Builder(RoutingContext & context) : m_context(context) {}

  Result Run(std::vector<SHARED_PTR<RouteSegmentResult>> const & route, double startLat, double startLon,
             double finishLat, double finishLon);

private:
  std::vector<RoadAtNode> const & RoadsAt(uint32_t x31, uint32_t y31);
  uint32_t FeatureIndex(RoadPtr const & road);
  int OneWay(RoadPtr const & road);
  bool Restricted(RoadPtr const & from, RoadPtr const & to, std::vector<RoadAtNode> const & atNode) const;
  LabEdge MakeEdge(RoadPtr const & road, int fromIndex, int toIndex);
  void EdgesAt(size_t pathIndex, std::vector<LabEdge> & ingoing, std::vector<LabEdge> & outgoing);
  void LoadPathAttributes(LabEdge const & inEdge, routing::LoadedPathSegment & pathSegment);
  void FillPathSegments();
  void DumpPathSegments() const;

  RoutingContext & m_context;
  std::map<std::pair<uint32_t, uint32_t>, std::vector<RoadAtNode>> m_nodes;
  std::unordered_map<int64_t, uint32_t> m_featureIndex;

  // OM layout: path[0] = path[1] = start, path[2..n+1] = route points, path[n+2] = path[n+3] = finish.
  routing::RouteJunctions m_path;
  std::vector<LabEdge> m_routeEdges;     // m_path.size() - 1 edges
  std::vector<RoutePoint> m_points;      // route points (path index - 2)
  AdjacentEdgesMap m_adjacentEdges;
  routing::TUnpackedPathSegments m_pathSegments;
};

std::vector<RoadAtNode> const & Builder::RoadsAt(uint32_t x31, uint32_t y31)
{
  auto const key = std::make_pair(x31, y31);
  auto it = m_nodes.find(key);
  if (it != m_nodes.end())
    return it->second;
  std::vector<RoadAtNode> roads;
  for (auto const & rs : m_context.loadRouteSegment(static_cast<int>(x31), static_cast<int>(y31)))
    roads.push_back({rs->getRoad(), rs->getSegmentStart()});
  return m_nodes.emplace(key, std::move(roads)).first->second;
}

uint32_t Builder::FeatureIndex(RoadPtr const & road)
{
  auto const inserted = m_featureIndex.emplace(road->getId(), static_cast<uint32_t>(m_featureIndex.size()));
  return inserted.first->second;
}

int Builder::OneWay(RoadPtr const & road)
{
  return m_context.config->router->isOneWay(road);
}

// OsmAnd turn restrictions without a via way: no_* from |from| to |to|, or an only_*
// restriction of |from| whose target road is at this node and is not |to|.
bool Builder::Restricted(RoadPtr const & from, RoadPtr const & to, std::vector<RoadAtNode> const & atNode) const
{
  if (from->getId() == to->getId())
    return false;  // staying on the road (or its U-turn) is not a turn restriction
  bool hasOnly = false;
  bool onlyTargetHere = false;
  for (auto const & r : from->restrictions)
  {
    if (r.via != 0)
      continue;
    bool const isOnly = r.type == RESTRICTION_ONLY_RIGHT_TURN || r.type == RESTRICTION_ONLY_LEFT_TURN ||
                        r.type == RESTRICTION_ONLY_STRAIGHT_ON;
    if (static_cast<int64_t>(r.to) == to->getId())
      return !isOnly;  // no_* to |to| forbids it, only_* to |to| allows it
    if (isOnly)
    {
      hasOnly = true;
      for (auto const & n : atNode)
        if (n.road->getId() == static_cast<int64_t>(r.to))
          onlyTargetHere = true;
    }
  }
  return hasOnly && onlyTargetHere;
}

LabEdge Builder::MakeEdge(RoadPtr const & road, int fromIndex, int toIndex)
{
  LabEdge edge;
  edge.feature = FeatureID(0, FeatureIndex(road));
  edge.forward = toIndex > fromIndex;
  edge.segId = static_cast<uint32_t>(std::min(fromIndex, toIndex));
  edge.start = Mercator31(road->pointsX[fromIndex], road->pointsY[fromIndex]);
  edge.end = Mercator31(road->pointsX[toIndex], road->pointsY[toIndex]);
  edge.road = road;
  return edge;
}

// IndexRoadGraph::GetIngoingEdges / GetOutgoingEdges at route point |pathIndex| for a car:
// outgoing = segments the route's ingoing segment can continue to, ingoing = segments
// that can enter the route's outgoing segment. One-way per OsmAnd's car profile; a
// U-turn onto the same segment only at a joint or a road end (IndexGraph::IsUTurnAndRestricted);
// turn restrictions without via ways.
void Builder::EdgesAt(size_t pathIndex, std::vector<LabEdge> & ingoing, std::vector<LabEdge> & outgoing)
{
  ingoing.clear();
  outgoing.clear();
  if (pathIndex < 2 || pathIndex >= m_path.size() - 2)
    return;  // fake start / finish point
  LabEdge const & inEdge = m_routeEdges[pathIndex - 1];
  if (inEdge.IsFake())
    return;  // the jump from the start point: no turn is generated there (GetTurnDirection, index 2)
  LabEdge const & outEdge = m_routeEdges[pathIndex];

  RoutePoint const & p = m_points[pathIndex - 2];
  auto const & roads = RoadsAt(p.x31, p.y31);
  bool const isJointPoint = roads.size() >= 2;

  for (auto const & at : roads)
  {
    RoadPtr const & road = at.road;
    int const n = road->getPointsLength();
    int const k = at.index;
    int const oneway = OneWay(road);
    bool const uTurnAllowed = isJointPoint || k == 0 || k == n - 1;

    // Outgoing.
    if (k + 1 < n && oneway >= 0)
      outgoing.push_back(MakeEdge(road, k, k + 1));
    if (k > 0 && oneway <= 0)
      outgoing.push_back(MakeEdge(road, k, k - 1));
    // Ingoing.
    if (k > 0 && oneway >= 0)
      ingoing.push_back(MakeEdge(road, k - 1, k));
    if (k + 1 < n && oneway <= 0)
      ingoing.push_back(MakeEdge(road, k + 1, k));

    if (!uTurnAllowed)
    {
      outgoing.erase(std::remove_if(outgoing.begin(), outgoing.end(),
                                    [&](LabEdge const & e) { return IsUTurnPair(e, inEdge); }),
                     outgoing.end());
      if (!outEdge.IsFake())
        ingoing.erase(std::remove_if(ingoing.begin(), ingoing.end(),
                                     [&](LabEdge const & e) { return IsUTurnPair(e, outEdge); }),
                      ingoing.end());
    }
  }

  outgoing.erase(std::remove_if(outgoing.begin(), outgoing.end(),
                                [&](LabEdge const & e) { return Restricted(inEdge.road, e.road, roads); }),
                 outgoing.end());
  if (!outEdge.IsFake())
    ingoing.erase(std::remove_if(ingoing.begin(), ingoing.end(),
                                 [&](LabEdge const & e) { return Restricted(e.road, outEdge.road, roads); }),
                  ingoing.end());
}

// DirectionsEngine::LoadPathAttributes on an OsmAnd road.
void Builder::LoadPathAttributes(LabEdge const & inEdge, routing::LoadedPathSegment & pathSegment)
{
  if (inEdge.IsFake())
    return;
  RouteDataObject & road = *inEdge.road;
  bool const isForward = inEdge.forward;
  int const oneway = OneWay(inEdge.road);

  pathSegment.m_lanes = routing::turns::lanes::ParseLanes(TurnLanesOf(road, oneway, isForward));
  pathSegment.m_highwayClass = HighwayClassOf(road);
  pathSegment.m_isLink = IsLink(road);
  pathSegment.m_onRoundabout = road.roundabout();
  pathSegment.m_isOneWay = oneway != 0;

  // OsmAnd's getName(lang)/getRef() index |names| with operator[] and so insert empty
  // entries into the shared road object; look the default name/ref up read-only.
  auto const nameByRule = [&road](int rule) -> std::string
  {
    auto const it = road.names.find(rule);
    return it == road.names.end() ? std::string() : it->second;
  };
  std::string lang;
  pathSegment.m_roadNameInfo.m_mwmId = inEdge.feature;
  pathSegment.m_roadNameInfo.m_isLink = pathSegment.m_isLink;
  pathSegment.m_roadNameInfo.m_junction_ref = road.getValue("junction:ref");
  pathSegment.m_roadNameInfo.m_destination_ref = road.getDestinationRef(isForward);
  pathSegment.m_roadNameInfo.m_destination = road.getDestinationName(lang, false, isForward);
  pathSegment.m_roadNameInfo.m_ref = nameByRule(road.region->refTypeRule);
  pathSegment.m_roadNameInfo.m_name = nameByRule(road.region->nameTypeRule);
}

// DirectionsEngine::FillPathSegmentsAndAdjacentEdgesMap + GetSegmentRangeAndAdjacentEdges,
// same control flow, on LabEdge.
void Builder::FillPathSegments()
{
  size_t const pathSize = m_path.size();
  CHECK_GREATER(pathSize, 1, ());
  CHECK_EQUAL(m_routeEdges.size() + 1, pathSize, ());

  auto constexpr kInvalidSegId = std::numeric_limits<uint32_t>::max();
  uint32_t startSegId = kInvalidSegId;

  routing::RouteJunctions prevJunctions;
  std::vector<routing::Segment> prevSegments;
  std::vector<LabEdge> ingoingEdges, outgoingEdges;
  for (size_t i = 1; i < pathSize; ++i)
  {
    auto const & currJunction = m_path[i];
    bool const isCurrJunctionFinish = (i + 1 == pathSize);

    EdgesAt(i, ingoingEdges, outgoingEdges);
    if (isCurrJunctionFinish)
      outgoingEdges.clear();

    LabEdge const & inEdge = m_routeEdges[i - 1];
    uint32_t const inSegId = inEdge.segId;

    if (startSegId == kInvalidSegId)
      startSegId = inSegId;

    prevJunctions.push_back(m_path[i - 1]);
    prevSegments.push_back(ToSegment(inEdge));

    if (!isCurrJunctionFinish && !inEdge.IsFake() && !IsJoint(ingoingEdges, outgoingEdges, inEdge, m_routeEdges[i]))
      continue;

    CHECK_EQUAL(prevJunctions.size(), static_cast<size_t>(std::abs(int(inSegId) - int(startSegId)) + 1), ());

    prevJunctions.push_back(currJunction);

    AdjacentEdges adjacentEdges(ingoingEdges.size());
    // GetSegmentRangeAndAdjacentEdges.
    routing::turns::TurnCandidates & outgoingTurns = adjacentEdges.m_outgoingTurns;
    outgoingTurns.isCandidatesAngleValid = true;
    routing::SegmentRange const segmentRange(inEdge.feature, startSegId, inSegId, inEdge.forward, inEdge.start,
                                             inEdge.end);
    CHECK(segmentRange.IsCorrect(), ());
    m2::PointD const & ingoingPoint = inEdge.start;
    m2::PointD const & junctionPoint = inEdge.end;
    for (auto const & edge : outgoingEdges)
    {
      if (edge.IsFake())
        continue;
      double angle = 0;
      double const distToJunction = mercator::DistanceOnEarth(junctionPoint, edge.start);
      if (distToJunction < routing::turns::kFeaturesNearTurnMeters)
        angle = math::RadToDeg(routing::turns::PiMinusTwoVectorsAngle(junctionPoint, ingoingPoint, edge.end));
      else
        outgoingTurns.isCandidatesAngleValid = false;
      outgoingTurns.candidates.emplace_back(angle, ToSegment(edge), HighwayClassOf(*edge.road), IsLink(*edge.road));
    }
    if (outgoingTurns.isCandidatesAngleValid)
      std::sort(outgoingTurns.candidates.begin(), outgoingTurns.candidates.end(),
                base::LessBy(&routing::turns::TurnCandidate::m_angle));

    routing::LoadedPathSegment pathSegment;
    pathSegment.m_segmentRange = segmentRange;
    CHECK_EQUAL(prevSegments.size() + 1, prevJunctions.size(), ());
    pathSegment.m_path = std::move(prevJunctions);
    pathSegment.m_segments = std::move(prevSegments);

    LoadPathAttributes(inEdge, pathSegment);

    if (!segmentRange.IsEmpty())
      m_adjacentEdges.emplace(segmentRange, std::move(adjacentEdges));

    m_pathSegments.push_back(std::move(pathSegment));

    prevJunctions.clear();
    prevSegments.clear();
    startSegId = kInvalidSegId;
  }
}

// JUNCTION_LAB_OMAPS_DEBUG=1: OM's view of the route on stderr.
void Builder::DumpPathSegments() const
{
  for (size_t i = 0; i < m_pathSegments.size(); ++i)
  {
    auto const & s = m_pathSegments[i];
    ms::LatLon const a = mercator::ToLatLon(s.m_path.front().GetPoint());
    ms::LatLon const b = mercator::ToLatLon(s.m_path.back().GetPoint());
    std::fprintf(stderr,
                 "omaps seg %zu: %.6f,%.6f -> %.6f,%.6f points=%zu class=%s link=%d oneway=%d roundabout=%d "
                 "lanes=%zu name=\"%s\" ref=\"%s\" first=%s\n",
                 i, a.m_lat, a.m_lon, b.m_lat, b.m_lon, s.m_path.size(), ftypes::DebugPrint(s.m_highwayClass).c_str(),
                 s.m_isLink, s.m_isOneWay, s.m_onRoundabout, s.m_lanes.size(), s.m_roadNameInfo.m_name.c_str(),
                 s.m_roadNameInfo.m_ref.c_str(), routing::DebugPrint(s.m_segments.front()).c_str());
    auto const it = m_adjacentEdges.find(s.m_segmentRange);
    if (it == m_adjacentEdges.end())
      continue;
    std::fprintf(stderr, "    ingoing=%zu candidates:", it->second.m_ingoingTurnsCount);
    for (auto const & c : it->second.m_outgoingTurns.candidates)
      std::fprintf(stderr, " [%.1f %s%s f%u s%u%s]", c.m_angle, ftypes::DebugPrint(c.m_highwayClass).c_str(),
                   c.m_isLink ? " link" : "", c.m_segment.GetFeatureId(), c.m_segment.GetSegmentIdx(),
                   c.m_segment.IsForward() ? "+" : "-");
    std::fprintf(stderr, "\n");
  }
}

char const * LaneWayName(routing::turns::lanes::LaneWay way)
{
  using routing::turns::lanes::LaneWay;
  switch (way)
  {
  case LaneWay::None: return "none";
  case LaneWay::ReverseLeft: return "reverse";
  case LaneWay::SharpLeft: return "sharp_left";
  case LaneWay::Left: return "left";
  case LaneWay::MergeToLeft: return "merge_to_left";
  case LaneWay::SlightLeft: return "slight_left";
  case LaneWay::Through: return "through";
  case LaneWay::SlightRight: return "slight_right";
  case LaneWay::MergeToRight: return "merge_to_right";
  case LaneWay::Right: return "right";
  case LaneWay::SharpRight: return "sharp_right";
  case LaneWay::ReverseRight: return "reverse_right";
  case LaneWay::Count: break;
  }
  return "?";
}

Result Builder::Run(std::vector<SHARED_PTR<RouteSegmentResult>> const & route, double startLat, double startLon,
                    double finishLat, double finishLon)
{
  Result result;

  // 1. Flatten OsmAnd's segments into route points and road edges.
  std::vector<RouteEdgeSource> sources;
  for (size_t s = 0; s < route.size(); ++s)
  {
    auto const & segment = route[s];
    if (!segment || !segment->object)
      continue;
    RoadPtr const & road = segment->object;
    int const start = segment->getStartPointIndex();
    int const end = segment->getEndPointIndex();
    int const step = start <= end ? 1 : -1;
    for (int i = start;; i += step)
    {
      RoutePoint p;
      p.x31 = road->pointsX[i];
      p.y31 = road->pointsY[i];
      p.point = Mercator31(p.x31, p.y31);
      p.osmandSegment = static_cast<long>(s);
      bool const same = !m_points.empty() && m_points.back().x31 == p.x31 && m_points.back().y31 == p.y31;
      if (!same)
      {
        if (!m_points.empty())
        {
          m_points.back().osmandSegment = static_cast<long>(s);
          // A segment that does not start where the previous one ended (not expected)
          // gets an edge numbered from the map road alone (see step 2).
          sources.push_back({road, i == start ? i : i - step, i});
        }
        m_points.push_back(p);
      }
      if (i == end)
        break;
    }
  }
  if (m_points.size() < 2)
  {
    result.error = "route has fewer than two points";
    return result;
  }

  // 2. Route edges in the map's own point numbering (OsmAnd may route on a copy of a
  //    road with the precise start/end point inserted).
  std::vector<LabEdge> realEdges;
  for (size_t e = 0; e < sources.size(); ++e)
  {
    auto const & src = sources[e];
    RoutePoint const & a = m_points[e];
    RoutePoint const & b = m_points[e + 1];
    std::vector<int> ia, ib;
    for (auto const & n : RoadsAt(a.x31, a.y31))
      if (n.road->getId() == src.road->getId())
        ia.push_back(n.index);
    RoadPtr original;
    for (auto const & n : RoadsAt(b.x31, b.y31))
      if (n.road->getId() == src.road->getId())
      {
        ib.push_back(n.index);
        original = n.road;
      }
    if (!original)
      for (auto const & n : RoadsAt(a.x31, a.y31))
        if (n.road->getId() == src.road->getId())
          original = n.road;
    bool const forward = src.copyTo > src.copyFrom;
    int from = -1, to = -1;
    for (int x : ia)
      for (int y : ib)
        if (std::abs(x - y) == 1 && (y > x) == forward)
        {
          from = x;
          to = y;
        }
    LabEdge edge;
    if (from >= 0)
    {
      edge = MakeEdge(original, from, to);
    }
    else
    {
      // A point of the copy that the map road does not have: keep the copy's geometry,
      // number the segment by the neighbour that exists in the map road.
      edge.feature = FeatureID(0, FeatureIndex(src.road));
      edge.forward = forward;
      if (!ib.empty())
        edge.segId = static_cast<uint32_t>(forward ? ib.front() - 1 : ib.front());
      else if (!ia.empty())
        edge.segId = static_cast<uint32_t>(forward ? ia.front() : ia.front() - 1);
      else
        edge.segId = static_cast<uint32_t>(std::min(src.copyFrom, src.copyTo));
      edge.start = a.point;
      edge.end = b.point;
      edge.road = original ? original : src.road;
    }
    realEdges.push_back(edge);
  }

  // 3. OM's layout with fake start/finish loops and jumps.
  m2::PointD const startPoint = mercator::FromLatLon(startLat, startLon);
  m2::PointD const finishPoint = mercator::FromLatLon(finishLat, finishLon);
  auto fake = [](m2::PointD const & a, m2::PointD const & b)
  {
    LabEdge e;
    e.start = a;
    e.end = b;
    return e;
  };
  m_path.push_back(startPoint);
  m_path.push_back(startPoint);
  m_routeEdges.push_back(fake(startPoint, startPoint));
  m_routeEdges.push_back(fake(startPoint, m_points.front().point));
  for (auto const & p : m_points)
    m_path.push_back(p.point);
  for (auto const & e : realEdges)
    m_routeEdges.push_back(e);
  m_routeEdges.push_back(fake(m_points.back().point, finishPoint));
  m_routeEdges.push_back(fake(finishPoint, finishPoint));
  m_path.push_back(finishPoint);
  m_path.push_back(finishPoint);

  // 4. Loaded path segments + adjacent edges, then OM's annotation.
  FillPathSegments();

  if (std::getenv("JUNCTION_LAB_OMAPS_DEBUG"))
    DumpPathSegments();

  LabRoutingResult routingResult(m_path, m_adjacentEdges, m_pathSegments);
  if (std::getenv("JUNCTION_LAB_OMAPS_DEBUG"))
  {
    // OM's smoothed turn angle (CalcTurnAngle) and one-segment angle at each joint.
    routing::NumMwmIds const numMwmIds;
    auto const settings = routing::GetRoutingSettings(routing::VehicleType::Car);
    for (size_t i = 3; i + 2 < m_pathSegments.size(); ++i)
    {
      routing::turns::TurnInfo info(&m_pathSegments[i - 1], &m_pathSegments[i]);
      ms::LatLon const at = mercator::ToLatLon(m_pathSegments[i].m_path.front().GetPoint());
      std::fprintf(stderr, "omaps angle into seg %zu at %.6f,%.6f: turn=%.1f one_segment=%.1f\n", i, at.m_lat,
                   at.m_lon, routing::turns::CalcTurnAngle(routingResult, i, numMwmIds, settings),
                   routing::turns::CalcOneSegmentTurnAngle(info));
    }
  }
  routing::MwmDataSource dataSource;
  LabCarDirectionsEngine engine(dataSource);
  std::vector<routing::RouteSegment> routeSegments;
  engine.MakeTurnAnnotation(routingResult, routeSegments);
  CHECK_EQUAL(routeSegments.size(), m_routeEdges.size(), ());

  // 5. Turns in route order. routeSegments[i] ends at m_path[i + 1].
  for (size_t i = 0; i < routeSegments.size(); ++i)
  {
    auto const & turn = routeSegments[i].GetTurn();
    if (turn.IsTurnNone())
      continue;
    Maneuver m;
    size_t const pathIndex = i + 1;
    ms::LatLon const ll = mercator::ToLatLon(m_path[pathIndex].GetPoint());
    m.lat = ll.m_lat;
    m.lon = ll.m_lon;
    if (pathIndex >= 2 && pathIndex - 2 < m_points.size())
    {
      RoutePoint const & p = m_points[pathIndex - 2];
      m.segment = p.osmandSegment;
      m.lat = get31LatitudeY(static_cast<int>(p.y31));
      m.lon = get31LongitudeX(static_cast<int>(p.x31));
    }
    m.turn = routing::turns::GetTurnString(turn.m_turn);
    m.exit = turn.m_exitNum;
    for (auto const & lane : turn.m_lanes)
    {
      Lane out;
      out.active = lane.recommendedWay != routing::turns::lanes::LaneWay::None;
      for (auto const way : lane.laneWays.GetActiveLaneWays())
        out.turns.push_back(LaneWayName(way));
      m.lanes.push_back(out);
    }
    if (i + 1 < routeSegments.size())
    {
      auto const & next = routeSegments[i + 1].GetRoadNameInfo();
      m.road = !next.m_name.empty() ? next.m_name : next.m_ref;
    }
    result.maneuvers.push_back(m);
  }
  result.ok = true;
  return result;
}
}  // namespace

Result annotate(RoutingContext & context, std::vector<std::shared_ptr<RouteSegmentResult>> const & route,
                double startLat, double startLon, double finishLat, double finishLon)
{
  try
  {
    Builder builder(context);
    return builder.Run(route, startLat, startLon, finishLat, finishLon);
  }
  catch (std::exception const & e)
  {
    Result result;
    result.error = e.what();
    return result;
  }
}
}  // namespace junction_lab::omaps
