#include "guidance_tracker.h"
#include "ui/compact_text.h"
#include "ui/navigation_model.h"
#include "ui/navigation_view.h"
#include "ui/overlay_occupancy.h"
#include "ui/recents_store.h"
#include <open_navicore/backend.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace navicore_osmand::ui;
using navicore_osmand::GuidanceTracker;

namespace {
bool covered(const OverlayOccupancy& occupancy, std::uint32_t x, std::uint32_t y)
{
    for (const auto& rect : occupancy.regions())
        if (x >= rect.x && x < rect.x + rect.width &&
            y >= rect.y && y < rect.y + rect.height)
            return true;
    return false;
}

// Every non-transparent pixel must lie inside a blended region.
bool covers_all_visible(const OverlayOccupancy& occupancy,
                        const std::uint32_t* pixels, std::uint32_t width,
                        std::uint32_t height)
{
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x)
            if ((pixels[y * width + x] >> 24) != 0u && !covered(occupancy, x, y))
                return false;
    return true;
}

std::uint64_t covered_area(const OverlayOccupancy& occupancy)
{
    std::uint64_t area = 0;
    for (const auto& rect : occupancy.regions())
        area += static_cast<std::uint64_t>(rect.width) * rect.height;
    return area;
}

void test_overlay_occupancy()
{
    constexpr std::uint32_t width = 64;
    constexpr std::uint32_t height = 40;
    std::vector<std::uint32_t> pixels(width * height, 0u);
    OverlayOccupancy occupancy(8u, 1u);  // 1 px cells: exact spans
    occupancy.update(pixels.data(), width, height, 0u, height);
    assert(occupancy.regions().empty());
    assert(occupancy.revision() == 1u);

    // A single semi-transparent pixel occupies exactly its 8-row band.
    pixels[10 * width + 5] = 0x01000000u;
    occupancy.update(pixels.data(), width, height, 10u, 1u);
    assert(occupancy.regions().size() == 1u);
    assert(occupancy.regions()[0].x == 5u && occupancy.regions()[0].width == 1u);
    assert(occupancy.regions()[0].y == 8u && occupancy.regions()[0].height == 8u);

    // An unchanged rescan does not bump the revision.
    const auto revision = occupancy.revision();
    occupancy.update(pixels.data(), width, height, 0u, height);
    assert(occupancy.revision() == revision);

    // Equal spans in adjacent bands merge; a different span starts a new quad.
    for (std::uint32_t y = 0; y < 24u; ++y)
        pixels[y * width + 5] = 0xff000000u;
    pixels[30 * width + 60] = 0xff000000u;
    occupancy.update(pixels.data(), width, height, 0u, height);
    assert(occupancy.regions().size() == 2u);
    assert(occupancy.regions()[0].y == 0u && occupancy.regions()[0].height == 24u);
    assert(occupancy.regions()[1].x == 60u && occupancy.regions()[1].y == 24u);
    assert(covers_all_visible(occupancy, pixels.data(), width, height));

    // Clearing pixels shrinks the regions again; out-of-range dirt is ignored.
    std::fill(pixels.begin(), pixels.end(), 0u);
    occupancy.update(pixels.data(), width, height, 0u, height);
    assert(occupancy.regions().empty());
    occupancy.update(pixels.data(), width, height, height + 5u, 3u);
    assert(occupancy.regions().empty());
}
void settle(NavigationView& view, int frames)
{
    for (int frame = 0; frame < frames; ++frame)
        (void)view.render(16);
}

// Holds each phase for 64 ms like a real touch: LVGL reads input on a 33 ms
// timer, so two 16 ms frames can miss the first press after a new view.
void tap(NavigationView& view, int32_t x, int32_t y)
{
    view.pointer(MOCK_NAV_BACKEND_POINTER_PRESS, x, y);
    settle(view, 4);
    view.pointer(MOCK_NAV_BACKEND_POINTER_RELEASE, x, y);
    settle(view, 4);
}

std::vector<MapCommand> commands(NavigationModel& model)
{
    std::vector<MapCommand> output;
    model.take_map_commands(output);
    return output;
}

bool opaque_near(const NavigationView& view, int x, int y)
{
    for (int dy = -4; dy <= 4; ++dy)
        for (int dx = -4; dx <= 4; ++dx)
            if ((view.pixels()[(y + dy) * static_cast<int>(view.width()) +
                               x + dx] >> 24) != 0u)
                return true;
    return false;
}

Destination place(const char* id, const char* title, double latitude)
{
    return {id, title, "Recent", "", latitude, 30.5};
}

void test_car_controls()
{
    // Layout constants mirrored from the view: 64 px buttons, 8 px gap,
    // 16 px margin, column at the right edge of the 1024x574 band.
    constexpr int column_x = 1024 - 16 - 32;
    const auto button_y = [](int index) { return 16 + 32 + index * 72; };
    {
        NavigationModel model;
        NavigationView view(model, 1024, 574);
        settle(view, 31);
        const auto area = view.map_area();
        assert(area.x == 336 && area.width == 688 && area.height == 574);

        tap(view, column_x, button_y(0));
        tap(view, column_x, button_y(1));
        tap(view, column_x, button_y(2));
        tap(view, column_x, button_y(3));
        tap(view, column_x, button_y(4));
        const auto taken = commands(model);
        assert((taken == std::vector<MapCommand>{
            MapCommand::ZoomIn, MapCommand::ZoomOut,
            MapCommand::ToggleOrientation, MapCommand::ToggleTilt,
            MapCommand::ToggleNight}));

        // Recenter is hidden while following, shown when the camera is free.
        tap(view, column_x, 574 - 16 - 32);
        assert(commands(model).empty());
        MapViewState free_view;
        free_view.following = false;
        assert(model.set_map_view(free_view));
        assert(!model.set_map_view(free_view));
        view.apply_map_view();
        settle(view, 2);
        tap(view, column_x, 574 - 16 - 32);
        assert((commands(model) == std::vector<MapCommand>{MapCommand::Recenter}));

        // Dragging bare map pans; dragging from a control does not.
        int dx = 0;
        int dy = 0;
        view.pointer(MOCK_NAV_BACKEND_POINTER_PRESS, 600, 300);
        view.pointer(MOCK_NAV_BACKEND_POINTER_MOVE, 640, 320);
        view.pointer(MOCK_NAV_BACKEND_POINTER_RELEASE, 650, 330);
        settle(view, 4);
        assert(model.take_pan(dx, dy) && dx == 50 && dy == 30);
        assert(!model.take_pan(dx, dy));
        view.pointer(MOCK_NAV_BACKEND_POINTER_PRESS, column_x, button_y(0));
        view.pointer(MOCK_NAV_BACKEND_POINTER_MOVE, column_x - 60, 300);
        view.pointer(MOCK_NAV_BACKEND_POINTER_RELEASE, column_x - 60, 300);
        settle(view, 4);
        assert(!model.take_pan(dx, dy));
        (void)commands(model);

        // The camera-driven marker draws inside the map area only.
        assert(!opaque_near(view, 680, 400));
        view.set_vehicle_marker(true, 680, 400, 45.0f);
        settle(view, 1);
        assert(opaque_near(view, 680, 400));
        // Moving the marker dirties only map-side areas, not the rail.
        view.set_vehicle_marker(true, 700, 420, 45.0f);
        settle(view, 1);
        assert(!view.dirty_regions().empty());
        for (const auto& region : view.dirty_regions())
            assert(region.x >= 336u && region.width < 200u);
        view.set_vehicle_marker(true, 100, 300, 0.0f);
        settle(view, 1);
        assert(!opaque_near(view, 680, 400));
    }
    {
        // Recents: newest first, de-duplicated, capped, and one tap routes.
        NavigationModel model;
        std::vector<Destination> many;
        for (int i = 0; i < 7; ++i)
            many.push_back(place(("p" + std::to_string(i)).c_str(), "Place",
                                 50.0 + i));
        model.set_recents(many);
        assert(model.recents().size() == NavigationModel::maximum_recents);
        model.set_recents({place("home", "Home", 50.4), place("work", "Work", 50.5)});
        NavigationView view(model, 1024, 574);
        settle(view, 31);
        // Recent rows follow "Where to?" (64), shortcuts (80) and the label.
        const auto revision = model.route_revision();
        tap(view, 160, 16 + 64 + 12 + 80 + 12 + 17 + 12 + 32);
        assert(model.screen() == Screen::Search);
        assert(model.destination() && model.destination()->id == "home");
        assert(model.route_revision() > revision);

        // Starting guidance moves the destination to the front once.
        RouteAlternative route;
        route.geometry = {{50.0, 30.0}, {50.01, 30.0}};
        assert(model.apply_routes(model.route_revision(), {route}));
        assert(model.start_guidance());
        assert(model.recents().front().id == "home");
        assert(model.recents().size() == 2);
        assert(model.select_recent(0) == 0); // not during guidance
    }
    {
        // Driving lock: no keyboard, read-only query, recents still usable.
        NavigationModel model;
        model.set_recents({place("home", "Home", 50.4)});
        NavigationView view(model, 1024, 574);
        settle(view, 31);
        view.open_search();
        settle(view, 2);
        assert(view.set_moving(true));
        assert(!view.set_moving(true));
        settle(view, 2);
        const auto query = model.query();
        tap(view, 700, 400);          // where the keyboard would be
        assert(model.query() == query);
        assert(model.destination() == nullptr);
        // Locked panel: 20 px pad, notice (24), label, then the recent row.
        tap(view, 600, 80 + 20 + 24 + 10 + 17 + 10 + 32);
        assert(model.destination() && model.destination()->id == "home");
        assert(view.set_moving(false));
    }
}
} // namespace

void test_recents_store()
{
    const std::string path = "/tmp/open-navicore-recents-test.tsv";
    std::remove(path.c_str());
    RecentsStore store(path);
    assert(store.load().empty());
    std::vector<Destination> recents = {
        {"home", "Home\tSweet", "Street 1\n", "", 50.4501, 30.5234},
        {"", "Work", "", "", -33.9, 151.2}};
    assert(store.save(recents));
    const auto loaded = store.load();
    assert(loaded.size() == 2);
    assert(loaded[0].id == "home" && loaded[0].title == "Home Sweet");
    assert(loaded[0].subtitle == "Street 1 ");
    assert(std::fabs(loaded[0].latitude - 50.4501) < 1e-9);
    assert(loaded[1].id.empty() && loaded[1].title == "Work");
    // Malformed and out-of-range lines are skipped, not fatal.
    {
        std::FILE* file = std::fopen(path.c_str(), "w");
        std::fputs("bad line\nx\tTitle\t\t95\t10\nok\tOk\t\t1.5\t2.5\n", file);
        std::fclose(file);
    }
    const auto partial = store.load();
    assert(partial.size() == 1 && partial[0].id == "ok");
    std::remove(path.c_str());
}

int main()
{
    test_overlay_occupancy();
    test_recents_store();
    test_car_controls();

    assert(compact_font_text(u8"Січневого Повстання") ==
           "Sichnevoho Povstannya");
    assert(compact_font_text("E95") == "E95");

    NavigationModel model;
    assert(model.screen() == Screen::FreeDrive);

    NavigationView view(model, 1024, 574);
    (void)view.render(16);
    assert(std::any_of(view.pixels(),
                       view.pixels() + view.width() * view.height(),
                       [](std::uint32_t pixel) { return pixel != 0u; }));
    {
        OverlayOccupancy occupancy;
        occupancy.update(view.pixels(), view.width(), view.height(), 0u,
                         view.height());
        assert(!occupancy.regions().empty());
        assert(covers_all_visible(occupancy, view.pixels(), view.width(),
                                  view.height()));
        // The free-drive UI leaves most of the map uncovered.
        assert(covered_area(occupancy) * 2u <
               static_cast<std::uint64_t>(view.width()) * view.height());
    }
    // Startup Screen events are quarantined until the first stable frames.
    view.pointer(MOCK_NAV_BACKEND_POINTER_PRESS, 80, 40);
    view.pointer(MOCK_NAV_BACKEND_POINTER_RELEASE, 80, 40);
    (void)view.render(16);
    assert(model.screen() == Screen::FreeDrive);
    for (int frame = 2; frame < 30; ++frame)
        (void)view.render(16);
    // Exercise the real provider action constants through LVGL's input queue.
    view.pointer(MOCK_NAV_BACKEND_POINTER_PRESS, 80, 40);
    (void)view.render(16);
    (void)view.render(16);
    view.pointer(MOCK_NAV_BACKEND_POINTER_RELEASE, 80, 40);
    (void)view.render(16);
    (void)view.render(16);
    assert(model.screen() == Screen::Search);

    const auto stale_revision = view.set_query("River");
    const auto search_revision = view.set_query("Riverside 12");
    assert(!view.apply_search_results(stale_revision,
        {{"stale", "Stale", "Must be rejected", "", 0.0, 0.0}}));
    assert(view.apply_search_results(search_revision,
        {{"house-12", "Riverside Ave 12", "House - Center",
          "1.3 km", 50.4501, 30.5234},
         {"mall", "Riverside Mall", "Riverside Ave 120",
          "9.4 km", 50.43, 30.51}}));
    assert(model.results().size() == 2);
    (void)view.render(16);
    view.pointer(MOCK_NAV_BACKEND_POINTER_PRESS, 100, 120);
    (void)view.render(16);
    (void)view.render(16);
    view.pointer(MOCK_NAV_BACKEND_POINTER_RELEASE, 100, 120);
    (void)view.render(16);
    (void)view.render(16);
    assert(model.destination());
    assert(model.destination()->id == "house-12");
    const auto route_revision = model.route_revision();
    const auto stale_route_revision = route_revision - 1;
    assert(!view.apply_routes(stale_route_revision,
        {{"stale", "99 min", "99 km", "", "Must be rejected", 0.0, 0.0,
          {}, {}}}));
    RouteAlternative fast;
    fast.id = "fast";
    fast.duration = "10 min";
    fast.distance = "1.8 km";
    fast.arrival = "--";
    fast.reason = "Fastest";
    fast.duration_seconds = 600.0;
    fast.distance_metres = 1828.0;
    fast.geometry = {{50.0, 30.0}, {50.0, 30.01}, {50.01, 30.01}};
    fast.maneuvers = {
        {ManeuverKind::Right, "River Road", 716.0, 1, 0},
        {ManeuverKind::Arrive, "Riverside Ave 12", 1828.0, 2, 0}};
    RouteAlternative quiet = fast;
    quiet.id = "quiet";
    quiet.duration = "12 min";
    quiet.reason = "Fewer major roads";
    assert(view.apply_routes(route_revision, {fast, quiet}));
    assert(model.screen() == Screen::RoutePreview);
    assert(model.routes().size() == 2);
    assert(model.select_route(0));
    assert(model.start_guidance());
    assert(model.screen() == Screen::Guidance);
    GuidanceTracker tracker;
    assert(view.apply_guidance(tracker.unavailable(model.routes().front())));
    assert(!model.guidance().position_available);
    assert(model.guidance().maneuver.kind == ManeuverKind::Right);
    assert(view.render(16));
    assert(view.apply_guidance(tracker.update(model.routes().front(),
                                              {50.0, 30.005})));
    assert(model.guidance().position_available);
    assert(model.guidance().progress_metres > 300.0);
    assert(model.guidance().distance_to_maneuver_metres < 400.0);
    const double progress = model.guidance().progress_metres;
    assert(view.apply_guidance(tracker.update(model.routes().front(),
                                              {50.0, 30.004})));
    assert(model.guidance().progress_metres >= progress);
    assert(view.apply_guidance(tracker.update(model.routes().front(),
                                              {50.01, 30.01})));
    assert(model.guidance().arrived);
    assert(model.guidance().maneuver.kind == ManeuverKind::Arrive);
    assert(view.render(16));
    assert(model.end_guidance());
    assert(model.screen() == Screen::FreeDrive);

    model.close_search();
    model.open_search();
    const auto empty_revision = model.set_query("not installed");
    assert(model.apply_search_results(empty_revision, {}));
    assert(model.results().empty());
    assert(model.message() == "No offline results");
    return 0;
}
