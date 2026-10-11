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
    return {
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
        20.0
    };
}
} // namespace navicore_osmand
