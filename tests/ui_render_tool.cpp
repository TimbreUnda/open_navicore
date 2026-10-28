// Renders the navigation UI in the design's reference states and writes each
// LVGL canvas as raw ARGB8888 (OUT_DIR/<state>.argb, 1024x574), for pixel
// comparison against the Uconnect navigation mockups:
//   ui-render-tool OUT_DIR [LANGUAGE]
// LANGUAGE is en (default), uk, fr-CA or es-MX; texts that come from the
// engine (categories, distances) are produced in that language too.
#include "ui/localization.h"
#include "ui/navigation_model.h"
#include "ui/navigation_view.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

using namespace navicore_osmand::ui;
namespace {
// "Fuel · 0.4 km" the way the engine builds a place card subtitle.
std::string fuel_subtitle(const std::string& tail = std::string())
{
    return loc::poi_category("amenity_fuel") + " · " + loc::format_distance(400.0) + tail;
}
} // namespace

namespace {
void settle(NavigationView& view)
{
    for (int i = 0; i < 40; ++i) {
        view.process_input(16);
        (void)view.render(0);
    }
}

bool write(const NavigationView& view, const std::string& path)
{
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file)
        return false;
    const std::size_t count = static_cast<std::size_t>(view.width()) * view.height();
    const bool ok = std::fwrite(view.pixels(), sizeof(std::uint32_t), count, file) == count;
    return std::fclose(file) == 0 && ok;
}

// Fixture texts the engine would produce (distances, categories) follow the
// UI language like in production.
std::string km(double value) { return loc::format_distance(value * 1000.0); }
std::string cat(const char* key, const std::string& tail = std::string())
{
    return tail.empty() ? loc::poi_category(key) : loc::poi_category(key) + " · " + tail;
}
std::string kind(loc::Str key, const std::string& tail)
{
    return std::string(loc::tr(key)) + " · " + tail;
}

Destination make_ocean()
{
    return {"ocean", "Ocean Plaza", "Antonovycha St 176 · " + cat("mall"), km(3.1), 50.4123,
            30.5221};
}

// Search -> preview through the real model transitions.
void to_preview(NavigationView& view)
{
    const auto search = view.set_query("Ocean");
    (void)view.apply_search_results(search, {make_ocean()});
    const auto route_revision = view.select_result(0);
    RouteAlternative route;
    route.id = "fast";
    route.duration = loc::format_duration_minutes(9);
    route.distance = km(2.4);
    route.reason = "Fastest";
    route.duration_seconds = 540.0;
    route.distance_metres = 2400.0;
    route.geometry = {{50.40, 30.52}, {50.41, 30.52}};
    (void)view.apply_routes(route_revision, {route});
}
} // namespace

int main(int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : ".";
    if (argc > 2) {
        loc::Language language = loc::Language::English;
        if (!loc::parse_language(argv[2], language)) {
            std::fprintf(stderr, "unknown language %s\n", argv[2]);
            return 2;
        }
        loc::set_language(language);
    }
    bool ok = true;
    {
        NavigationModel model;
        model.set_recents({make_ocean(),
                           {"rail", "Kyiv-Pasazhyrskyi", cat("station"), km(2.4), 50.44, 30.49},
                           {"olena", "Home of Olena", "Saksahanskoho St 41", km(1.2), 50.43, 30.51}});
        NavigationView view(model, 1024, 574);
        MapViewState night;  // the design boards show the night map
        night.night = true;
        (void)model.set_map_view(night);
        view.apply_map_view();
        settle(view);
        ok = write(view, out + "/free.argb") && ok;
    }
    {
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        to_preview(view);
        settle(view);
        ok = write(view, out + "/preview.argb") && ok;
    }
    {
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        to_preview(view);
        (void)view.start_guidance(0);
        MapViewState night;
        night.night = true;
        (void)model.set_map_view(night);
        GuidanceState state;
        state.position_available = true;
        state.distance_to_maneuver_metres = 250.0;
        state.remaining_metres = 2000.0;
        state.remaining_seconds = 360.0;
        state.maneuver.kind = ManeuverKind::Right;
        state.maneuver.road = "бульвар Тараса Шевченка";
        (void)view.apply_guidance(state);
        settle(view);
        ok = write(view, out + "/guidance.argb") && ok;
    }
    const std::vector<Destination> results = {
        {"s1", "Khreshchatyk St", kind(loc::Str::AddressStreet, "Shevchenkivskyi, Kyiv"), km(1.9), 50.44, 30.52},
        {"s2", "Khreshchatyk St 22", kind(loc::Str::AddressBuilding, "Kyiv"), km(2.0), 50.44, 30.52},
        {"s3", "Khreshchatyk St 15", kind(loc::Str::AddressBuilding, "Kyiv"), km(2.1), 50.44, 30.52},
        {"s4", "Khreshchatyk Park", cat("park", "Kyiv"), km(2.6), 50.44, 30.52}};
    for (const bool lock : {false, true}) {
        NavigationModel model;
        model.set_recents({make_ocean(),
                           {"rail", "Kyiv-Pasazhyrskyi", cat("station"), km(2.4), 50.44, 30.49},
                           {"olena", "Home of Olena", "Saksahanskoho St 41", km(1.2), 50.43, 30.51},
                           {"arsenalna", "Arsenalna", cat("subway_entrance"), km(4.8), 50.44, 30.54},
                           {"kbp", "Boryspil Airport (KBP)", cat("airport", "Boryspil"), km(34), 50.34, 30.89}});
        NavigationView view(model, 1024, 574);
        view.open_search();
        const auto revision = view.set_query("Khreshchatyk");
        (void)view.apply_search_results(revision, results);
        if (lock)
            (void)view.set_moving(true);
        settle(view);
        ok = write(view, out + (lock ? "/lock.argb" : "/search.argb")) && ok;
    }
    {
        // Place card over free drive (design proposal G2).
        NavigationModel model;
        model.set_recents({make_ocean(),
                           {"rail", "Kyiv-Pasazhyrskyi", cat("station"), km(2.4), 50.44, 30.49},
                           {"olena", "Home of Olena", "Saksahanskoho St 41", km(1.2), 50.43, 30.51}});
        NavigationView view(model, 1024, 574);
        MapViewState night;
        night.night = true;
        (void)model.set_map_view(night);
        (void)view.show_place_card({"poi:1,2", "WOG", fuel_subtitle(" · вулиця Саксаганського"), "",
                                    50.44, 30.52}, "amenity_fuel");
        settle(view);
        ok = write(view, out + "/place.argb") && ok;
    }
    {
        // Route preview with Back and three alternatives (G3, G4).
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        const auto search = view.set_query("Ocean");
        (void)view.apply_search_results(search, {make_ocean()});
        const auto route_revision = view.select_result(0);
        std::vector<RouteAlternative> routes;
        for (const auto& [time, distance, reason] :
             {std::tuple<std::string, std::string, const char*>{
                  loc::format_duration_minutes(9), km(2.4), "Fastest"},
              {loc::format_duration_minutes(11), km(2.2), "Shortest"},
              {loc::format_duration_minutes(12), km(2.9), "Fewer turns"}}) {
            RouteAlternative route;
            route.duration = time;
            route.distance = distance;
            route.reason = reason;
            route.geometry = {{50.40, 30.52}, {50.41, 30.52}};
            routes.push_back(route);
        }
        (void)view.apply_routes(route_revision, routes);
        settle(view);
        ok = write(view, out + "/alts.argb") && ok;
    }
    {
        // Arrival card (G5).
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        const auto search = view.set_query("Ocean");
        (void)view.apply_search_results(search, {{"ocean", "Ocean Plaza", "Antonovycha St 176",
                                                  "", 50.41, 30.52}});
        const auto route_revision = view.select_result(0);
        RouteAlternative route;
        route.geometry = {{50.40, 30.52}, {50.41, 30.52}};
        (void)view.apply_routes(route_revision, {route});
        (void)view.start_guidance(0);
        MapViewState night;
        night.night = true;
        (void)model.set_map_view(night);
        GuidanceState state;
        state.position_available = true;
        state.arrived = true;
        state.maneuver.kind = ManeuverKind::Arrive;
        (void)view.apply_guidance(state);
        for (int i = 0; i < 5; ++i) {
            view.process_input(16);
            (void)view.render(0);
        }
        ok = write(view, out + "/arrival.argb") && ok;
    }
    {
        // Off route, recalculating (G8).
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        to_preview(view);
        (void)view.start_guidance(0);
        MapViewState night;
        night.night = true;
        (void)model.set_map_view(night);
        GuidanceState state;
        state.position_available = true;
        state.recalculating = true;
        state.road = "вулиця Антоновича";
        state.speed_limit_kmh = 50;
        state.remaining_metres = 2400.0;
        state.remaining_seconds = 420.0;
        (void)view.apply_guidance(state);
        settle(view);
        ok = write(view, out + "/recalc.argb") && ok;
    }
    {
        // Lane guidance at 250 m (G9): use the two right lanes.
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        to_preview(view);
        (void)view.start_guidance(0);
        MapViewState night;
        night.night = true;
        (void)model.set_map_view(night);
        GuidanceState state;
        state.position_available = true;
        state.distance_to_maneuver_metres = 250.0;
        state.remaining_metres = 2000.0;
        state.remaining_seconds = 360.0;
        state.maneuver.kind = ManeuverKind::Right;
        state.maneuver.road = "бульвар Тараса Шевченка";
        Lane left, straight, both, right;
        left.kind = ManeuverKind::Left;
        both.combined = true;
        both.also = ManeuverKind::Right;
        both.recommended = true;
        right.kind = ManeuverKind::Right;
        right.recommended = true;
        state.maneuver.lanes = {left, straight, both, right};
        (void)view.apply_guidance(state);
        settle(view);
        ok = write(view, out + "/lanes.argb") && ok;
    }
    for (const bool following : {true, false}) {
        // Long street name: the pill uses the free row, less recenter.
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        to_preview(view);
        (void)view.start_guidance(0);
        MapViewState night;
        night.night = true;
        night.following = following;
        (void)model.set_map_view(night);
        GuidanceState state;
        state.position_available = true;
        state.road = "проспект Степана Бандери · вулиця Велика Васильківська";
        state.remaining_metres = 2400.0;
        state.remaining_seconds = 420.0;
        (void)view.apply_guidance(state);
        settle(view);
        ok = write(view, out + (following ? "/street_long.argb" : "/street_long_free.argb")) && ok;
    }
    for (const bool failed : {false, true}) {
        // Navigate from a place card: calculating, then a failure.
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        settle(view);
        (void)view.show_place_card({"poi:1,2", "WOG", fuel_subtitle(" · вулиця Саксаганського"), "",
                                    50.44, 30.52}, "amenity_fuel");
        const auto revision = view.select_place(*model.place_card());
        if (failed)
            (void)view.apply_routes(revision, {});
        settle(view);
        ok = write(view, out + (failed ? "/place_failed.argb" : "/place_calculating.argb")) && ok;
    }
    {
        // Dropped pin (long press) with its coordinates; heading-up dimmed
        // (no heading known yet).
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        MapViewState state;
        state.heading_known = false;
        (void)model.set_map_view(state);
        settle(view);
        (void)view.show_place_card({"pin:50.44000, 30.52000", loc::tr(loc::Str::DroppedPin),
                                    "50.44000, 30.52000 · " + loc::format_distance(400.0), "",
                                    50.44, 30.52}, "pin");
        view.set_pin_marker(true, 640, 300);
        settle(view);
        ok = write(view, out + "/pin.argb") && ok;
    }
    {
        // Simulated guidance into a roundabout with a long instruction (two
        // lines) and a long road name.
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        to_preview(view);
        (void)view.start_guidance(0);
        MapViewState night;
        night.night = true;
        (void)model.set_map_view(night);
        GuidanceState state;
        state.position_available = true;
        state.simulated = true;
        state.distance_to_maneuver_metres = 290.0;
        state.remaining_metres = 12400.0;
        state.remaining_seconds = 4500.0;
        state.maneuver.kind = ManeuverKind::Roundabout;
        state.maneuver.roundabout_exit = 2;
        state.maneuver.road = "проспект Академіка Глушкова";
        state.road = "проспект Академіка Глушкова";
        state.speed_limit_kmh = 60;
        (void)view.apply_guidance(state);
        settle(view);
        ok = write(view, out + "/roundabout.argb") && ok;
    }
    {
        // Sharp left with the longest turn wording.
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        to_preview(view);
        (void)view.start_guidance(0);
        GuidanceState state;
        state.position_available = true;
        state.distance_to_maneuver_metres = 1500.0;
        state.remaining_metres = 2000.0;
        state.remaining_seconds = 360.0;
        state.maneuver.kind = ManeuverKind::SharpLeft;
        state.maneuver.road = "вулиця Антоновича";
        (void)view.apply_guidance(state);
        settle(view);
        ok = write(view, out + "/sharp.argb") && ok;
    }
    {
        // No GPS fix.
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        to_preview(view);
        (void)view.start_guidance(0);
        GuidanceState state;
        state.position_available = false;
        state.maneuver.kind = ManeuverKind::KeepRight;
        state.maneuver.road = "вулиця Антоновича";
        (void)view.apply_guidance(state);
        settle(view);
        ok = write(view, out + "/nogps.argb") && ok;
    }
    {
        // Empty states: free drive without recents, search messages, no route.
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        settle(view);
        ok = write(view, out + "/free_empty.argb") && ok;
    }
    for (const int stage : {0, 1, 2, 3}) {
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        view.open_search();
        if (stage >= 1) {
            const auto revision = view.set_query("Khreshchatyk");
            if (stage == 2)
                (void)view.apply_search_results(revision, {});
            if (stage == 3) {
                (void)view.apply_search_results(revision, {make_ocean()});
                const auto route = view.select_result(0);
                (void)view.apply_routes(route, {});
            }
        }
        (void)view.set_moving(stage == 0);
        settle(view);
        ok = write(view, out + "/search_" + std::to_string(stage) + ".argb") && ok;
    }
    {
        // Developer menu with typical rows.
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        settle(view);
        (void)view.open_dev_menu();
        std::vector<DevSection> sections = {
            {"GPS", {{"Fix (raw)", "3 · position"}, {"Satellites", "9 used / 14 in view"},
                     {"DOP (raw)", "H 14 · P 15 · V 16"}, {"Accuracy (raw)", "42"},
                     {"Position", "50.450123, 30.523456"}, {"Course (raw)", "hdg 1234 · spd 56"},
                     {"Quality / sync", "1 / 17"}, {"Last fix", "0.4 s ago"}, {"Rate", "1.0 Hz"}}},
            {"POSITION AND CAMERA", {{"Motion", "42 km/h · 87°"},
                                     {"Camera", "heading-up · z16.25 · 50°"},
                                     {"Off route", "not guiding"}}},
            {"MAP DATA", {{"Map file", "kyiv.obf · 31 MB"}, {"Cache free", "162 MB"},
                          {"Tiles", "1234 new · 5678 disk · 96 GPU"}, {"GPU memory", "38.2 MB"},
                          {"Wanted", "64 · prefetch 12 · z16"},
                          {"Frames", "58.9 fps · 1% low 31.2 fps"},
                          {"Heap", "84.1 MB used · 96.0 MB arena"}}},
            {"BUILD", {{"Backend", "9a2d7ddb1fd1-dirty"}, {"Built", "Oct 15 2026 12:30:00"},
                       {"Interfaces", "query V1 · contacts"}}}};
        std::vector<DevToggle> toggles = {
            {"Night map", true, ""}, {"3D view", false, ""},
            {"Route simulation", true, "applies at next start"},
            {"Performance metrics", false, "applies at next start"},
            {"Input trace", false, "applies at next start"},
            {"Benchmark autostart", false, "applies at next start"},
            {"Simulate detour", false, "applies at next start"},
            {"Clear vector cache", false, "rebuilds tiles from the map"}};
        (void)view.set_dev_info(sections, toggles);
        settle(view);
        ok = write(view, out + "/dev.argb") && ok;
    }
    // HMI split screen (Media screen block): the area the stock HMI sends on
    // the bench (display 129,160 765x323), in NAV_main window pixels.
    for (const char* name : {"split", "split_recalc", "split_free", "split_nogps"}) {
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        NavigationView::MapArea area;
        area.x = 129;
        area.y = 88;
        area.width = 765;
        area.height = 323;
        (void)view.set_split_area(true, area);
        if (std::strcmp(name, "split_free") != 0) {
            to_preview(view);
            (void)view.start_guidance(0);
            GuidanceState state;
            state.position_available = std::strcmp(name, "split_nogps") != 0;
            state.recalculating = std::strcmp(name, "split_recalc") == 0;
            state.distance_to_maneuver_metres = 300.0;
            state.remaining_metres = 9400.0;
            state.remaining_seconds = 14 * 60.0;
            state.maneuver.kind = ManeuverKind::Right;
            state.maneuver.road = "бульвар Тараса Шевченка";
            state.has_following_maneuver = true;
            state.following_maneuver.kind = ManeuverKind::Left;
            state.following_maneuver.road = "вулиця Антоновича";
            state.speed_limit_kmh = 50;
            (void)view.apply_guidance(state);
        }
        settle(view);
        ok = write(view, out + "/" + name + ".argb") && ok;
    }
    std::puts(ok ? "ui-render-tool: ok" : "ui-render-tool: write failed");
    return ok ? 0 : 1;
}
