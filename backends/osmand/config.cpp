#include "config.h"

#include <cstdlib>
#include <cstdio>

namespace navicore_osmand {
namespace {
std::string path(const char* variable, const char* fallback)
{
    const char* value = std::getenv(variable);
    return value && *value ? value : fallback;
}

bool marker_or_environment(const char* variable, const char* marker_path)
{
    const char* value = std::getenv(variable);
    if (value && value[0] == '1' && value[1] == '\0')
        return true;
    std::FILE* marker = std::fopen(marker_path, "r");
    if (!marker)
        return false;
    std::fclose(marker);
    return true;
}
}

Config Config::from_environment()
{
    Config config{
        path("OPEN_NAVICORE_OSMAND_RESOURCES",
             "/datastore/navicore-backend/libOsmAndCore_ResourcesBundle_shared.so"),
        path("OPEN_NAVICORE_OSMAND_MAP", "/datastore/navicore-backend/maps/kyiv.obf"),
        path("OPEN_NAVICORE_OSMAND_FONTS", "/datastore/navicore-backend/fonts"),
        marker_or_environment(
            "OPEN_NAVICORE_OSMAND_SIMULATE_ROUTE",
            "/datastore/navicore-backend/enable-route-simulation"),
        marker_or_environment(
            "OPEN_NAVICORE_OSMAND_PERFORMANCE_METRICS",
            "/datastore/navicore-backend/enable-performance-metrics"),
        marker_or_environment(
            "OPEN_NAVICORE_OSMAND_DETAILED_TILE_METRICS",
            "/datastore/navicore-backend/enable-detailed-tile-metrics"),
        marker_or_environment(
            "OPEN_NAVICORE_OSMAND_RENDER_SHOOTOUT",
            "/datastore/navicore-backend/enable-render-shootout"),
        marker_or_environment(
            "OPEN_NAVICORE_OSMAND_SUSTAINED_BASEMAP_BENCHMARK",
            "/datastore/navicore-backend/enable-sustained-basemap-benchmark"),
        marker_or_environment(
            "OPEN_NAVICORE_OSMAND_BAKED_MAP_SYMBOLS",
            "/datastore/navicore-backend/enable-baked-map-symbols"),
        marker_or_environment(
            "OPEN_NAVICORE_OSMAND_BOUNDED_MAP_SYMBOLS",
            "/datastore/navicore-backend/enable-bounded-map-symbols"),
        marker_or_environment(
            "OPEN_NAVICORE_OSMAND_RETAINED_MAP_SYMBOLS",
            "/datastore/navicore-backend/enable-retained-map-symbols"),
        marker_or_environment(
            "OPEN_NAVICORE_OSMAND_GPU_UPLOAD_WORKER",
            "/datastore/navicore-backend/enable-gpu-upload-worker"),
        marker_or_environment(
            "OPEN_NAVICORE_OSMAND_BENCHMARK_AUTOSTART",
            "/datastore/navicore-backend/enable-benchmark-autostart"),
        // Car product: the car style is the default; the marker opts out.
        !marker_or_environment(
            "OPEN_NAVICORE_OSMAND_DISABLE_CAR_STYLE",
            "/datastore/navicore-backend/disable-car-style"),
        marker_or_environment(
            "OPEN_NAVICORE_OSMAND_RGB565_TILES",
            "/datastore/navicore-backend/enable-rgb565-tiles"),
        20.0
    };
    config.recents = path("OPEN_NAVICORE_OSMAND_RECENTS",
                          "/datastore/navicore-backend/recents.tsv");
    config.tile_cache = marker_or_environment(
        "OPEN_NAVICORE_OSMAND_TILE_CACHE",
        "/datastore/navicore-backend/enable-tile-cache");
    config.tile_cache_runtime = path("OPEN_NAVICORE_OSMAND_TILE_CACHE_DIR",
        "/navi_map/open-navicore/tilecache");
    config.tile_cache_base = path("OPEN_NAVICORE_OSMAND_TILE_CACHE_BASE",
        "/navi_map/open-navicore/tilecache-base");
    if (const char* megabytes = std::getenv("OPEN_NAVICORE_OSMAND_TILE_CACHE_MB")) {
        char* end = nullptr;
        const unsigned long value = std::strtoul(megabytes, &end, 10);
        if (end && *end == '\0' && value > 0)
            config.tile_cache_megabytes = value;
    }
    config.precache_manifest = path("OPEN_NAVICORE_OSMAND_PRECACHE_MANIFEST",
        "/datastore/navicore-backend/precache.manifest");
    // One switch for the live qualification of everything built offline:
    // RGB565 tiles, GPU upload worker, tile cache and retained labels.
    if (marker_or_environment(
            "OPEN_NAVICORE_OSMAND_PRODUCTION_PROFILE",
            "/datastore/navicore-backend/enable-production-profile")) {
        config.production_attempt =
            path("OPEN_NAVICORE_OSMAND_PRODUCTION_ATTEMPT",
                 "/datastore/navicore-backend/.production-profile-attempt");
        // Crash guard: an attempt file left behind means the last run with
        // these features died before it was stable. Run without them once
        // so System Manager's relaunch budget is not spent on a crash loop.
        if (std::FILE* previous =
                std::fopen(config.production_attempt.c_str(), "r")) {
            std::fclose(previous);
            std::remove(config.production_attempt.c_str());
            config.production_profile_suppressed = true;
        } else {
            if (std::FILE* attempt =
                    std::fopen(config.production_attempt.c_str(), "w"))
                std::fclose(attempt);
            config.production_profile = true;
            config.rgb565_tiles = true;
            config.gpu_upload_worker = true;
            config.tile_cache = true;
            config.retained_map_symbols = true;
        }
    }
    // The vector map is the default; the disable marker (or the variable
    // set to 0) brings back OsmAnd's raster map.
    const char* vector_variable = std::getenv("OPEN_NAVICORE_OSMAND_VECTOR_MAP");
    const bool vector_disabled =
        (vector_variable && vector_variable[0] == '0' && vector_variable[1] == '\0') ||
        marker_or_environment("OPEN_NAVICORE_OSMAND_VECTOR_MAP_DISABLED",
                              "/datastore/navicore-backend/disable-vector-map");
    config.start_tilted = marker_or_environment("OPEN_NAVICORE_OSMAND_START_TILTED",
                                                "/datastore/navicore-backend/start-tilted");
    config.start_night = marker_or_environment("OPEN_NAVICORE_OSMAND_START_NIGHT",
                                               "/datastore/navicore-backend/start-night");
    config.simulate_detour = marker_or_environment(
        "OPEN_NAVICORE_OSMAND_SIMULATE_DETOUR", "/datastore/navicore-backend/simulate-detour");
    config.vector_overzoom = marker_or_environment(
        "OPEN_NAVICORE_OSMAND_VECTOR_OVERZOOM", "/datastore/navicore-backend/vector-overzoom");
    config.vector_cache = path("OPEN_NAVICORE_OSMAND_VECTOR_CACHE",
                               "/datastore/navicore-backend/vectorcache");
    if (!vector_disabled) {
        config.vector_map_attempt =
            path("OPEN_NAVICORE_OSMAND_VECTOR_MAP_ATTEMPT",
                 "/datastore/navicore-backend/.vector-map-attempt");
        if (std::FILE* previous =
                std::fopen(config.vector_map_attempt.c_str(), "r")) {
            std::fclose(previous);
            std::remove(config.vector_map_attempt.c_str());
            config.vector_map_suppressed = true;
        } else {
            if (std::FILE* attempt =
                    std::fopen(config.vector_map_attempt.c_str(), "w"))
                std::fclose(attempt);
            config.vector_map = true;
        }
    }
    return config;
}
} // namespace navicore_osmand
