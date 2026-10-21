#pragma once

#include "camera_controller.h"
#include "gps_history.h"
#include "ui/navigation_model.h"

#include <open_navicore/backend.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace navicore_osmand {

// A value the platform reported, kept raw for the developer menu.
template <typename T> struct RawInput { T value{}; bool has = false; };

// Bench markers shown as switches; read at backend (or provider) start.
inline constexpr const char* kMarkerDir = "/datastore/navicore-backend/";
struct DevMarker { const char* label; const char* file; };
inline constexpr DevMarker kDevMarkers[] = {
    {"Route simulation", "enable-route-simulation"},
    {"Performance metrics", "enable-performance-metrics"},
    {"Input trace", "enable-input-trace"},
    {"Benchmark autostart", "enable-benchmark-autostart"},
    {"Simulate detour", "simulate-detour"},
};
inline constexpr std::size_t kDevToggleNight = 0, kDevToggleTilt = 1, kDevMarkerFirst = 2;
inline constexpr std::size_t kDevClearCache =
    kDevMarkerFirst + sizeof(kDevMarkers) / sizeof(kDevMarkers[0]);

bool file_exists(const std::string& path);

struct DevMapStats {
    std::uint64_t tiles_built = 0, disk_hits = 0;
    std::size_t gpu_tiles = 0, gpu_bytes = 0, wanted_tiles = 0, prefetch_tiles = 0;
    int tile_zoom = 0;
};

// Everything the developer menu shows, copied out of the engine so the menu
// text is built by pure functions.
struct DevMenuSnapshot {
    std::chrono::steady_clock::time_point now;

    OpenNaviBackendGpsV1 gps{};
    bool has_valid_position = false;
    GpsHistory gps_history;

    CameraController::Vehicle vehicle;
    CameraController::Mode camera_mode = CameraController::Mode::HeadingUp;
    float zoom = 0.0f, elevation_degrees = 0.0f;
    bool guiding = false, recalculating = false;
    double off_route_metres = 0.0;

    std::string map_path, vector_cache_dir;
    bool has_vector_stats = false;
    DevMapStats vector_stats;
    std::vector<float> frame_intervals_ms;

    RawInput<OpenNaviBackendVehicleV2> vehicle_in;
    RawInput<OpenNaviBackendVisibilityV2> visibility_in;
    RawInput<OpenNaviBackendScreenV1> screen_in;
    RawInput<OpenNaviBackendInternationalizationV1> language_in;
    RawInput<OpenNaviBackendDriverDistractionV1> distraction_in;
    RawInput<OpenNaviBackendClusterDisplayV1> cluster_display_in;
    bool outbound = false;
    std::uint64_t cluster_publishes = 0;
    std::uint32_t cluster_last_status = 0;

    std::string backend_git, backend_date;

    bool night = false, tilted = false;
    bool cache_clearing = false, cache_clear_done = false;
    std::size_t cache_cleared_files = 0;
};

std::vector<ui::DevSection> build_dev_sections(const DevMenuSnapshot& snapshot);
std::vector<ui::DevToggle> build_dev_toggles(const DevMenuSnapshot& snapshot);

} // namespace navicore_osmand
