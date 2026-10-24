#include "ch_planner.h"

#include "ch/ch_sidecar.h"

#include "routeResultPreparation.h"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>

namespace navicore_osmand {
namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point started)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

std::string base_name(const std::string& path)
{
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

class ChPlanner::Impl {
public:
    std::unique_ptr<onc_ch::Sidecar> sidecar;
    std::unique_ptr<onc_ch::Router> router;
    std::string description;
};

ChPlanner::ChPlanner(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ChPlanner::~ChPlanner() = default;

std::unique_ptr<ChPlanner> ChPlanner::open(const std::string& sidecar_path,
                                           const std::vector<std::string>& routable_maps,
                                           std::string& error)
{
    std::unique_ptr<Impl> impl(new Impl);
    impl->sidecar = onc_ch::Sidecar::open(sidecar_path.c_str(), error);
    if (!impl->sidecar)
        return nullptr;
    // The roads are identified by OBF route ids, so the sidecar is only good for
    // the maps it was built from: same names and sizes, no more and no fewer.
    std::map<std::string, std::uint64_t> expected;
    for (const std::string& path : routable_maps) {
        struct stat info {};
        if (::stat(path.c_str(), &info) != 0) {
            error = "cannot stat " + path;
            return nullptr;
        }
        expected[base_name(path)] = static_cast<std::uint64_t>(info.st_size);
    }
    std::map<std::string, std::uint64_t> built;
    for (std::size_t i = 0; i < impl->sidecar->map_count(); ++i) {
        onc_ch::SidecarMap entry;
        if (!impl->sidecar->map(i, entry)) { error = "cannot read the sidecar's map list"; return nullptr; }
        built[std::string(entry.name, strnlen(entry.name, sizeof(entry.name)))] = entry.size;
    }
    if (built != expected) {
        for (const auto& kv : expected) {
            const auto it = built.find(kv.first);
            if (it == built.end()) { error = "sidecar was not built from " + kv.first; return nullptr; }
            if (it->second != kv.second) { error = "sidecar is stale for " + kv.first; return nullptr; }
        }
        error = "sidecar covers maps that are not loaded";
        return nullptr;
    }
    impl->router.reset(new onc_ch::Router(*impl->sidecar));
    char line[128];
    std::snprintf(line, sizeof(line), "nodes=%u edges=%u roads=%u maps=%zu", impl->sidecar->nodes(),
                  impl->sidecar->real_edges(), impl->sidecar->header().roads, built.size());
    impl->description = line;
    return std::unique_ptr<ChPlanner>(new ChPlanner(std::move(impl)));
}

std::string ChPlanner::describe() const { return impl_->description; }

ChPlanner::Segments ChPlanner::route(RoutePlannerFrontEnd& front_end, const SHARED_PTR<RoutingContext>& context,
                                     int start_x, int start_y, int end_x, int end_y, std::string& why,
                                     double start_heading_degrees)
{
    timing_ = Timing();
    why.clear();
    auto step = Clock::now();
    std::vector<onc_ch::RoadRun> runs;
    onc_ch::u32 cost = 0;
    const auto status = impl_->router->route(start_x, start_y, end_x, end_y, runs, cost,
                                             start_heading_degrees);
    timing_.search_ms = ms_since(step);
    timing_.settled = impl_->router->settled();
    timing_.base_edges = impl_->router->edges().size();
    timing_.road_runs = runs.size();
    if (status == onc_ch::Router::Status::NoRoute) { why = "no_route_in_sidecar"; return {}; }
    if (status == onc_ch::Router::Status::Inconsistent) { why = "sidecar_inconsistent"; return {}; }
    if (runs.empty()) { why = "too_short_for_sidecar"; return {}; }

    // Each stretch needs its RouteDataObject: loading the tile at a point of the road
    // yields the road, found by its id (the way HHRoutePlanner finds its network roads).
    step = Clock::now();
    Segments results;
    results.reserve(runs.size());
    for (const onc_ch::RoadRun& run : runs) {
        SHARED_PTR<RouteDataObject> road;
        for (const auto& segment : context->loadRouteSegment(run.anchor_x, run.anchor_y)) {
            if (segment && segment->getRoad()->getId() == run.road) {
                road = segment->getRoad();
                break;
            }
        }
        const int points = road ? static_cast<int>(road->pointsX.size()) : 0;
        if (!road || run.start < 0 || run.end < 0 || run.start >= points || run.end >= points) {
            why = "road_missing_in_maps";
            return {};
        }
        results.push_back(std::make_shared<RouteSegmentResult>(road, run.start, run.end));
    }
    timing_.roads_ms = ms_since(step);

    step = Clock::now();
    prepareResult(context.get(), results);
    front_end.makeStartEndPointsPrecise(context.get(), results, start_x, start_y, end_x, end_y);
    timing_.prepare_ms = ms_since(step);
    return results;
}

} // namespace navicore_osmand
