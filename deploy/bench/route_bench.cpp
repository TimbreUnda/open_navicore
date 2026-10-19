// Route calculation benchmark for the radio (bench only, not in navigation.ifs).
//
//   route_bench_run MAP ROUTING_XML MODE MEMORY_MB LAT1 LON1 LAT2 LON2 [REPEAT]
//
// MAP is one .obf file or a directory of them (all are opened).
//
// MODE: astar (what the navigation worker did), hh (highway hierarchy),
// ch (contraction hierarchy; ROUTE_BENCH_CH=<prefix> of the .coords/.ch/.meta files),
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
#include <cstring>
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
            const uint32_t from_node = nearest_node(*ch, sx, sy), to_node = nearest_node(*ch, ex, ey);
            const double snap_ms = ms_since(step);
            step = Clock::now();
            uint32_t meet = 0;
            const uint32_t cost = query->run(from_node, to_node, meet);
            const double query_ms = ms_since(step);
            std::vector<uint32_t> edges;
            step = Clock::now();
            if (cost == onc_ch::INF || !query->path_edges(from_node, to_node, meet, edges)) {
                std::printf("run %d ch: no route\n", run);
                continue;
            }
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
