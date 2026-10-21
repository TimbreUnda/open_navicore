#include "dev_menu.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/statvfs.h>
#if defined(__QNXNTO__)
#include <malloc.h>
#endif

namespace navicore_osmand {

bool file_exists(const std::string& path)
{
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0;
}

namespace {

std::string printf_string(const char* format, ...)
{
    char text[160] = {};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    return text;
}

// One "label: value" line of a developer-menu section.
void add_row(ui::DevSection& section, std::string label, std::string value)
{
    section.rows.emplace_back(std::move(label), std::move(value));
}

ui::DevSection gps_section(const DevMenuSnapshot& s)
{
    ui::DevSection gps{"GPS", {}};
    // Several fields are raw values without qualified units
    // (api-ui-restrictions.md), shown raw and labelled so.
    const auto& g = s.gps;
    add_row(gps, "Fix (raw)", printf_string("%u%s", g.fix_type,
                                            s.has_valid_position ? " · position" : " · none"));
    add_row(gps, "Satellites", printf_string("%u used / %u in view", g.satellites_used,
                                             g.satellites_in_view));
    add_row(gps, "DOP (raw)", printf_string("H %u · P %u · V %u", g.hdop_raw, g.pdop_raw,
                                            g.vdop_raw));
    add_row(gps, "Accuracy (raw)", printf_string("%u", g.accuracy_raw));
    add_row(gps, "Position", printf_string("%.6f, %.6f", g.latitude_e7 / 1e7,
                                           g.longitude_e7 / 1e7));
    add_row(gps, "Course (raw)", printf_string("hdg %u · spd %u", g.heading_raw,
                                               g.speed_raw_masked));
    add_row(gps, "Quality / sync", printf_string("%u / %u", g.quality_status,
                                                 g.sync_counter));
    if (s.gps_history.empty()) {
        add_row(gps, "Last fix", "never");
        add_row(gps, "Rate", "-");
    } else {
        add_row(gps, "Last fix", printf_string("%.1f s ago",
            std::chrono::duration<double>(s.now - s.gps_history.latest()).count()));
        const double rate = s.gps_history.rate_hz();
        add_row(gps, "Rate", rate > 0.0 ? printf_string("%.1f Hz", rate)
                                        : std::string("-"));
    }
    return gps;
}

ui::DevSection position_section(const DevMenuSnapshot& s)
{
    ui::DevSection position{"POSITION AND CAMERA", {}};
    const auto& vehicle = s.vehicle;
    add_row(position, "Motion", vehicle.valid
        ? printf_string("%.0f km/h · %.0f°", vehicle.speed_mps * 3.6, vehicle.heading_degrees)
        : std::string("no position"));
    const char* mode = "free";
    switch (s.camera_mode) {
    case CameraController::Mode::HeadingUp: mode = "heading-up"; break;
    case CameraController::Mode::NorthUp: mode = "north-up"; break;
    case CameraController::Mode::Free: mode = "free"; break;
    case CameraController::Mode::Overview: mode = "overview"; break;
    }
    add_row(position, "Camera", printf_string("%s · z%.2f · %.0f°", mode,
        s.zoom, s.elevation_degrees));
    add_row(position, "Off route", s.guiding
        ? printf_string("%.0f m%s", s.off_route_metres,
                        s.recalculating ? " · recalculating" : "")
        : std::string("not guiding"));
    return position;
}

ui::DevSection map_section(const DevMenuSnapshot& s)
{
    ui::DevSection map{"MAP DATA", {}};
    {
        struct stat info {};
        const auto slash = s.map_path.rfind('/');
        const std::string name =
            slash == std::string::npos ? s.map_path : s.map_path.substr(slash + 1);
        add_row(map, "Map file", ::stat(s.map_path.c_str(), &info) == 0
            ? printf_string("%s · %.0f MB", name.c_str(), info.st_size / 1048576.0)
            : name + " · missing");
        struct statvfs space {};
        add_row(map, "Cache free", !s.vector_cache_dir.empty() &&
                         ::statvfs(s.vector_cache_dir.c_str(), &space) == 0
            ? printf_string("%.0f MB", static_cast<double>(space.f_bavail) * space.f_frsize / 1048576.0)
            : std::string("off"));
    }
    if (s.has_vector_stats) {
        const auto& stats = s.vector_stats;
        add_row(map, "Tiles", printf_string("%llu new · %llu disk · %zu GPU",
            static_cast<unsigned long long>(stats.tiles_built),
            static_cast<unsigned long long>(stats.disk_hits), stats.gpu_tiles));
        add_row(map, "GPU memory", printf_string("%.1f MB", stats.gpu_bytes / 1048576.0));
        add_row(map, "Wanted", printf_string("%zu · prefetch %zu · z%d",
            stats.wanted_tiles, stats.prefetch_tiles, stats.tile_zoom));
    }
    if (!s.frame_intervals_ms.empty()) {
        std::vector<float> sorted = s.frame_intervals_ms;
        const std::size_t count = sorted.size();
        std::sort(sorted.begin(), sorted.end());
        float total = 0.0f;
        for (const float interval : sorted)
            total += interval;
        const float p99 = sorted[std::min(count - 1, count * 99 / 100)];
        add_row(map, "Frames", printf_string("%.1f fps · 1%% low %.1f fps",
            1000.0f * count / std::max(total, 1.0f), 1000.0f / std::max(p99, 1.0f)));
    }
#if defined(__QNXNTO__)
    {
        const struct mallinfo heap = ::mallinfo();
        add_row(map, "Heap", printf_string("%.1f MB used · %.1f MB arena",
            heap.uordblks / 1048576.0, heap.arena / 1048576.0));
    }
#endif
    return map;
}

ui::DevSection platform_section(const DevMenuSnapshot& s)
{
    ui::DevSection platform{"CAR AND HMI (RAW)", {}};
    const auto raw = [&](bool has, std::string text) { return has ? text : std::string("-"); };
    const auto& v = s.vehicle_in.value;
    float displayed_speed = 0.0f;
    static_assert(sizeof(displayed_speed) == sizeof(v.displayed_vehicle_speed_bits), "float bits");
    std::memcpy(&displayed_speed, &v.displayed_vehicle_speed_bits, sizeof(displayed_speed));
    add_row(platform, "Day/night", raw(s.vehicle_in.has, printf_string(
        "status %u · setting %u · fields 0x%x", v.day_night_status, v.day_night_setting,
        v.valid_fields)));
    add_row(platform, "Car speed", raw(s.vehicle_in.has &&
        (v.valid_fields & OPEN_NAVI_BACKEND_VEHICLE_SPEED) != 0,
        printf_string("%.1f (GPS %.1f km/h)", displayed_speed,
                      s.vehicle.valid ? s.vehicle.speed_mps * 3.6 : 0.0)));
    add_row(platform, "Visibility", raw(s.visibility_in.has, printf_string(
        "%u · interactive %u · screen %u", s.visibility_in.value.visibility,
        s.visibility_in.value.interactivity, s.screen_in.value.screen_type)));
    add_row(platform, "Language", raw(s.language_in.has, printf_string(
        "%u · date %u · time %u", s.language_in.value.language_raw,
        s.language_in.value.date_format_raw, s.language_in.value.time_format_raw)));
    add_row(platform, "Distraction", raw(s.distraction_in.has, printf_string(
        "%u", s.distraction_in.value.distraction_status_raw)));
    add_row(platform, "Cluster", s.outbound ? printf_string(
        "%llu sent · status %u · display %s", static_cast<unsigned long long>(s.cluster_publishes),
        s.cluster_last_status, s.cluster_display_in.has
            ? printf_string("%u", s.cluster_display_in.value.enabled_raw).c_str() : "-")
        : std::string("no outbound services"));
    return platform;
}

ui::DevSection build_section(const DevMenuSnapshot& s)
{
    ui::DevSection build{"BUILD", {}};
    add_row(build, "Backend", s.backend_git);
    add_row(build, "Built", s.backend_date);
    add_row(build, "Interfaces", s.outbound ? "backend V5 · contacts" : "backend V2 · contacts");
    return build;
}

} // namespace

std::vector<ui::DevSection> build_dev_sections(const DevMenuSnapshot& snapshot)
{
    std::vector<ui::DevSection> sections;
    sections.push_back(gps_section(snapshot));
    sections.push_back(position_section(snapshot));
    sections.push_back(map_section(snapshot));
    sections.push_back(platform_section(snapshot));
    sections.push_back(build_section(snapshot));
    return sections;
}

std::vector<ui::DevToggle> build_dev_toggles(const DevMenuSnapshot& snapshot)
{
    std::vector<ui::DevToggle> toggles;
    toggles.push_back({"Night map", snapshot.night, ""});
    toggles.push_back({"3D view", snapshot.tilted, ""});
    for (const auto& marker : kDevMarkers)
        toggles.push_back({marker.label, file_exists(std::string(kMarkerDir) + marker.file),
                           "applies at next start"});
    toggles.push_back({"Clear vector cache", false,
                       snapshot.cache_clearing
                           ? printf_string("clearing · %zu files", snapshot.cache_cleared_files)
                       : snapshot.cache_clear_done
                           ? printf_string("cleared %zu files", snapshot.cache_cleared_files)
                           : std::string("rebuilds tiles from the map")});
    return toggles;
}

} // namespace navicore_osmand
