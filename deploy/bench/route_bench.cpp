// Route calculation benchmark for the radio (bench only, not in navigation.ifs).
//
//   route_bench_run MAP ROUTING_XML MODE MEMORY_MB LAT1 LON1 LAT2 LON2 [REPEAT]
//
// MAP is one .obf file or a directory of them (all are opened).
//
// MODE: astar (what the navigation worker did), hh (highway hierarchy),
// ch (contraction hierarchy; ROUTE_BENCH_CH=<prefix> of the .coords/.ch/.meta files),
// compare (hh vs ch on every "lat1 lon1 lat2 lon2" line of the file given as LAT1),
// hhalt (hh with alternatives). Prints wall time per phase and OsmAnd's own
// load/calculate timers and tile counters, one line per run.

// Core and core-legacy both ship a Logging.h; use core-legacy's names only.
#include "binaryRead.h"
#include "commonOsmAndCore.h"
#include "hhRouteDataStructure.h"
#include "hhRoutePlanner.h"
#include "routeCalculationProgress.h"
#include "routePlannerFrontEnd.h"
#include "routeResultPreparation.h"
#include "routeSegmentResult.h"
#include "routingConfiguration.h"
#include "routingContext.h"

#include "../../backends/osmand/ch/ch_query.h"

#include <chrono>
#include <dirent.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point started)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

double route_metres(const std::vector<SHARED_PTR<RouteSegmentResult>>& segments, float* seconds)
{
    double metres = 0.0;
    float time = 0.0f;
    for (const auto& segment : segments) {
        if (!segment) continue;
        metres += segment->distance;
        time += segment->segmentTime;
    }
    if (seconds) *seconds = time;
    return metres;
}

void print_progress(const char* label, RouteCalculationProgress& p)
{
    std::printf("  %s: load=%llums headers=%llums calc=%llums initial=%llums extra=%llums "
                "visited=%d loaded_tiles=%d unloaded=%d reloaded=%d distinct=%d max_loaded=%d\n",
                label, (unsigned long long)p.timeToLoad.GetElapsedMs(), (unsigned long long)p.timeToLoadHeaders.GetElapsedMs(),
                (unsigned long long)p.timeToCalculate.GetElapsedMs(), (unsigned long long)p.timeToFindInitialSegments.GetElapsedMs(),
                (unsigned long long)p.timeExtra.GetElapsedMs(), p.visitedSegments, p.loadedTiles, p.unloadedTiles,
                p.loadedPrevUnloadedTiles, p.distinctLoadedTiles, p.maxLoadedTiles);
}

// ---- contraction-hierarchy mode -------------------------------------------
// Files (prefix in ROUTE_BENCH_CH): <prefix>.coords (graph header + node
// coordinates), <prefix>.ch (hierarchy), <prefix>.meta (edge -> road table).
struct ChData {
    onc_ch::Hierarchy hierarchy;
    std::vector<int32_t> x, y;
    std::vector<uint32_t> from, pf, pt;
    std::vector<int64_t> road;
    std::vector<uint8_t> rev;
};

template <class T>
bool read_array(std::FILE* f, std::vector<T>& v, size_t count)
{
    v.resize(count);
    return count == 0 || std::fread(v.data(), sizeof(T), count, f) == count;
}

bool load_ch(const std::string& prefix, ChData& data, std::string& error)
{
    std::FILE* f = std::fopen((prefix + ".coords").c_str(), "rb");
    char magic[4];
    uint32_t nodes = 0, edges = 0;
    if (!f || std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "CHG1", 4) != 0 ||
        std::fread(&nodes, 4, 1, f) != 1 || std::fread(&edges, 4, 1, f) != 1 ||
        !read_array(f, data.x, nodes) || !read_array(f, data.y, nodes)) {
        if (f) std::fclose(f);
        error = "bad " + prefix + ".coords";
        return false;
    }
    std::fclose(f);
    if (!onc_ch::load_hierarchy((prefix + ".ch").c_str(), data.hierarchy, error)) return false;
    f = std::fopen((prefix + ".meta").c_str(), "rb");
    uint32_t meta_edges = 0;
    if (!f || std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "CHM1", 4) != 0 ||
        std::fread(&meta_edges, 4, 1, f) != 1 || meta_edges != edges ||
        !read_array(f, data.from, edges) || !read_array(f, data.road, edges) ||
        !read_array(f, data.pf, edges) || !read_array(f, data.pt, edges) || !read_array(f, data.rev, edges)) {
        if (f) std::fclose(f);
        error = "bad " + prefix + ".meta";
        return false;
    }
    std::fclose(f);
    return true;
}

uint32_t nearest_node(const ChData& data, int x31, int y31)
{
    uint32_t best = 0;
    int64_t best_d = INT64_MAX;
    for (uint32_t i = 0; i < data.x.size(); ++i) {
        const int64_t dx = (int64_t)data.x[i] - x31, dy = (int64_t)data.y[i] - y31;
        const int64_t d = dx * dx + dy * dy;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

// The k nearest nodes (brute force; a product build uses a grid), nearest first,
// with their distance in metres.  A split junction (turn restrictions) has several
// nodes at one point, some arrival-only and some departure-only, so a route may
// have to start or end at the second or third nearest.
std::vector<std::pair<uint32_t, double>> nearest_nodes(const ChData& data, int x31, int y31, size_t k)
{
    std::vector<std::pair<int64_t, uint32_t>> best;   // (squared distance, node), kept sorted, size <= k
    for (uint32_t i = 0; i < data.x.size(); ++i) {
        const int64_t dx = (int64_t)data.x[i] - x31, dy = (int64_t)data.y[i] - y31;
        const int64_t d = dx * dx + dy * dy;
        if (best.size() == k && d >= best.back().first) continue;
        best.insert(std::upper_bound(best.begin(), best.end(), std::make_pair(d, i)), {d, i});
        if (best.size() > k) best.pop_back();
    }
    // 31-bit units to metres: the circumference over 2^31, shrunk by cos(latitude)
    const double lat = std::atan(std::sinh(M_PI * (1.0 - 2.0 * (double)y31 / 2147483648.0)));
    const double metres_per_unit = 40075016.7 / 2147483648.0 * std::cos(lat);
    std::vector<std::pair<uint32_t, double>> out;
    for (const auto& b : best) out.push_back({b.second, std::sqrt((double)b.first) * metres_per_unit});
    return out;
}

// Cheapest route between the k nearest nodes at each end; the snap distance counts as
// 120 ms per metre (a slow walk), so a node far away does not win on route cost alone.
bool best_route(const ChData& data, onc_ch::Query& query, int sx, int sy, int ex, int ey,
                std::vector<uint32_t>& edges, uint32_t& cost_out)
{
    const auto from = nearest_nodes(data, sx, sy, 4), to = nearest_nodes(data, ex, ey, 4);
    double best = 1e18;
    uint32_t best_from = 0, best_to = 0, best_meet = 0, best_cost = onc_ch::INF;
    for (const auto& s : from) {
        for (const auto& t : to) {
            uint32_t meet = 0;
            const uint32_t cost = query.run(s.first, t.first, meet);
            if (cost == onc_ch::INF) continue;
            const double total = cost + 120.0 * (s.second + t.second);
            if (total < best) { best = total; best_from = s.first; best_to = t.first; best_meet = meet; best_cost = cost; }
        }
    }
    if (best_cost == onc_ch::INF) return false;
    uint32_t meet = 0;
    query.run(best_from, best_to, meet);   // path_edges reads the last run
    cost_out = best_cost;
    return query.path_edges(best_from, best_to, meet, edges);
}

// One stretch of one road, in travel direction.
struct RoadRun {
    int64_t road;
    int start, end;
    int anchor_x, anchor_y;   // a point on the road, used to load it
};

std::vector<RoadRun> merge_edges(const ChData& data, const std::vector<uint32_t>& edges)
{
    std::vector<RoadRun> runs;
    for (uint32_t e : edges) {
        if (data.road[e] < 0) continue;   // zero-length junction transition (turn restrictions)
        const int start = data.rev[e] ? (int)data.pt[e] : (int)data.pf[e];
        const int end = data.rev[e] ? (int)data.pf[e] : (int)data.pt[e];
        if (!runs.empty() && runs.back().road == data.road[e] && runs.back().end == start &&
            ((runs.back().end > runs.back().start) == (end > start))) {
            runs.back().end = end;
        } else {
            runs.push_back({data.road[e], start, end, data.x[data.from[e]], data.y[data.from[e]]});
        }
    }
    return runs;
}

} // namespace

extern "C" __attribute__((visibility("default")))
int route_bench_main(int argc, char** argv)
{
    if (argc < 9) {
        std::fprintf(stderr, "usage: %s MAP ROUTING_XML astar|hh|hhalt MEMORY_MB "
                             "LAT1 LON1 LAT2 LON2 [REPEAT]\n", argv[0]);
        return 2;
    }
    const std::string map = argv[1];
    const std::string mode = argv[3];
    const int memory_mb = std::atoi(argv[4]);
    const double lat1 = std::atof(argv[5]), lon1 = std::atof(argv[6]);
    const double lat2 = std::atof(argv[7]), lon2 = std::atof(argv[8]);
    const int repeat = argc > 9 ? std::atoi(argv[9]) : 1;

    auto started = Clock::now();
    auto builder = parseRoutingConfigurationFromXml(argv[2], "");
    if (!builder) { std::fprintf(stderr, "routing.xml parse failed\n"); return 1; }
    const double parse_ms = ms_since(started);
    started = Clock::now();
    // MAP is one .obf or a directory: every .obf in it is opened, like the
    // navigation worker does, so a route can cross regions.
    std::vector<std::string> map_files;
    struct stat map_info {};
    if (::stat(map.c_str(), &map_info) == 0 && S_ISDIR(map_info.st_mode)) {
        if (DIR* dir = ::opendir(map.c_str())) {
            while (const dirent* entry = ::readdir(dir)) {
                const std::string name = entry->d_name;
                if (name.size() > 4 && name[0] != '.' && name.compare(name.size() - 4, 4, ".obf") == 0)
                    map_files.push_back(map + "/" + name);
            }
            ::closedir(dir);
        }
    } else {
        map_files.push_back(map);
    }
    size_t routing_indexes = 0, hh_indexes = 0, opened = 0;
    for (const auto& path : map_files) {
        const auto file = initBinaryMapFile(path, false, true);
        if (!file || file->routingIndexes.empty()) continue;
        ++opened;
        routing_indexes += file->routingIndexes.size();
        hh_indexes += file->hhIndexes.size();
    }
    if (opened == 0) {
        std::fprintf(stderr, "map has no routing index\n");
        return 1;
    }
    const double open_ms = ms_since(started);
    Dl_info core{};
    if (dladdr(reinterpret_cast<void*>(&parseRoutingConfigurationFromXml), &core) && core.dli_fname)
        std::printf("core: %s\n", core.dli_fname);
    std::printf("setup: parse_xml=%.0fms open_map=%.0fms files=%zu routing_indexes=%zu hh_indexes=%zu\n",
                parse_ms, open_ms, opened, routing_indexes, hh_indexes);

    if (mode == "compare") {
        // argv[5] is a text file of "lat1 lon1 lat2 lon2" lines.  Each pair is routed with
        // HH and with CH; the routes are compared by time, length and shared road.
        const char* prefix = std::getenv("ROUTE_BENCH_CH");
        if (!prefix) { std::fprintf(stderr, "ROUTE_BENCH_CH=<prefix> is required\n"); return 2; }
        ChData ch;
        std::string error;
        if (!load_ch(prefix, ch, error)) { std::fprintf(stderr, "ch load failed: %s\n", error.c_str()); return 1; }
        onc_ch::Query query(ch.hierarchy);
        std::FILE* pairs = std::fopen(argv[5], "r");
        if (!pairs) { std::fprintf(stderr, "cannot open pairs file\n"); return 1; }
        std::vector<double> time_ratio, dist_ratio, overlap_hh, overlap_ch, hh_ms_all, ch_ms_all;
        double a, b, c, d;
        int index = 0;
        auto road_metres = [](const std::vector<SHARED_PTR<RouteSegmentResult>>& segments) {
            std::map<int64_t, double> by_road;
            for (const auto& s : segments) if (s) by_road[s->object->getId()] += s->distance;
            return by_road;
        };
        while (std::fscanf(pairs, "%lf %lf %lf %lf", &a, &b, &c, &d) == 4) {
            const int sx = get31TileNumberX(b), sy = get31TileNumberY(a);
            const int ex = get31TileNumberX(d), ey = get31TileNumberY(c);
            MAP_STR_STR parameters;
            std::vector<int> ix, iy;
            // reference router: HH, or plain A* with ROUTE_BENCH_REFERENCE=astar (the ground truth
            // for the unit's routing.xml; HH shortcut weights come from the map's build)
            const bool plain_astar = std::getenv("ROUTE_BENCH_REFERENCE") &&
                                     std::strcmp(std::getenv("ROUTE_BENCH_REFERENCE"), "astar") == 0;
            HHRoutingConfig* hh = nullptr;
            if (!plain_astar) {
                hh = HHRoutingConfig::astar(0);
                hh->calcDetailed(HHRoutingConfig::CALCULATE_ALL_DETAILED);
            }
            RoutePlannerFrontEnd hh_front = hh ? RoutePlannerFrontEnd(hh) : RoutePlannerFrontEnd();
            auto hh_context = hh_front.buildRoutingContext(builder->build("car", memory_mb, parameters));
            hh_context->progress = std::make_shared<RouteCalculationProgress>();
            auto t = Clock::now();
            auto hh_segments = hh_front.searchRoute(hh_context, sx, sy, ex, ey, ix, iy, nullptr);
            const double hh_ms = ms_since(t);
            float hh_s = 0.0f;
            const double hh_m = route_metres(hh_segments, &hh_s);
            // CH
            RoutePlannerFrontEnd ch_front;
            auto ch_context = ch_front.buildRoutingContext(builder->build("car", memory_mb, parameters));
            ch_context->progress = std::make_shared<RouteCalculationProgress>();
            t = Clock::now();
            uint32_t ch_cost = 0;
            std::vector<uint32_t> edges;
            std::vector<SHARED_PTR<RouteSegmentResult>> ch_segments;
            if (best_route(ch, query, sx, sy, ex, ey, edges, ch_cost)) {
                for (const auto& r : merge_edges(ch, edges)) {
                    SHARED_PTR<RouteDataObject> road;
                    for (const auto& segment : ch_context->loadRouteSegment(r.anchor_x, r.anchor_y))
                        if (segment->getRoad()->getId() == r.road) { road = segment->getRoad(); break; }
                    if (road) ch_segments.push_back(std::make_shared<RouteSegmentResult>(road, r.start, r.end));
                }
                prepareResult(ch_context.get(), ch_segments);
                ch_front.makeStartEndPointsPrecise(ch_context.get(), ch_segments, sx, sy, ex, ey);
            }
            const double ch_ms = ms_since(t);
            float ch_s = 0.0f;
            const double ch_m = route_metres(ch_segments, &ch_s);
            ++index;
            if (hh_segments.empty() || ch_segments.empty() || hh_m <= 0 || ch_m <= 0) {
                std::printf("pair %d: hh %zu segments, ch %zu segments: no comparison\n", index,
                            hh_segments.size(), ch_segments.size());
                continue;
            }
            if (const char* dump = std::getenv("ROUTE_BENCH_DUMP")) {
                // ordered "road:metres" lists of both routes, for analysis on the PC
                if (std::FILE* out = std::fopen(dump, "a")) {
                    for (int which = 0; which < 2; ++which) {
                        const auto& segments = which == 0 ? hh_segments : ch_segments;
                        std::fprintf(out, "%c %d", which == 0 ? 'H' : 'C', index);
                        for (const auto& s : segments)
                            if (s) std::fprintf(out, " %lld:%.0f", (long long)s->object->getId(), (double)s->distance);
                        std::fprintf(out, "\n");
                    }
                    std::fclose(out);
                }
            }
            const auto hh_roads = road_metres(hh_segments), ch_roads = road_metres(ch_segments);
            double shared = 0;
            for (const auto& kv : ch_roads) {
                const auto it = hh_roads.find(kv.first);
                if (it != hh_roads.end()) shared += std::min(kv.second, it->second);
            }
            time_ratio.push_back(ch_s / hh_s); dist_ratio.push_back(ch_m / hh_m);
            overlap_hh.push_back(shared / hh_m); overlap_ch.push_back(shared / ch_m);
            hh_ms_all.push_back(hh_ms); ch_ms_all.push_back(ch_ms);
            std::printf("pair %d: hh %.0fms %.0fm %.0fs | ch %.0fms %.0fm %.0fs | time x%.3f dist x%.3f shared %.0f%% of hh, %.0f%% of ch\n",
                        index, hh_ms, hh_m, hh_s, ch_ms, ch_m, ch_s, ch_s / hh_s, ch_m / hh_m,
                        100.0 * shared / hh_m, 100.0 * shared / ch_m);
            std::fflush(stdout);
        }
        std::fclose(pairs);
        auto median = [](std::vector<double> v) {
            if (v.empty()) return 0.0;
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        std::printf("summary: %zu pairs; median hh %.0fms, ch %.0fms; median ch/hh time x%.3f dist x%.3f; "
                    "median shared road %.0f%% of hh, %.0f%% of ch; worst time x%.3f\n", time_ratio.size(),
                    median(hh_ms_all), median(ch_ms_all), median(time_ratio), median(dist_ratio),
                    100.0 * median(overlap_hh), 100.0 * median(overlap_ch),
                    time_ratio.empty() ? 0.0 : *std::max_element(time_ratio.begin(), time_ratio.end()));
        return 0;
    }

    for (int run = 0; run < repeat; ++run) {
        MAP_STR_STR parameters;
        started = Clock::now();
        auto config = builder->build("car", memory_mb, parameters);
        const double build_ms = ms_since(started);
        const int sx = get31TileNumberX(lon1), sy = get31TileNumberY(lat1);
        const int ex = get31TileNumberX(lon2), ey = get31TileNumberY(lat2);
        std::vector<int> ix, iy;

        if (mode == "astar" || mode == "hh") {
            HHRoutingConfig* hh = nullptr;
            if (mode == "hh") {
                hh = HHRoutingConfig::astar(0);
                hh->calcDetailed(HHRoutingConfig::CALCULATE_ALL_DETAILED);
            }
            RoutePlannerFrontEnd front_end = hh ? RoutePlannerFrontEnd(hh) : RoutePlannerFrontEnd();
            auto context = front_end.buildRoutingContext(config);
            context->progress = std::make_shared<RouteCalculationProgress>();
            started = Clock::now();
            auto segments = front_end.searchRoute(context, sx, sy, ex, ey, ix, iy, nullptr);
            const double route_ms = ms_since(started);
            float seconds = 0.0f;
            const double metres = route_metres(segments, &seconds);
            std::printf("run %d %s mem=%d: route=%.0fms build_config=%.0fms segments=%zu "
                        "distance=%.0fm time=%.0fs\n", run, mode.c_str(), memory_mb, route_ms,
                        build_ms, segments.size(), metres, seconds);
            print_progress("progress", *context->progress);
            if (context->calculationProgressFirstPhase)
                print_progress("phase1", *context->calculationProgressFirstPhase);
        } else if (mode == "hhalt") {
            RoutePlannerFrontEnd front_end;
            auto context = front_end.buildRoutingContext(config);
            context->progress = std::make_shared<RouteCalculationProgress>();
            HHRoutingConfig* hh = HHRoutingConfig::astar(0);
            hh->calcDetailed(HHRoutingConfig::CALCULATE_ALL_DETAILED);
            hh->calcAlternative();
            started = Clock::now();
            HHRoutePlanner planner(context.get());
            HHNetworkRouteRes* result = planner.runRouting(sx, sy, ex, ey, hh);
            const double route_ms = ms_since(started);
            if (!result || !result->error.empty()) {
                std::printf("run %d hhalt: failed %s (%.0fms)\n", run,
                            result ? result->error.c_str() : "null", route_ms);
            } else {
                // What RoutePlannerFrontEnd does with an HH result.
                const auto prepare_started = Clock::now();
                prepareResult(context.get(), result->detailed);
                front_end.makeStartEndPointsPrecise(context.get(), result->detailed, sx, sy, ex, ey);
                for (auto* alt : result->altRoutes) {
                    prepareResult(context.get(), alt->detailed);
                    front_end.makeStartEndPointsPrecise(context.get(), alt->detailed, sx, sy, ex, ey);
                }
                std::printf("  prepare_results=%.0fms\n", ms_since(prepare_started));
                float seconds = 0.0f;
                const double metres = route_metres(result->detailed, &seconds);
                std::printf("run %d hhalt mem=%d: route=%.0fms segments=%zu distance=%.0fm "
                            "time=%.0fs alternatives=%zu\n", run, memory_mb, route_ms,
                            result->detailed.size(), metres, seconds, result->altRoutes.size());
                for (std::size_t i = 0; i < result->altRoutes.size(); ++i) {
                    const auto* alt = result->altRoutes[i];
                    const double alt_metres = route_metres(alt->detailed, &seconds);
                    std::printf("  alt %zu: segments=%zu distance=%.0fm time=%.0fs\n", i,
                                alt->detailed.size(), alt_metres, seconds);
                }
                std::printf("  stats: routing=%.0fms alt=%.0fms prep=%.0fms load_points=%.0fms "
                            "start_end=%.0fms\n", result->stats.routingTime,
                            result->stats.altRoutingTime, result->stats.prepTime,
                            result->stats.loadPointsTime, result->stats.searchPointsTime);
            }
            print_progress("progress", *context->progress);
        } else if (mode == "ch") {
            // CH path -> road runs -> RouteSegmentResult -> the core's own
            // prepareResult (turns, attached roads, times), each step timed.
            static std::unique_ptr<ChData> ch;
            static std::unique_ptr<onc_ch::Query> query;
            const char* prefix = std::getenv("ROUTE_BENCH_CH");
            if (!prefix) { std::fprintf(stderr, "ROUTE_BENCH_CH=<prefix of .coords/.ch/.meta> is required\n"); return 2; }
            if (!ch) {
                const auto load_started = Clock::now();
                ch.reset(new ChData);
                std::string error;
                if (!load_ch(prefix, *ch, error)) { std::fprintf(stderr, "ch load failed: %s\n", error.c_str()); return 1; }
                query.reset(new onc_ch::Query(ch->hierarchy));
                std::printf("ch: loaded %zu nodes, %zu edges in %.0fms\n", ch->x.size(), ch->road.size(),
                            ms_since(load_started));
            }
            RoutePlannerFrontEnd front_end;
            auto context = front_end.buildRoutingContext(config);
            context->progress = std::make_shared<RouteCalculationProgress>();
            const int sx = get31TileNumberX(lon1), sy = get31TileNumberY(lat1);
            const int ex = get31TileNumberX(lon2), ey = get31TileNumberY(lat2);
            auto total_started = Clock::now();
            auto step = Clock::now();
            const double snap_ms = 0.0;   // snapping is inside best_route below
            std::vector<uint32_t> edges;
            uint32_t cost = 0;
            step = Clock::now();
            if (!best_route(*ch, *query, sx, sy, ex, ey, edges, cost)) {
                std::printf("run %d ch: no route\n", run);
                continue;
            }
            const double query_ms = ms_since(step);   // snap (4 x 4 nodes) + queries + unpack
            step = Clock::now();
            const auto runs = merge_edges(*ch, edges);
            const double unpack_ms = ms_since(step);
            step = Clock::now();
            std::vector<SHARED_PTR<RouteSegmentResult>> results;
            size_t missing = 0;
            for (const auto& r : runs) {
                SHARED_PTR<RouteDataObject> road;
                for (const auto& segment : context->loadRouteSegment(r.anchor_x, r.anchor_y)) {
                    if (segment->getRoad()->getId() == r.road) { road = segment->getRoad(); break; }
                }
                if (!road) { ++missing; continue; }
                results.push_back(std::make_shared<RouteSegmentResult>(road, r.start, r.end));
            }
            const double roads_ms = ms_since(step);
            if (missing) std::printf("run %d ch: %zu of %zu road runs not found in the maps\n", run, missing, runs.size());
            step = Clock::now();
            prepareResult(context.get(), results);
            front_end.makeStartEndPointsPrecise(context.get(), results, sx, sy, ex, ey);
            const double prepare_ms = ms_since(step);
            float seconds = 0.0f;
            const double metres = route_metres(results, &seconds);
            std::printf("run %d ch mem=%d: route=%.0fms (snap=%.0f query=%.2f unpack=%.2f roads=%.0f prepare=%.0f) "
                        "segments=%zu distance=%.0fm time=%.0fs ch_cost=%.0fs base_edges=%zu road_runs=%zu\n",
                        run, memory_mb, ms_since(total_started), snap_ms, query_ms, unpack_ms, roads_ms, prepare_ms,
                        results.size(), metres, seconds, cost / 1000.0, edges.size(), runs.size());
            print_progress("progress", *context->progress);
        } else {
            std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
            return 2;
        }
        std::fflush(stdout);
    }
    return 0;
}
