#include "ch_planner.h"

#include "ch/ch_sidecar.h"

#include "binaryRoutePlanner.h"
#include "generalRouter.h"
#include "routeResultPreparation.h"
#include "routeSegment.h"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

// Start and end are snapped onto roads as the core's own routers do (the nearest
// segment of each nearby road), then follow the road in its travel direction to
// the first junction the sidecar knows. Snapping straight to the nearest junctions
// instead put a point between the two carriageways of a dual carriageway onto
// whichever side had a junction nearby, and a route started the wrong way round
// (2026-10-27, Khreshchatyk).
constexpr double kSnapMsPerMetre = 120.0;      // the distance to the road, as the node snap
constexpr double kReversePenaltyMs = 60000.0;  // starting against the car's heading
constexpr double kMaxSnapMetres = 1000.0;
constexpr double kMaxApproachMetres = 5000.0;  // along a road to its first junction
constexpr std::size_t kSnapRoads = 8;          // nearest roads tried at each end

double metres_between(int x1, int y1, int x2, int y2)
{
    return std::hypot(static_cast<double>(x2) - x1, static_cast<double>(y2) - y1) *
           onc_ch::metres_per_unit((static_cast<double>(y1) + y2) / 2.0);
}

// Degrees clockwise from north (31-bit y grows southwards).
double bearing_between(int x1, int y1, int x2, int y2)
{
    const double degrees = std::atan2(static_cast<double>(x2) - x1, static_cast<double>(y1) - y2) *
                           180.0 / M_PI;
    return degrees < 0.0 ? degrees + 360.0 : degrees;
}

// The point projected onto the nearest segment (lo, lo + 1) of one road.
struct RoadSnap {
    SHARED_PTR<RouteDataObject> road;
    int lo = 0, hi = 0;
    int x = 0, y = 0;
    double metres = 0;
    int oneway = 0;   // the router's: 1 forward (rising point index) only, -1 backward only
};

std::vector<RoadSnap> snap_roads(RoutingContext* context, int x, int y)
{
    std::vector<RoadSnap> snaps;
    const auto nearest = findRouteSegment(x, y, context);
    if (!nearest) return snaps;
    std::vector<SHARED_PTR<RouteSegmentPoint>> points{nearest};
    points.insert(points.end(), nearest->others.begin(), nearest->others.end());
    const auto& router = context->config->router;
    for (const auto& point : points) {
        if (snaps.size() >= kSnapRoads) break;
        if (!point || !point->road || !router->acceptLine(point->road)) continue;
        RoadSnap snap;
        snap.road = point->road;
        snap.lo = std::min<int>(point->getSegmentStart(), point->getSegmentEnd());
        snap.hi = std::max<int>(point->getSegmentStart(), point->getSegmentEnd());
        snap.x = point->preciseX;
        snap.y = point->preciseY;
        snap.metres = metres_between(x, y, snap.x, snap.y);
        snap.oneway = router->isOneWay(point->road);
        if (snap.hi == snap.lo || snap.hi >= static_cast<int>(snap.road->pointsX.size()) ||
            snap.metres > kMaxSnapMetres)
            continue;
        snaps.push_back(snap);
    }
#ifdef ONC_ROUTE_DIAGNOSTICS
    // Causal probe for the route survey, never compiled into the backend.
    const char* nearest_only = std::getenv("ROUTE_BENCH_NEAREST_ONLY");
    if (nearest_only && std::strcmp(nearest_only, "1") == 0 && snaps.size() > 1)
        snaps.resize(1);
#endif
    return snaps;
}

bool may_travel(const RoadSnap& snap, bool forward)
{
    return forward ? snap.oneway >= 0 : snap.oneway <= 0;
}

double speed_of(RoutingContext* context, const RoadSnap& snap, bool forward)
{
    return std::max(1.0, context->config->router->defineRoutingSpeed(snap.road, forward));
}

// One way onto (start) or off (end) the hierarchy along a road: the stretch between
// the projected point and a junction, in travel order, and what driving it costs.
struct Approach {
    SHARED_PTR<RouteDataObject> road;
    int start = 0, end = 0;   // point indexes in travel order
    double cost_ms = 0;
    std::vector<onc_ch::u32> nodes;
};

// From the projection along rising (up) or falling point indexes to the first
// point that is a junction of the sidecar.
bool walk_to_node(onc_ch::Sidecar& sidecar, const RoadSnap& snap, bool up, int& index,
                  double& metres, std::vector<onc_ch::u32>& nodes)
{
    const auto& road = snap.road;
    const int count = static_cast<int>(road->pointsX.size());
    int x = snap.x, y = snap.y;
    metres = 0;
    for (index = up ? snap.hi : snap.lo; index >= 0 && index < count; index += up ? 1 : -1) {
        const int px = static_cast<int>(road->pointsX[index]);
        const int py = static_cast<int>(road->pointsY[index]);
        metres += metres_between(x, y, px, py);
        x = px;
        y = py;
        if (metres > kMaxApproachMetres) return false;
        sidecar.nodes_at(px, py, nodes);
        if (!nodes.empty()) return true;
    }
    return false;
}

// start: drive from the point to a junction; otherwise arrive at the point from one.
std::vector<Approach> approaches(onc_ch::Sidecar& sidecar, RoutingContext* context,
                                 const std::vector<RoadSnap>& snaps, bool start,
                                 double heading_degrees)
{
    std::vector<Approach> output;
    for (const RoadSnap& snap : snaps) {
        for (const bool forward : {true, false}) {
            if (!may_travel(snap, forward)) continue;
            // A start drives on in the travel direction; an end is reached from behind.
            const bool up = start ? forward : !forward;
            Approach approach;
            int index = 0;
            double metres = 0;
            if (!walk_to_node(sidecar, snap, up, index, metres, approach.nodes)) continue;
            approach.road = snap.road;
            if (start) {
                approach.start = forward ? snap.lo : snap.hi;
                approach.end = index;
            } else {
                approach.start = index;
                approach.end = forward ? snap.hi : snap.lo;
            }
            approach.cost_ms = kSnapMsPerMetre * snap.metres +
                               1000.0 * metres / speed_of(context, snap, forward);
            if (start && heading_degrees >= 0.0) {
                const auto& road = snap.road;
                const int from = forward ? snap.lo : snap.hi, to = forward ? snap.hi : snap.lo;
                const double travel = bearing_between(road->pointsX[from], road->pointsY[from],
                                                      road->pointsX[to], road->pointsY[to]);
                if (std::fabs(std::remainder(travel - heading_degrees, 360.0)) > 120.0)
                    approach.cost_ms += kReversePenaltyMs;
            }
            output.push_back(std::move(approach));
        }
    }
    return output;
}

// Start and end on one road, the end ahead in an allowed direction: the road
// itself, without a junction in between. False when there is no such run.
bool same_road_run(RoutingContext* context, const RoadSnap& from, const RoadSnap& to,
                   double heading_degrees, Approach& run)
{
    if (from.road->getId() != to.road->getId()) return false;
    const auto& road = from.road;
    bool found = false;
    for (const bool forward : {true, false}) {
        if (!may_travel(from, forward)) continue;
        const int a = forward ? from.lo : from.hi;   // the segment end behind the start
        const int b = forward ? to.lo : to.hi;       // the one behind the end
        const bool same_segment = from.lo == to.lo;
        const bool ahead = same_segment
            ? metres_between(road->pointsX[a], road->pointsY[a], to.x, to.y) >
              metres_between(road->pointsX[a], road->pointsY[a], from.x, from.y)
            : (forward ? to.lo > from.lo : to.lo < from.lo);
        if (!ahead) continue;
        double metres = 0;
        if (same_segment) {
            metres = metres_between(from.x, from.y, to.x, to.y);
        } else {
            int x = from.x, y = from.y;
            for (int i = forward ? from.hi : from.lo; ; i += forward ? 1 : -1) {
                metres += metres_between(x, y, road->pointsX[i], road->pointsY[i]);
                x = road->pointsX[i];
                y = road->pointsY[i];
                if (i == b) break;
            }
            metres += metres_between(x, y, to.x, to.y);
        }
        double cost = kSnapMsPerMetre * (from.metres + to.metres) +
                      1000.0 * metres / speed_of(context, from, forward);
        if (heading_degrees >= 0.0) {
            const int c = forward ? from.hi : from.lo;
            const double travel = bearing_between(road->pointsX[a], road->pointsY[a],
                                                  road->pointsX[c], road->pointsY[c]);
            if (std::fabs(std::remainder(travel - heading_degrees, 360.0)) > 120.0)
                cost += kReversePenaltyMs;
        }
        if (!found || cost < run.cost_ms) {
            run.road = road;
            run.start = a;
            run.end = forward ? to.hi : to.lo;
            run.cost_ms = cost;
            found = true;
        }
    }
    return found;
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
    const auto progress = context->progress;
    const auto cancelled = [&progress] { return progress && progress->isCancelled(); };
    onc_ch::Sidecar& sidecar = *impl_->sidecar;

    // Onto the roads, then along them to the hierarchy.
    const auto from_roads = snap_roads(context.get(), start_x, start_y);
    const auto to_roads = snap_roads(context.get(), end_x, end_y);
    const auto starts = approaches(sidecar, context.get(), from_roads, true, start_heading_degrees);
    const auto ends = approaches(sidecar, context.get(), to_roads, false, -1.0);
    Approach direct;
    bool has_direct = false;
    for (const RoadSnap& from : from_roads)
        for (const RoadSnap& to : to_roads) {
            Approach run;
            if (same_road_run(context.get(), from, to, start_heading_degrees, run) &&
                (!has_direct || run.cost_ms < direct.cost_ms)) {
                direct = run;
                has_direct = true;
            }
        }
    timing_.snap_ms = ms_since(step);

#ifdef ONC_ROUTE_DIAGNOSTICS
    // Standalone route survey only; absent from the installed backend.
    std::size_t start_nodes = 0, end_nodes = 0;
    for (const auto& approach : starts) start_nodes += approach.nodes.size();
    for (const auto& approach : ends) end_nodes += approach.nodes.size();
    std::fprintf(stderr, "ch_diag start_roads=%zu end_roads=%zu start_approaches=%zu end_approaches=%zu "
        "start_nodes=%zu end_nodes=%zu direct=%d snap_ms=%.1f\n", from_roads.size(), to_roads.size(),
        starts.size(), ends.size(), start_nodes, end_nodes, has_direct ? 1 : 0, timing_.snap_ms);
    const auto print_snaps = [](const char* side, const std::vector<RoadSnap>& snaps) {
        for (const auto& snap : snaps)
            std::fprintf(stderr, "ch_snap side=%s road=%lld gap_m=%.2f lo=%d hi=%d oneway=%d\n",
                side, static_cast<long long>(snap.road->getId()), snap.metres, snap.lo, snap.hi, snap.oneway);
    };
    print_snaps("start", from_roads);
    print_snaps("end", to_roads);
#endif

    std::vector<onc_ch::RoadRun> runs;
    onc_ch::u32 cost = 0;
    const Approach* first = nullptr;
    const Approach* last = nullptr;
    bool use_direct = false;
    if (!starts.empty() && !ends.empty()) {
        timing_.road_snap = true;
        // Every junction node of every approach, at that approach's cost (the
        // cheapest approach where two reach one node).
        using Seed = onc_ch::BasicQuery<onc_ch::SidecarStore>::Seed;
        std::map<onc_ch::u32, const Approach*> source_of, target_of;
        const auto add = [](std::map<onc_ch::u32, const Approach*>& owner,
                            const std::vector<Approach>& from) {
            for (const Approach& approach : from)
                for (const onc_ch::u32 node : approach.nodes) {
                    auto it = owner.find(node);
                    if (it == owner.end() || approach.cost_ms < it->second->cost_ms)
                        owner[node] = &approach;
                }
        };
        add(source_of, starts);
        add(target_of, ends);
        std::vector<Seed> sources, targets;
        for (const auto& kv : source_of)
            sources.push_back({kv.first, static_cast<onc_ch::u32>(std::lround(kv.second->cost_ms))});
        for (const auto& kv : target_of)
            targets.push_back({kv.first, static_cast<onc_ch::u32>(std::lround(kv.second->cost_ms))});
        step = Clock::now();
        onc_ch::u32 from = 0, to = 0;
        const auto status = impl_->router->route_seeded(sources, targets, runs, cost, cancelled, from, to);
        timing_.search_ms = ms_since(step);
        timing_.settled = impl_->router->settled();
        if (status == onc_ch::Router::Status::Cancelled) { why = "cancelled"; return {}; }
        if (status == onc_ch::Router::Status::Inconsistent) { why = "sidecar_inconsistent"; return {}; }
        const bool found = status == onc_ch::Router::Status::Ok;
        if (found) {
            first = source_of[from];
            last = target_of[to];
        }
        use_direct = has_direct && (!found || direct.cost_ms <= cost);
        if (!found && !use_direct) { why = "no_route_in_sidecar"; return {}; }
    } else if (has_direct) {
        use_direct = true;
    } else {
        // No road near enough to snap onto: the nearest junctions, as before.
        step = Clock::now();
        const auto status = impl_->router->route(start_x, start_y, end_x, end_y, runs, cost,
                                                 start_heading_degrees, cancelled);
        timing_.search_ms = ms_since(step);
        timing_.settled = impl_->router->settled();
        if (status == onc_ch::Router::Status::NoRoute) { why = "no_route_in_sidecar"; return {}; }
        if (status == onc_ch::Router::Status::Inconsistent) { why = "sidecar_inconsistent"; return {}; }
        if (status == onc_ch::Router::Status::Cancelled) { why = "cancelled"; return {}; }
        if (runs.empty()) { why = "too_short_for_sidecar"; return {}; }
    }
    timing_.base_edges = use_direct ? 0 : impl_->router->edges().size();
    timing_.road_runs = use_direct ? 0 : runs.size();

    Segments results;
    if (use_direct) {
        results.push_back(std::make_shared<RouteSegmentResult>(direct.road, direct.start, direct.end));
        timing_.direct = true;
    } else {
        // Each stretch needs its RouteDataObject: loading the tile at a point of the road
        // yields the road, found by its id (the way HHRoutePlanner finds its network roads).
        step = Clock::now();
        results.reserve(runs.size() + 2);
        if (first && first->start != first->end)
            results.push_back(std::make_shared<RouteSegmentResult>(first->road, first->start, first->end));
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
#ifdef ONC_ROUTE_DIAGNOSTICS
                std::fprintf(stderr, "ch_diag missing_road=%lld from=%d to=%d points=%d anchor=%d,%d\n",
                    static_cast<long long>(run.road), run.start, run.end, points, run.anchor_x, run.anchor_y);
#endif
                why = "road_missing_in_maps";
                return {};
            }
            results.push_back(std::make_shared<RouteSegmentResult>(road, run.start, run.end));
        }
        if (last && last->start != last->end)
            results.push_back(std::make_shared<RouteSegmentResult>(last->road, last->start, last->end));
        timing_.roads_ms = ms_since(step);
        if (results.empty()) { why = "too_short_for_sidecar"; return {}; }
    }

    step = Clock::now();
    prepareResult(context.get(), results);
    front_end.makeStartEndPointsPrecise(context.get(), results, start_x, start_y, end_x, end_y);
#ifdef ONC_ROUTE_DIAGNOSTICS
    if (!results.empty()) {
        const auto& first_result = results.front();
        const auto& last_result = results.back();
        std::fprintf(stderr, "ch_selected start_road=%lld end_road=%lld start_index=%d end_index=%d direct=%d\n",
            static_cast<long long>(first_result->object->getId()),
            static_cast<long long>(last_result->object->getId()), first_result->getStartPointIndex(),
            last_result->getEndPointIndex(), use_direct ? 1 : 0);
    }
#endif
    timing_.prepare_ms = ms_since(step);
    return results;
}

} // namespace navicore_osmand
