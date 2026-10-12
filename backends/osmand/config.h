#pragma once

#include <string>

namespace navicore_osmand {

// An owned snapshot taken at start; environment storage is never retained.
struct Config {
    std::string resources;
    std::string map;
    std::string fonts;
    bool simulate_route = false;
    bool performance_metrics = false;
    bool detailed_tile_metrics = false;
    bool render_shootout = false;
    bool sustained_basemap_benchmark = false;
    bool baked_map_symbols = false;
    bool bounded_map_symbols = false;
    bool retained_map_symbols = false;
    bool gpu_upload_worker = false;
    bool benchmark_autostart = false;
    bool car_style = false;      // from_environment() defaults it on
    bool rgb565_tiles = false;
    double simulation_speed_metres_per_second = 20.0;
    std::string recents = "/datastore/navicore-backend/recents.tsv";
    bool tile_cache = false;
    std::string tile_cache_runtime = "/datastore/navicore-backend/tilecache";
    std::string tile_cache_base = "/datastore/navicore-backend/tilecache-base";
    unsigned long tile_cache_megabytes = 8192;
    // Presence starts the pre-cache worker into the base store.
    std::string precache_manifest =
        "/datastore/navicore-backend/precache.manifest";
    // Set after the positional fields above; see from_environment().
    bool production_profile = false;

    static Config from_environment();
};

} // namespace navicore_osmand
