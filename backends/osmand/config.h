#pragma once

#include <string>

namespace navicore_osmand {

// An owned snapshot taken at start; environment storage is never retained.
struct Config {
    std::string resources;
    std::string map;
    std::string fonts;
    bool simulate_route = false;
    double simulation_speed_metres_per_second = 200.0;

    static Config from_environment();
};

} // namespace navicore_osmand
