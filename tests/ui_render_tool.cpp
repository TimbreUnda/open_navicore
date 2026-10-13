// Renders the navigation UI in the design's reference states and writes each
// LVGL canvas as raw ARGB8888 (OUT_DIR/<state>.argb, 1024x574), for pixel
// comparison against the Uconnect navigation mockups:
//   ui-render-tool OUT_DIR
#include "ui/navigation_model.h"
#include "ui/navigation_view.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace navicore_osmand::ui;

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

const Destination ocean{"ocean", "Ocean Plaza", "Antonovycha St 176 · Shopping centre",
                        "3.1 km", 50.4123, 30.5221};

// Search -> preview through the real model transitions.
void to_preview(NavigationView& view)
{
    const auto search = view.set_query("Ocean");
    (void)view.apply_search_results(search, {ocean});
    const auto route_revision = view.select_result(0);
    RouteAlternative route;
    route.id = "fast";
    route.duration = "9 min";
    route.distance = "2.4 km";
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
    bool ok = true;
    {
        NavigationModel model;
        model.set_recents({ocean,
                           {"rail", "Kyiv-Pasazhyrskyi", "Railway station", "2.4 km", 50.44, 30.49},
                           {"olena", "Home of Olena", "Saksahanskoho St 41", "1.2 km", 50.43, 30.51}});
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
        {"s1", "Khreshchatyk St", "Street · Shevchenkivskyi, Kyiv", "1.9 km", 50.44, 30.52},
        {"s2", "Khreshchatyk St 22", "House · Kyiv", "2.0 km", 50.44, 30.52},
        {"s3", "Khreshchatyk St 15", "House · Kyiv", "2.1 km", 50.44, 30.52},
        {"s4", "Khreshchatyk Park", "Park · Kyiv", "2.6 km", 50.44, 30.52}};
    for (const bool lock : {false, true}) {
        NavigationModel model;
        model.set_recents({ocean,
                           {"rail", "Kyiv-Pasazhyrskyi", "Railway station", "2.4 km", 50.44, 30.49},
                           {"olena", "Home of Olena", "Saksahanskoho St 41", "1.2 km", 50.43, 30.51},
                           {"arsenalna", "Arsenalna", "Metro station", "4.8 km", 50.44, 30.54},
                           {"kbp", "Boryspil Airport (KBP)", "Airport · Boryspil", "34 km", 50.34, 30.89}});
        NavigationView view(model, 1024, 574);
        view.open_search();
        const auto revision = view.set_query("Khreshchatyk");
        (void)view.apply_search_results(revision, results);
        if (lock)
            (void)view.set_moving(true);
        settle(view);
        ok = write(view, out + (lock ? "/lock.argb" : "/search.argb")) && ok;
    }
    std::puts(ok ? "ui-render-tool: ok" : "ui-render-tool: write failed");
    return ok ? 0 : 1;
}
