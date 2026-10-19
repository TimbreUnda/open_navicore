// Route calculation benchmark for the radio (bench only, not in navigation.ifs).
//
//   route_bench_run MAP ROUTING_XML MODE MEMORY_MB LAT1 LON1 LAT2 LON2 [REPEAT]
//
// MAP is one .obf file or a directory of them (all are opened).
//
// MODE: astar (what the navigation worker did), hh (highway hierarchy),
// ch (contraction hierarchy; ROUTE_BENCH_CH=<routing sidecar file>),
// compare (hh vs ch on every "lat1 lon1 lat2 lon2" line of the file given as LAT1),
// hhalt (hh with alternatives). Prints wall time per phase and OsmAnd's own
// load/calculate timers and tile counters, one line per run.

// Core and core-legacy both ship a Logging.h; the planner header includes the Core's and blocks
// the legacy one, so it comes first.
#include "../../backends/osmand/ch_planner.h"
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
    std::vector<std::string> routable_maps;
    for (const auto& path : map_files) {
        const auto file = initBinaryMapFile(path, false, true);
        if (!file || file->routingIndexes.empty()) continue;
        ++opened;
        routable_maps.push_back(path);
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
        const char* sidecar_path = std::getenv("ROUTE_BENCH_CH");
        if (!sidecar_path) { std::fprintf(stderr, "ROUTE_BENCH_CH=<sidecar file> is required\n"); return 2; }
        std::string error;
        auto planner = navicore_osmand::ChPlanner::open(sidecar_path, routable_maps, error);
        if (!planner) { std::fprintf(stderr, "sidecar: %s\n", error.c_str()); return 1; }
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
            std::string why;
            auto ch_segments = planner->route(ch_front, ch_context, sx, sy, ex, ey, why);
            if (ch_segments.empty()) std::printf("  ch: %s\n", why.c_str());
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
            // Sidecar route through ChPlanner (the worker's code): search, load the roads,
            // the core's prepareResult; each step timed.
            static std::unique_ptr<navicore_osmand::ChPlanner> planner;
            if (!planner) {
                const char* sidecar_path = std::getenv("ROUTE_BENCH_CH");
                if (!sidecar_path) { std::fprintf(stderr, "ROUTE_BENCH_CH=<sidecar file> is required\n"); return 2; }
                const auto open_started = Clock::now();
                std::string error;
                planner = navicore_osmand::ChPlanner::open(sidecar_path, routable_maps, error);
                if (!planner) { std::fprintf(stderr, "sidecar: %s\n", error.c_str()); return 1; }
                std::printf("ch: %s opened in %.0fms\n", planner->describe().c_str(), ms_since(open_started));
            }
            RoutePlannerFrontEnd front_end;
            auto context = front_end.buildRoutingContext(config);
            context->progress = std::make_shared<RouteCalculationProgress>();
            const auto total_started = Clock::now();
            std::string why;
            auto results = planner->route(front_end, context, sx, sy, ex, ey, why);
            const double total_ms = ms_since(total_started);
            if (results.empty()) {
                std::printf("run %d ch: no route (%s)\n", run, why.c_str());
                continue;
            }
            const auto& timing = planner->last_timing();
            float seconds = 0.0f;
            const double metres = route_metres(results, &seconds);
            std::printf("run %d ch mem=%d: route=%.0fms (search=%.2f roads=%.0f prepare=%.0f) segments=%zu "
                        "distance=%.0fm time=%.0fs base_edges=%zu road_runs=%zu settled=%zu\n",
                        run, memory_mb, total_ms, timing.search_ms, timing.roads_ms, timing.prepare_ms,
                        results.size(), metres, seconds, timing.base_edges, timing.road_runs, timing.settled);
            print_progress("progress", *context->progress);
        } else {
            std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
            return 2;
        }
        std::fflush(stdout);
    }
    return 0;
}
