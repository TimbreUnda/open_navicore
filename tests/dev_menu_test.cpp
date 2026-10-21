// The developer-menu text is built from a snapshot by pure functions.
#include "dev_menu.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace navicore_osmand;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,         \
                         __LINE__, #condition);                                 \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

namespace {

const ui::DevSection* section(const std::vector<ui::DevSection>& sections,
                              const std::string& title)
{
    for (const auto& candidate : sections)
        if (candidate.title == title)
            return &candidate;
    return nullptr;
}

std::string row(const ui::DevSection& s, const std::string& label)
{
    for (const auto& entry : s.rows)
        if (entry.first == label)
            return entry.second;
    return "<missing>";
}

void test_sections_and_empty_states()
{
    DevMenuSnapshot snapshot;
    snapshot.backend_git = "abc123";
    snapshot.backend_date = "2026-10-21";
    const auto sections = build_dev_sections(snapshot);
    CHECK(sections.size() == 5);
    const auto* gps = section(sections, "GPS");
    CHECK(gps && row(*gps, "Last fix") == "never" && row(*gps, "Rate") == "-");
    CHECK(row(*gps, "Fix (raw)") == "0 · none");
    const auto* position = section(sections, "POSITION AND CAMERA");
    CHECK(position && row(*position, "Motion") == "no position");
    CHECK(row(*position, "Off route") == "not guiding");
    const auto* platform = section(sections, "CAR AND HMI (RAW)");
    CHECK(platform && row(*platform, "Day/night") == "-");
    CHECK(row(*platform, "Cluster") == "no outbound services");
    const auto* build = section(sections, "BUILD");
    CHECK(build && row(*build, "Backend") == "abc123" && row(*build, "Built") == "2026-10-21");
    CHECK(row(*build, "Interfaces") == "backend V2 · contacts");
}

void test_values()
{
    DevMenuSnapshot snapshot;
    snapshot.now = std::chrono::steady_clock::now();
    snapshot.gps.satellites_used = 7;
    snapshot.gps.satellites_in_view = 11;
    snapshot.has_valid_position = true;
    snapshot.vehicle.valid = true;
    snapshot.vehicle.speed_mps = 10.0;
    snapshot.vehicle.heading_degrees = 90.0;
    snapshot.camera_mode = CameraController::Mode::NorthUp;
    snapshot.zoom = 15.5f;
    snapshot.elevation_degrees = 90.0f;
    snapshot.guiding = true;
    snapshot.off_route_metres = 42.0;
    snapshot.recalculating = true;
    snapshot.outbound = true;
    snapshot.cluster_publishes = 3;
    snapshot.frame_intervals_ms = {16.0f, 16.0f, 16.0f, 40.0f};
    const auto sections = build_dev_sections(snapshot);
    const auto* gps = section(sections, "GPS");
    CHECK(row(*gps, "Satellites") == "7 used / 11 in view");
    CHECK(row(*gps, "Fix (raw)") == "0 · position");
    const auto* position = section(sections, "POSITION AND CAMERA");
    CHECK(row(*position, "Motion") == "36 km/h · 90°");
    CHECK(row(*position, "Camera") == "north-up · z15.50 · 90°");
    CHECK(row(*position, "Off route") == "42 m · recalculating");
    const auto* map = section(sections, "MAP DATA");
    CHECK(row(*map, "Frames") == "45.5 fps · 1% low 25.0 fps");
    CHECK(row(*section(sections, "CAR AND HMI (RAW)"), "Cluster") ==
          "3 sent · status 0 · display -");
    CHECK(row(*section(sections, "BUILD"), "Interfaces") == "backend V5 · contacts");
}

void test_toggles()
{
    DevMenuSnapshot snapshot;
    snapshot.night = true;
    auto toggles = build_dev_toggles(snapshot);
    CHECK(toggles.size() == kDevClearCache + 1);
    CHECK(toggles[kDevToggleNight].on && !toggles[kDevToggleTilt].on);
    CHECK(toggles[kDevClearCache].note == "rebuilds tiles from the map");
    snapshot.cache_clearing = true;
    snapshot.cache_cleared_files = 12;
    CHECK(build_dev_toggles(snapshot)[kDevClearCache].note == "clearing · 12 files");
    snapshot.cache_clearing = false;
    snapshot.cache_clear_done = true;
    CHECK(build_dev_toggles(snapshot)[kDevClearCache].note == "cleared 12 files");
}

} // namespace

int main()
{
    test_sections_and_empty_states();
    test_values();
    test_toggles();
    std::puts("dev_menu_test passed");
    return 0;
}
