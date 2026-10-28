#include "routing/car_directions.hpp"
#include "routing/turns_generator.hpp"
#include "routing/turns_generator_utils.hpp"
#include "geometry/mercator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// Internal car decision entry point: run the complete classification, including
// suppression and leftmost/rightmost correction, on a small routing result.
namespace routing {
void GetTurnDirectionBasic(turns::IRoutingResult const &, size_t, NumMwmIds const &,
                           RoutingSettings const &, turns::TurnItem &);
}
namespace {
using namespace routing;
using namespace routing::turns;
using ftypes::HighwayClass;

void require(bool ok, char const * message)
{
  if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

m2::PointD metres(double east, double north)
{
  return mercator::FromLatLon(north / 111317.0, east / 111317.0);
}

struct Route : IRoutingResult
{
  TUnpackedPathSegments segments{2};
  TurnCandidates candidates;
  Route(std::vector<m2::PointD> const & outgoing)
  {
    segments[0].m_path = {metres(0, -80), metres(0, 0)};
    for (auto const & p : outgoing) segments[1].m_path.emplace_back(p);
    for (size_t i = 0; i < 2; ++i)
    {
      auto & s = segments[i];
      s.m_highwayClass = HighwayClass::LivingStreet;
      s.m_segmentRange = SegmentRange(FeatureID(0, i + 1), 5, 5, true,
                                     s.m_path.front().GetPoint(), s.m_path.back().GetPoint());
    }
  }
  TUnpackedPathSegments const & GetSegments() const override { return segments; }
  void GetPossibleTurns(SegmentRange const &, m2::PointD const &, size_t & count,
                        TurnCandidates & turns) const override { count = 3; turns = candidates; }
  double GetPathLength() const override { return 200; }
  geometry::PointWithAltitude GetStartPoint() const override { return segments[0].m_path.front(); }
  geometry::PointWithAltitude GetEndPoint() const override { return segments[1].m_path.back(); }
  double angle() const
  { return CalcInstructionTurnAngle(*this, 1, NumMwmIds{}, GetRoutingSettings(VehicleType::Car)); }
  CarDirection direction() const
  {
    TurnItem turn;
    GetTurnDirectionBasic(*this, 1, NumMwmIds{}, GetRoutingSettings(VehicleType::Car), turn);
    return turn.m_turn;
  }
};

void entranceTests()
{
  // The reported entrance at Вереснева площа: short perpendicular entrance,
  // then a diagonal street. Use its actual geometry as well as synthetic cases.
  Route reported({mercator::FromLatLon(48.485428, 34.954022),
                  mercator::FromLatLon(48.485426, 34.953947),
                  mercator::FromLatLon(48.485430, 34.953880),
                  mercator::FromLatLon(48.485503, 34.953754),
                  mercator::FromLatLon(48.485778, 34.953483),
                  mercator::FromLatLon(48.486694, 34.952391)});
  reported.segments[0].m_path = {mercator::FromLatLon(48.484921, 34.954151),
                                mercator::FromLatLon(48.485428, 34.954022)};
  require(reported.angle() > -40 && reported.angle() < -30, "reported entrance should use the diagonal heading");
  for (double sign : {-1.0, 1.0})
  {
    Route rounded({metres(0, 0), metres(sign * 5, 0), metres(sign * 10, 5), metres(sign * 50, 45)});
    require(std::abs(std::abs(rounded.angle()) - 45) < 1, "rounded entrance should use diagonal heading");
    // Other exits: opposite diagonal and ahead. A U-turn is included as in the adapter.
    rounded.candidates.candidates = {{-135, Segment(0, 3, 0, true), HighwayClass::LivingStreet, false},
      {sign * 90, Segment(0, 2, 5, true), HighwayClass::LivingStreet, false},
      {10, Segment(0, 4, 0, true), HighwayClass::LivingStreet, false},
      {180, Segment(0, 1, 5, false), HighwayClass::LivingStreet, false}};
    std::sort(rounded.candidates.candidates.begin(), rounded.candidates.candidates.end(),
              [](auto const & a, auto const & b) { return a.m_angle < b.m_angle; });
    require(rounded.direction() == (sign < 0 ? CarDirection::TurnSlightLeft : CarDirection::TurnSlightRight),
            "rounded entrance must produce a slight instruction");

    Route square({metres(0, 0), metres(sign * 5, 0), metres(sign * 80, 0)});
    require(std::abs(std::abs(square.angle()) - 90) < 1, "square corner must stay square");
    Route sharp({metres(0, 0), metres(sign * 2, -5), metres(sign * 60, 40)});
    require(std::abs(sharp.angle()) > 135, "sharp turn must remain sharp");
    Route shortRoad({metres(0, 0), metres(sign * 5, 0), metres(sign * 10, 5)});
    require(std::abs(shortRoad.angle()) > 80, "must not infer a settled heading beyond the next junction");
    Route laterBend({metres(0, 0), metres(sign * 40, 0), metres(sign * 70, 30)});
    require(std::abs(laterBend.angle()) > 80, "a later bend must not soften the corner");
    Route sBend({metres(0, 0), metres(sign * 5, 0), metres(sign * 10, 10), metres(sign * 25, 12)});
    require(std::abs(sBend.angle()) > 80, "an S-bend must not soften the corner");
  }
}

void tJunctionTests()
{
  for (double sign : {-1.0, 1.0})
  {
    Route t({metres(0, 0), metres(sign * 59, 81)});
    t.candidates.candidates = {{sign * 36, Segment(0, 2, 5, true), HighwayClass::LivingStreet, false},
                              {-sign * 138, Segment(0, 2, 4, false), HighwayClass::LivingStreet, false}};
    std::sort(t.candidates.candidates.begin(), t.candidates.candidates.end(),
              [](auto const & a, auto const & b) { return a.m_angle < b.m_angle; });
    require(t.direction() == (sign < 0 ? CarDirection::TurnSlightLeft : CarDirection::TurnSlightRight),
            "angled T-junction needs an instruction in either direction");
    auto & incoming = t.segments[0];
    auto const originalRange = incoming.m_segmentRange;
    incoming.m_segmentRange = SegmentRange(FeatureID(0, 2), 3, 3, true,
                                          incoming.m_path.front().GetPoint(), incoming.m_path.back().GetPoint());
    require(t.direction() == CarDirection::None, "continuing on the same feature should keep the old suppression");
    incoming.m_segmentRange = originalRange;
    t.segments[1].m_isLink = true;
    require(t.direction() == CarDirection::None, "link roads should keep their existing rules");
    t.segments[1].m_isLink = false;
    for (auto & c : t.candidates.candidates)
      if (c.m_segment.GetSegmentIdx() == 4) c.m_segment = Segment(0, 3, 4, false);
    require(t.direction() == CarDirection::None, "ordinary continuation past a rear branch stays silent");
    t.candidates.candidates.erase(t.candidates.candidates.begin() + 1, t.candidates.candidates.end());
    t.candidates.candidates[0] = {sign * 36, Segment(0, 2, 5, true), HighwayClass::LivingStreet, false};
    require(t.direction() == CarDirection::None, "a road bend without a choice stays silent");
  }
}
}
int main()
{
  entranceTests();
  tJunctionTests();
  std::puts("Organic Maps entrance and T-junction regressions passed");
}
