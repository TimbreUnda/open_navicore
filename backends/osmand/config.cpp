#include "config.h"

#include <cstdlib>

namespace navicore_osmand {
namespace {
std::string path(const char* variable, const char* fallback)
{
    const char* value = std::getenv(variable);
    return value && *value ? value : fallback;
}
}

Config Config::from_environment()
{
    return {
        path("OPEN_NAVICORE_OSMAND_RESOURCES",
             "/datastore/navicore-backend/libOsmAndCore_ResourcesBundle_shared.so"),
        path("OPEN_NAVICORE_OSMAND_MAP", "/datastore/navicore-backend/maps/kyiv.obf"),
        path("OPEN_NAVICORE_OSMAND_FONTS", "/datastore/navicore-backend/fonts")
    };
}
} // namespace navicore_osmand
