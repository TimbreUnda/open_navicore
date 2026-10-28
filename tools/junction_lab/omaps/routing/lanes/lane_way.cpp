#include "lane_way.hpp"

#include "base/assert.hpp"

#include <sstream>

namespace routing::turns::lanes
{
std::string DebugPrint(LaneWay const laneWay)
{
  // junction lab: C++17 port, `using enum LaneWay` replaced by qualified names.
  switch (laneWay)
  {
  case LaneWay::None: return "None";
  case LaneWay::ReverseLeft: return "ReverseLeft";
  case LaneWay::SharpLeft: return "SharpLeft";
  case LaneWay::Left: return "Left";
  case LaneWay::MergeToLeft: return "MergeToLeft";
  case LaneWay::SlightLeft: return "SlightLeft";
  case LaneWay::Through: return "Through";
  case LaneWay::SlightRight: return "SlightRight";
  case LaneWay::MergeToRight: return "MergeToRight";
  case LaneWay::Right: return "Right";
  case LaneWay::SharpRight: return "SharpRight";
  case LaneWay::ReverseRight: return "ReverseRight";
  case LaneWay::Count: return "Count";
  default:
    ASSERT_FAIL("Unsupported value: " + std::to_string(static_cast<std::uint8_t>(laneWay)));
    return "Unsupported";
  }
}

std::string DebugPrint(LaneWays const & laneWays)
{
  std::stringstream out;
  out << "LaneWays: [";
  std::uint8_t const waysCount = laneWays.m_laneWays.count();
  std::uint8_t waysPrinted = 0;
  for (std::size_t i = 0; i < laneWays.m_laneWays.size(); ++i)
  {
    if (laneWays.m_laneWays.test(i))
    {
      out << DebugPrint(static_cast<LaneWay>(i));
      if (waysPrinted < waysCount - 1)
        out << ", ";
      waysPrinted++;
    }
  }
  out << "]";
  return out.str();
}
}  // namespace routing::turns::lanes
