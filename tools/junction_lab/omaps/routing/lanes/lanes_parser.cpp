#include "lanes_parser.hpp"

#include "base/string_utils.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace routing::turns::lanes
{
namespace
{
std::uint8_t constexpr kLaneWayNamesCount = static_cast<std::uint8_t>(LaneWay::Count) + 4;

/**
 * The order is important. Starting with the most frequent tokens according to
 * taginfo.openstreetmap.org we minimize the number of the comparisons in ParseSingleLane().
 *
 * A `none` lane can be represented either as "none" or as "". That means both "none" and ""
 * should be considered names, even though they refer to the same thing. As a result,
 * `LaneWay::None` appears twice in this array, which is one longer than the number of
 * enum values.
 */
std::array<std::pair<LaneWay, std::string_view>, kLaneWayNamesCount> constexpr g_laneWayNames{{
    {LaneWay::None, ""},
    {LaneWay::Through, "through"},
    {LaneWay::Left, "left"},
    {LaneWay::Right, "right"},
    {LaneWay::None, "none"},
    {LaneWay::SharpLeft, "sharp_left"},
    {LaneWay::SlightLeft, "slight_left"},
    {LaneWay::MergeToRight, "merge_to_right"},
    {LaneWay::MergeToLeft, "merge_to_left"},
    {LaneWay::SlightRight, "slight_right"},
    {LaneWay::SharpRight, "sharp_right"},
    {LaneWay::ReverseLeft, "reverse"},
    {LaneWay::Right,
     "next_right"},  // "next_right" means "turn right, not in the first intersection but the one after that".
    {LaneWay::Through, "slide_left"},  // "slide_left" means "move a bit left within the lane".
    {LaneWay::Through, "slide_right"}  // "slide_right" means "move a bit right within the lane".
}};

// junction lab: C++17 port. Upstream takes a ranges view; here the token is a
// std::string that the caller already stripped of ASCII spaces and lower-cased.
bool ParseSingleLane(std::string const & laneWayRange, LaneWay & laneWay)
{
  auto const it = std::find_if(g_laneWayNames.begin(), g_laneWayNames.end(),
                               [&laneWayRange](auto const & pair) { return laneWayRange == pair.second; });
  if (it != g_laneWayNames.end())
  {
    laneWay = it->first;
    return true;
  }
  return false;
}

// junction lab: C++17 stand-in for `text | std::views::split(delimiter)`: every
// delimiter ends a part, so "a|" gives {"a", ""} and "" gives {""}.
std::vector<std::string_view> Split(std::string_view text, char delimiter)
{
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (true)
  {
    std::size_t const end = text.find(delimiter, start);
    if (end == std::string_view::npos)
    {
      parts.push_back(text.substr(start));
      return parts;
    }
    parts.push_back(text.substr(start, end - start));
    start = end + 1;
  }
}

}  // namespace

LanesInfo ParseLanes(std::string_view lanesString)
{
  if (lanesString.empty())
    return {};

  LanesInfo lanes;
  for (std::string_view const laneInfo : Split(lanesString, '|'))
  {
    LaneInfo lane;
    if (laneInfo.empty())
      lane.laneWays.Add(LaneWay::None);
    else
    {
      for (std::string_view const laneWay : Split(laneInfo, ';'))
      {
        auto way = LaneWay::None;
        // Not <cctype>: its functions have undefined behavior for the non-ASCII bytes of UTF-8 text.
        std::string laneWayProcessed;
        for (char const c : laneWay)
          if (!strings::IsASCIISpace(c))
            laneWayProcessed.push_back(strings::AsciiToLower(c));
        if (!ParseSingleLane(laneWayProcessed, way))
          return {};
        lane.laneWays.Add(way);
        if (way == LaneWay::ReverseLeft)
          lane.laneWays.Add(LaneWay::ReverseRight);
      }
    }

    lanes.push_back(lane);
  }
  return lanes;
}
}  // namespace routing::turns::lanes
