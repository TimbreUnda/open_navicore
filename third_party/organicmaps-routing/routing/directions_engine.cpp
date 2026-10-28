#include "routing/directions_engine.hpp"

// junction lab: trimmed Organic Maps routing/directions_engine.cpp, see the header.
// MakeTurnAnnotation is the upstream loop unchanged except that it takes the
// IRoutingResult instead of building one from route edges, and the size CHECKs on
// route edges are expressed on the loaded segments.

#include "routing/turns.hpp"

namespace routing
{
using namespace routing::turns;

DirectionsEngine::DirectionsEngine(MwmDataSource & dataSource, std::shared_ptr<NumMwmIds> numMwmIds)
  : m_dataSource(dataSource)
  , m_numMwmIds(numMwmIds)
{
  CHECK(m_numMwmIds, ());
}

// Normally loadedSegments structure is:
// - Start point. Fake loop LoadedPathSegment with 1 segment of zero length.
// - Straight jump from start point to the beginning of real route. LoadedPathSegment with 1 segment.
// - Real route. N LoadedPathSegments, each with arbitrary amount of segments. N >= 1.
// - Straight jump from the end of real route to finish point. LoadedPathSegment with 1 segment.
// - Finish point. Fake loop LoadedPathSegment with 1 segment of zero length.
// So minimal amount of segments is 5.

// Resulting structure of turnsDir:
// No Turn for 0th segment (no ingoing). m_index == 0.
// No Turn for 1st segment (ingoing fake loop) - at start point. m_index == 1.
// No Turn for 2nd (ingoing == jump) - at beginning of real route. m_index == 2.
// Possible turn for next N-1 segments. m_index >= 3.
// No Turn for (2 + N + 1)th segment (outgoing jump) - at finish point. m_index = 3 + M.
// No Turn for (2 + N + 2)th segment (outgoing fake loop) - at finish point. m_index == 4 + M.
// Added ReachedYourDestination - at finish point. m_index == 4 + M.
// Where M - total amount of all segments from all LoadedPathSegments (size of |segments|).
// So minimum m_index of ReachedYourDestination is 5 (real route with single segment),
// and minimal |turnsDir| is - single ReachedYourDestination with m_index == 5.

void DirectionsEngine::MakeTurnAnnotation(IRoutingResult const & result, std::vector<RouteSegment> & routeSegments)
{
  CHECK_GREATER_OR_EQUAL(result.GetSegments().size(), 2, ());

  RoutingSettings const vehicleSettings = GetRoutingSettings(m_vehicleType);

  auto const & loadedSegments = result.GetSegments();  // the same as m_pathSegments

  // First point of first loadedSegment is ignored. This is the reason for:
  // ASSERT_EQUAL(loadedSegments.front().m_path.back(), loadedSegments.front().m_path.front(), ());

  size_t skipTurnSegments = 0;
  for (size_t idxLoadedSegment = 0; idxLoadedSegment < loadedSegments.size(); ++idxLoadedSegment)
  {
    auto const & loadedSegment = loadedSegments[idxLoadedSegment];

    CHECK(loadedSegment.IsValid(), ());
    CHECK_GREATER_OR_EQUAL(loadedSegment.m_segments.size(), 1, ());
    CHECK_EQUAL(loadedSegment.m_segments.size() + 1, loadedSegment.m_path.size(), ());

    auto rni = loadedSegment.m_roadNameInfo;
    ASSERT_EQUAL(rni.m_isLink, loadedSegment.m_isLink, ());

    for (size_t i = 0; i < loadedSegment.m_segments.size() - 1; ++i)
    {
      auto const & junction = loadedSegment.m_path[i + 1];
      routeSegments.emplace_back(loadedSegment.m_segments[i], TurnItem(), junction, rni);
      if (i == 0)
        rni.ClearUselessStringsForTailSegments();
    }

    // For the last segment of current loadedSegment put info about turn
    // from current loadedSegment to the next one.
    TurnItem turnItem;
    if (skipTurnSegments == 0)
    {
      turnItem.m_index = static_cast<uint32_t>(routeSegments.size() + 1);
      skipTurnSegments = GetTurnDirection(result, idxLoadedSegment + 1, *m_numMwmIds, vehicleSettings, turnItem);
    }
    else
      --skipTurnSegments;

    routeSegments.emplace_back(loadedSegment.m_segments.back(), turnItem, loadedSegment.m_path.back(), rni);
  }

  // ASSERT_EQUAL(routeSegments.front().GetJunction(), result.GetStartPoint(), ());
  // ASSERT_EQUAL(routeSegments.back().GetJunction(), result.GetEndPoint(), ());

  FixupTurns(routeSegments);
}

}  // namespace routing
