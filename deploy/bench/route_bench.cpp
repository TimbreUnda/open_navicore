// Route calculation benchmark for the radio (bench only, not in navigation.ifs).
//
//   route_bench_run MAP ROUTING_XML MODE MEMORY_MB LAT1 LON1 LAT2 LON2 [REPEAT]
//
// MAP is one .obf file or a directory of them (all are opened).
//
// MODE: astar (what the navigation worker does), hh (highway hierarchy),
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

#include <chrono>
#include <dirent.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
        } else {
            std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
            return 2;
        }
        std::fflush(stdout);
    }
    return 0;
}
