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

bool simulation_enabled()
{
    const char* value = std::getenv("OPEN_NAVICORE_OSMAND_SIMULATE_ROUTE");
    if (value && value[0] == '1' && value[1] == '\0')
        return true;
    std::FILE* marker = std::fopen(
        "/datastore/navicore-backend/enable-route-simulation", "r");
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
        simulation_enabled(),
        200.0
    };
}
} // namespace navicore_osmand
