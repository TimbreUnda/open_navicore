#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

struct RouteSegmentResult;

namespace navicore_osmand {

// Immutable road/index snapshots at route nodes, owned by one request. These
// are not reusable search-state wrappers or a process-wide decoded-map cache.
using RouteRoads = std::unordered_map<std::uint64_t,
    std::vector<std::shared_ptr<RouteSegmentResult>>>;

inline std::uint64_t route_node_key(std::uint32_t x, std::uint32_t y)
{
    return (static_cast<std::uint64_t>(x) << 32) | y;
}

} // namespace navicore_osmand
