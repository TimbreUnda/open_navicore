// Shim for Organic Maps routing_common/num_mwm_id.hpp. Every OsmAnd road is in mwm 0.
#pragma once

#include "platform/country_file.hpp"

#include <cstdint>
#include <limits>

namespace routing
{
using NumMwmId = std::uint16_t;
NumMwmId constexpr kFakeNumMwmId = std::numeric_limits<NumMwmId>::max();

class NumMwmIds
{
public:
  NumMwmId GetId(platform::CountryFile const &) const { return 0; }
};
}  // namespace routing
