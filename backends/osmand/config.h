#pragma once

#include <string>

namespace navicore_osmand {

// An owned snapshot taken at start; environment storage is never retained.
// The production build has no feature switches: the vector map, car camera
// and design UI are always on. Only the paths and bench tools below remain.
struct Config {
    std::string resources = "/datastore/navicore-backend/libOsmAndCore_ResourcesBundle_shared.so";
    std::string map = "/datastore/navicore-backend/maps/kyiv.obf";
    std::string fonts = "/datastore/navicore-backend/fonts";
    std::string recents = "/datastore/navicore-backend/recents.tsv";
    // Built vector tiles, per map file; empty disables the disk cache.
    std::string vector_cache = "/datastore/navicore-backend/vectorcache";

    // Bench tools (markers in /datastore/navicore-backend, see README).
    bool simulate_route = false;       // enable-route-simulation
    bool benchmark_autostart = false;  // enable-benchmark-autostart
    bool simulate_detour = false;      // simulate-detour
    bool performance_metrics = false;  // enable-performance-metrics
    double simulation_speed_metres_per_second = 20.0;

    static Config from_environment();
};

} // namespace navicore_osmand
