#include "config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>

#include <open_navicore/vp4r_flags.h>

#include <dirent.h>
#include <sys/stat.h>

namespace navicore_osmand {
namespace {
std::string path(const char* variable, const std::string& fallback)
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

std::vector<std::string> obf_files(const std::string& map)
{
    std::vector<std::string> files;
    struct stat info {};
    if (map.empty() || ::stat(map.c_str(), &info) != 0)
        return files;
    if (!S_ISDIR(info.st_mode)) {
        files.push_back(map);
        return files;
    }
    DIR* dir = ::opendir(map.c_str());
    if (!dir)
        return files;
    while (const dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.size() > 4 && name[0] != '.' &&
            name.compare(name.size() - 4, 4, ".obf") == 0)
            files.push_back(map + "/" + name);
    }
    ::closedir(dir);
    std::sort(files.begin(), files.end());
    return files;
}

bool is_basemap(const std::string& path)
{
    const auto slash = path.rfind('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name.find("basemap") != std::string::npos;
}

Config Config::from_environment()
{
    Config config;
    config.resources = path("OPEN_NAVICORE_OSMAND_RESOURCES", config.resources);
    const bool map_overridden = std::getenv("OPEN_NAVICORE_OSMAND_MAP") != nullptr;
    config.map = path("OPEN_NAVICORE_OSMAND_MAP", config.map);
    if (!map_overridden && obf_files(config.map).empty())
        config.map = "/datastore/navicore-backend/maps/kyiv.obf";
    struct stat map_info {};
    const bool map_is_directory = ::stat(config.map.c_str(), &map_info) == 0 && S_ISDIR(map_info.st_mode);
    config.ch_sidecar = path("OPEN_NAVICORE_OSMAND_CH_SIDECAR",
                             map_is_directory ? config.map + "/car.chr" : config.map + ".chr");
    if (marker_or_environment("OPEN_NAVICORE_OSMAND_CH_DISABLE", "/datastore/navicore-backend/disable-ch"))
        config.ch_sidecar.clear();
    config.fonts = path("OPEN_NAVICORE_OSMAND_FONTS", config.fonts);
    config.search_index = path("OPEN_NAVICORE_OSMAND_SEARCH_INDEX", config.search_index);
    config.recents = path("OPEN_NAVICORE_OSMAND_RECENTS", config.recents);
    config.parked = path("OPEN_NAVICORE_OSMAND_PARKED", config.parked);
    config.vector_cache = path("OPEN_NAVICORE_OSMAND_VECTOR_CACHE", config.vector_cache);
    config.language_marker = path("OPEN_NAVICORE_OSMAND_LANGUAGE_MARKER", config.language_marker);
    config.hmi_language_file = path("OPEN_NAVICORE_OSMAND_HMI_LANGUAGE", config.hmi_language_file);
    config.simulate_route = marker_or_environment(
        "OPEN_NAVICORE_OSMAND_SIMULATE_ROUTE",
        "/datastore/navicore-backend/enable-route-simulation");
    config.benchmark_autostart = marker_or_environment(
        "OPEN_NAVICORE_OSMAND_BENCHMARK_AUTOSTART",
        "/datastore/navicore-backend/enable-benchmark-autostart");
    config.simulate_detour = marker_or_environment(
        "OPEN_NAVICORE_OSMAND_SIMULATE_DETOUR",
        "/datastore/navicore-backend/simulate-detour");
    config.performance_metrics = marker_or_environment(
        "OPEN_NAVICORE_OSMAND_PERFORMANCE_METRICS",
        "/datastore/navicore-backend/enable-performance-metrics");
    // On by default (vp4r layer); $VP4R_HOME/flags/navicore.d/no-voice or
    // OPEN_NAVICORE_OSMAND_VOICE=0 switches it off, =1 forces it on.
    if (const char* voice = std::getenv("OPEN_NAVICORE_OSMAND_VOICE"))
        config.voice = !(voice[0] == '0' && voice[1] == '\0');
    else
        config.voice = !open_navi_vp4r_flag_exists("navicore", "no-voice");
    config.voice_queue = path("OPEN_NAVICORE_OSMAND_VOICE_QUEUE", config.voice_queue);

    return config;
}
} // namespace navicore_osmand
