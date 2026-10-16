#pragma once

#include <string>
#include <vector>

namespace navicore_osmand {

// An owned snapshot taken at start; environment storage is never retained.
// The production build has no feature switches: the vector map, car camera
// and design UI are always on. Only the paths and bench tools below remain.
struct Config {
    std::string resources = "/open-navicore/libOsmAndCore_ResourcesBundle_shared.so";
    // A map file, or a directory whose *.obf files are all loaded. The
    // default directory is on the read-only-by-default map partition; when
    // it holds no .obf the single legacy file is used instead.
    std::string map = "/navi_map/osm";
    std::string fonts = "/datastore/navicore-backend/fonts";
    std::string recents = "/datastore/navicore-backend/recents.tsv";
    // UI language: the marker holds "uk", "en", "fr-CA" or "es-MX" (bench
    // override, also OPEN_NAVICORE_OSMAND_LANGUAGE); without one the radio's
    // own HMI language file is read ("english.cfg"); English otherwise.
    std::string language_marker = "/datastore/navicore-backend/language";
    std::string hmi_language_file = "/datastore/dc_hmi_language.txt";
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

// The .obf files named by a Config::map value, sorted by name: the file
// itself, or the *.obf entries of a directory. Empty when there are none.
std::vector<std::string> obf_files(const std::string& map);

// True for a world basemap file (its name contains "basemap", any case). The
// vector map draws tiles up to z10 from it; search and routing never see it.
bool is_basemap(const std::string& path);

} // namespace navicore_osmand
