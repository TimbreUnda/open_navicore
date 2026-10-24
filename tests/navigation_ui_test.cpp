#include "cluster_payload.h"
#include "guidance_tracker.h"
#include "junction_shape.h"
#include "ui/compact_text.h"
#include "ui/navigation_model.h"
#include "ui/navigation_view.h"
#include "ui/overlay_occupancy.h"
#include "ui/recents_store.h"
#include <open_navicore/backend.h>
#include <open_navicore/cluster.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
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
// One engine frame: input first (real 16 ms), then rendering.
void frame(NavigationView& view)
{
    view.process_input(16);
    (void)view.render(0);
}

void settle(NavigationView& view, int frames)
{
    for (int i = 0; i < frames; ++i)
        frame(view);
}

// Holds each phase for 64 ms like a real touch (LVGL press timing).
void tap(NavigationView& view, int32_t x, int32_t y)
{
    view.pointer(OPEN_NAVI_BACKEND_POINTER_PRESS, x, y);
    settle(view, 4);
    view.pointer(OPEN_NAVI_BACKEND_POINTER_RELEASE, x, y);
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

        // Dragging bare map reports absolute positions from the press point,
        // in the same frame; dragging from a control does not.
        MapDrag drag;
        MapTouch touch;
        // The tap on the hidden recenter button landed on bare map.
        assert(model.take_map_touch(touch) && touch.gesture == MapGesture::Tap &&
               touch.x == column_x);
        assert(!model.take_map_touch(touch));
        view.pointer(OPEN_NAVI_BACKEND_POINTER_PRESS, 600, 300);
        view.pointer(OPEN_NAVI_BACKEND_POINTER_MOVE, 640, 320);
        frame(view);
        assert(model.take_drag(drag) && drag.started && drag.active);
        assert(drag.start_x == 600 && drag.start_y == 300 && drag.x == 640 && drag.y == 320);
        view.pointer(OPEN_NAVI_BACKEND_POINTER_RELEASE, 650, 330);
        frame(view);
        assert(model.take_drag(drag) && !drag.started && !drag.active);
        assert(drag.x == 650 && drag.y == 330);
        assert(!model.take_drag(drag));
        assert(!model.take_map_touch(touch));
        // A small wobble is still a tap, not a drag.
        view.pointer(OPEN_NAVI_BACKEND_POINTER_PRESS, 600, 300);
        view.pointer(OPEN_NAVI_BACKEND_POINTER_MOVE, 604, 303);
        settle(view, 2);
        view.pointer(OPEN_NAVI_BACKEND_POINTER_RELEASE, 604, 303);
        settle(view, 2);
        assert(!model.take_drag(drag));
        assert(model.take_map_touch(touch) && touch.gesture == MapGesture::Tap &&
               touch.x == 600 && touch.y == 300);
        assert(!model.take_map_touch(touch));
        view.pointer(OPEN_NAVI_BACKEND_POINTER_PRESS, column_x, button_y(0));
        view.pointer(OPEN_NAVI_BACKEND_POINTER_MOVE, column_x - 60, 300);
        view.pointer(OPEN_NAVI_BACKEND_POINTER_RELEASE, column_x - 60, 300);
        settle(view, 4);
        assert(!model.take_drag(drag));
        assert(!model.take_map_touch(touch));
        (void)commands(model);
        settle(view, 30);  // past the double-tap window

        // A second tap close by and soon is a double tap at the first tap;
        // the first tap still acts at once.
        tap(view, 600, 300);
        tap(view, 610, 296);
        assert(model.take_map_touch(touch) && touch.gesture == MapGesture::Tap);
        assert(model.take_map_touch(touch) && touch.gesture == MapGesture::DoubleTap &&
               touch.x == 600 && touch.y == 300);
        assert(!model.take_map_touch(touch));
        // A third tap starts over; one far away or late is just a tap.
        tap(view, 600, 300);
        tap(view, 700, 300);
        settle(view, 30);
        tap(view, 700, 300);
        for (int i = 0; i < 3; ++i)
            assert(model.take_map_touch(touch) && touch.gesture == MapGesture::Tap);
        assert(!model.take_map_touch(touch));
        settle(view, 30);

        // Holding still for 600 ms is a long press, noticed while the
        // finger rests (frames keep coming); no tap or drag follows.
        view.pointer(OPEN_NAVI_BACKEND_POINTER_PRESS, 620, 280);
        frame(view);
        assert(view.wants_frames());
        settle(view, 30);  // 480 ms
        assert(!model.take_map_touch(touch));
        settle(view, 10);
        assert(model.take_map_touch(touch) && touch.gesture == MapGesture::LongPress &&
               touch.x == 620 && touch.y == 280);
        assert(!view.wants_frames());
        view.pointer(OPEN_NAVI_BACKEND_POINTER_MOVE, 700, 300);
        view.pointer(OPEN_NAVI_BACKEND_POINTER_RELEASE, 700, 300);
        settle(view, 2);
        assert(!model.take_map_touch(touch) && !model.take_drag(drag));
        // A second finger turns a drag into a pinch: the drag ends, the
        // pinch reports both fingers, and lifting either ends it with no
        // tap, drag or long press from the finger left down.
        MapPinch pinch;
        view.pointer(OPEN_NAVI_BACKEND_POINTER_PRESS, 500, 300);
        view.pointer(OPEN_NAVI_BACKEND_POINTER_MOVE, 540, 300);
        frame(view);
        assert(model.take_drag(drag) && drag.started);
        view.pointer(OPEN_NAVI_BACKEND_POINTER_PRESS, 700, 300, 1);
        frame(view);
        assert(model.take_drag(drag) && !drag.active);
        assert(model.take_pinch(pinch) && pinch.started && pinch.active);
        assert(pinch.x0 == 540 && pinch.x1 == 700);
        view.pointer(OPEN_NAVI_BACKEND_POINTER_MOVE, 760, 320, 1);
        view.pointer(OPEN_NAVI_BACKEND_POINTER_MOVE, 520, 290);
        frame(view);
        assert(model.take_pinch(pinch) && !pinch.started && pinch.active);
        assert(pinch.x0 == 520 && pinch.y0 == 290 && pinch.x1 == 760 && pinch.y1 == 320);
        assert(!model.take_drag(drag));
        view.pointer(OPEN_NAVI_BACKEND_POINTER_RELEASE, 760, 320, 1);
        frame(view);
        assert(model.take_pinch(pinch) && !pinch.active);
        view.pointer(OPEN_NAVI_BACKEND_POINTER_MOVE, 400, 200);
        settle(view, 45);  // past the long-press time
        view.pointer(OPEN_NAVI_BACKEND_POINTER_RELEASE, 400, 200);
        settle(view, 2);
        assert(!model.take_pinch(pinch) && !model.take_drag(drag));
        assert(!model.take_map_touch(touch));
        // A second finger alone (first one on a control) does nothing.
        view.pointer(OPEN_NAVI_BACKEND_POINTER_PRESS, column_x, button_y(0));
        view.pointer(OPEN_NAVI_BACKEND_POINTER_PRESS, 600, 300, 1);
        view.pointer(OPEN_NAVI_BACKEND_POINTER_RELEASE, 600, 300, 1);
        view.pointer(OPEN_NAVI_BACKEND_POINTER_RELEASE, column_x, button_y(0));
        settle(view, 4);
        assert(!model.take_pinch(pinch));
        (void)commands(model);
        settle(view, 30);

        // Moving past the slop first makes it a drag, never a long press.
        view.pointer(OPEN_NAVI_BACKEND_POINTER_PRESS, 620, 280);
        view.pointer(OPEN_NAVI_BACKEND_POINTER_MOVE, 660, 280);
        settle(view, 45);
        assert(!view.wants_frames());
        view.pointer(OPEN_NAVI_BACKEND_POINTER_RELEASE, 660, 280);
        settle(view, 2);
        assert(!model.take_map_touch(touch));
        assert(model.take_drag(drag) && !drag.active);

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
        // Recent rows follow "Where to?" (16 + 64 + 8) and the card's
        // RECENT heading (12 + 22): the first row spans y 122..186.
        // A recent destination routes directly: free drive stays, the card
        // shows the calculation, no search screen.
        const auto revision = model.route_revision();
        tap(view, 160, 122 + 32);
        assert(model.screen() == Screen::FreeDrive);
        assert(model.direct_routing() && model.calculating_route());
        assert(model.place_card() && model.place_card()->id == "home");
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
        assert(model.select_place(place("fuel", "Fuel", 50.4)) == 0);
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
        // Locked panel replaces the keyboard: recents in two columns, the
        // first row at y 336..400 on the left.
        tap(view, 200, 336 + 32);
        assert(model.destination() && model.destination()->id == "home");
        assert(view.set_moving(false));
    }
}

void test_place_card_and_back()
{
    NavigationModel model;
    NavigationView view(model, 1024, 574);
    settle(view, 31);
    Destination wog{"poi:1,2", "WOG", "Fuel · 0.4 km", "", 50.44, 30.52};
    // Close (left button) hides the card; the screen stays free drive.
    assert(view.show_place_card(wog, "amenity_fuel"));
    settle(view, 2);
    assert(model.place_card() && model.place_card()->title == "WOG");
    tap(view, 16 + 16 + 71, 574 - 16 - 160 + 80 + 32);
    assert(!model.place_card() && model.screen() == Screen::FreeDrive);
    // Navigate (right button) routes to the place from free drive: no
    // search screen, the card says it is calculating.
    assert(view.show_place_card(wog, "amenity_fuel"));
    settle(view, 2);
    const int navigate_x = 16 + 166 + 99, navigate_y = 574 - 16 - 160 + 80 + 32;
    tap(view, navigate_x, navigate_y);
    assert(model.screen() == Screen::FreeDrive);
    assert(model.destination() && model.destination()->id == "poi:1,2");
    assert(model.place_card() && model.place_card_icon() == "amenity_fuel");
    assert(model.calculating_route());
    const auto calculating = model.route_revision();
    settle(view, 2);
    tap(view, navigate_x, navigate_y);  // disabled while calculating
    assert(model.route_revision() == calculating);

    // A failure stays on the card; Navigate tries again.
    assert(view.apply_routes(calculating, {}));
    assert(model.screen() == Screen::FreeDrive && model.place_card());
    assert(!model.calculating_route() && !model.message().empty());
    settle(view, 2);
    tap(view, navigate_x, navigate_y);
    assert(model.calculating_route() && model.route_revision() > calculating);

    // The route opens Route preview; a stale result does not.
    RouteAlternative route;
    route.duration = "5 min";
    route.distance = "1.0 km";
    route.geometry = {{50.0, 30.0}, {50.01, 30.0}};
    assert(!view.apply_routes(calculating, {route}));
    assert(view.apply_routes(model.route_revision(), {route}));
    assert(model.screen() == Screen::RoutePreview);
    // Place cards only open over free drive.
    assert(!view.show_place_card(wog, "amenity_fuel"));

    // Route preview Back returns to free drive with the place card.
    settle(view, 2);
    tap(view, 16 + 40, 16 + 32);
    assert(model.screen() == Screen::FreeDrive);
    assert(model.routes().empty() && !model.destination());
    assert(model.place_card() && model.place_card()->id == "poi:1,2");
    assert(!model.direct_routing());

    // Close while calculating cancels the route.
    settle(view, 2);
    tap(view, navigate_x, navigate_y);
    const auto cancelled = model.route_revision();
    assert(view.close_place_card());
    assert(!model.destination() && model.route_revision() > cancelled);
    assert(!view.apply_routes(cancelled, {route}));
    assert(model.screen() == Screen::FreeDrive);

    // A dropped pin is a place card too: its address arrives later (for
    // that pin only), and Navigate routes to it.
    Destination pin{"pin:50.44000, 30.52000", "Dropped pin", "50.44000, 30.52000", "",
                    50.44, 30.52};
    assert(view.show_place_card(pin, "pin"));
    assert(model.place_card_is_pin());
    assert(!view.set_place_subtitle("pin:other", "вулиця Інша"));
    assert(view.set_place_subtitle(pin.id, "вулиця Хрещатик, 22 · 0.4 km"));
    assert(model.place_card()->subtitle == "вулиця Хрещатик, 22 · 0.4 km");
    assert(view.select_place(*model.place_card()));
    assert(model.destination() && model.destination()->id == pin.id);
    assert(view.close_place_card() && !model.place_card_is_pin());
    view.set_pin_marker(true, 600, 300);  // no card, no crash; hidden on rebuild
    settle(view, 2);

    // From Search the old flow stays: preview Back returns to the results.
    view.open_search();
    (void)model.set_query("WOG");
    assert(view.apply_search_results(model.search_revision(), {wog}));
    assert(view.select_result(0));
    assert(view.apply_routes(model.route_revision(), {route}));
    assert(model.screen() == Screen::RoutePreview);
    settle(view, 2);
    tap(view, 16 + 40, 16 + 32);
    assert(model.screen() == Screen::Search);
    assert(model.routes().empty() && !model.destination());
}

// The provider's serializer rules (src/runtime/open_navi_cluster_backend.c):
// only supported fields, values inside the recovered ranges.
void check_cluster_valid(const navicore_osmand::ClusterPayload& payload)
{
    const auto& c = payload.cluster;
    const unsigned valid = c.valid_attributes;
    assert(c.abi_version == OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1 && c.struct_size == sizeof(c));
    assert((valid & ~unsigned(OPEN_NAVI_CLUSTER_SEMANTIC_SUPPORTED_MASK)) == 0u);
    if (valid & OPEN_NAVI_CLUSTER_ROUTE_GUIDANCE_STATE)
        assert(c.route_guidance_state >= 1 && c.route_guidance_state <= 7);
    if (valid & OPEN_NAVI_CLUSTER_NAVIGATION_STATUS)
        assert(c.navigation_status >= 1 && c.navigation_status <= 8);
    if (valid & OPEN_NAVI_CLUSTER_DISTANCE_DESTINATION)
        assert(c.distance_destination_unit >= 1 && c.distance_destination_unit <= 5 &&
               c.distance_destination <= 65535u);
    if (valid & OPEN_NAVI_CLUSTER_DISTANCE_MANEUVER)
        assert(c.distance_maneuver_unit >= 1 && c.distance_maneuver_unit <= 5 &&
               c.distance_maneuver_tenths <= 65535u);
    if (valid & OPEN_NAVI_CLUSTER_BAR_PERCENT)
        assert(c.bar_percent <= 100);
    if (valid & OPEN_NAVI_CLUSTER_ROUTE_SYMBOL)
        assert(c.route_guidance_symbol >= OPEN_NAVI_CLUSTER_SYMBOL_MIN &&
               c.route_guidance_symbol <= OPEN_NAVI_CLUSTER_SYMBOL_MAX);
    else
        assert(c.route_guidance_symbol == 0);
    // The cluster draws its turn view only with both bits (live, 2026-10-19).
    if (valid & OPEN_NAVI_CLUSTER_ROUTE_GUIDANCE_STATE)
        assert((valid & OPEN_NAVI_CLUSTER_ROUTE_SYMBOL) &&
               (valid & OPEN_NAVI_CLUSTER_BAR_PERCENT));
    if (valid & OPEN_NAVI_CLUSTER_COMPASS)
        assert(c.compass >= 1 && c.compass <= 8);
    if (valid & OPEN_NAVI_CLUSTER_SPEED_LIMIT)
        assert(c.speed_unit >= 1 && c.speed_unit <= 2);
    assert(c.current_road.byte_count == payload.current_road.size());
    assert(c.next_road.byte_count == payload.next_road.size());
    assert(c.reserved0[0] == 0);
}

void test_cluster_payload()
{
    using navicore_osmand::ClusterInputs;
    using navicore_osmand::build_cluster_payload;
    ClusterInputs in;
    in.now = 12 * 3600;  // fixed clock
    // Not guiding: "no route"; while a route is calculated: "calculation".
    auto p = build_cluster_payload(in);
    check_cluster_valid(p);
    assert(p.cluster.valid_attributes == OPEN_NAVI_CLUSTER_NAVIGATION_STATUS);
    assert(p.cluster.navigation_status == OPEN_NAVI_CLUSTER_NAV_NO_ROUTE);
    in.calculating_route = true;
    assert(build_cluster_payload(in).cluster.navigation_status ==
           OPEN_NAVI_CLUSTER_NAV_ROUTE_CALCULATION);
    // Guidance without a fix.
    in.screen = Screen::Guidance;
    p = build_cluster_payload(in);
    check_cluster_valid(p);
    assert(p.cluster.navigation_status == OPEN_NAVI_CLUSTER_NAV_DIRECTION_NOT_AVAILABLE);
    // 250 m before a left turn, 2.4 km and 7 min to go, heading east.
    in.guidance.position_available = true;
    in.guidance.distance_to_maneuver_metres = 250.0;
    in.guidance.maneuver.kind = ManeuverKind::Left;
    in.guidance.maneuver.road = "вулиця Антоновича";
    in.guidance.road = "бульвар Тараса Шевченка";
    in.guidance.remaining_metres = 2400.0;
    in.guidance.remaining_seconds = 420.0;
    in.guidance.speed_limit_kmh = 50;
    in.heading_valid = true;
    in.heading_degrees = 90.0;
    p = build_cluster_payload(in);
    check_cluster_valid(p);
    const auto& c = p.cluster;
    assert(c.route_guidance_state == OPEN_NAVI_CLUSTER_ROUTE_MANOEUVRE_ANNOUNCEMENT);
    assert(!(c.valid_attributes & OPEN_NAVI_CLUSTER_NAVIGATION_STATUS));
    assert(c.distance_destination == 2 &&
           c.distance_destination_unit == OPEN_NAVI_CLUSTER_DISTANCE_KILOMETRES);
    assert(c.distance_maneuver_tenths == 2500 &&
           c.distance_maneuver_unit == OPEN_NAVI_CLUSTER_DISTANCE_METRES);
    assert(c.rta_hour == 0 && c.rta_minute == 7);
    assert(c.valid_attributes & OPEN_NAVI_CLUSTER_ETA);
    assert(c.bar_percent == 63);
    assert(c.route_guidance_symbol == OPEN_NAVI_CLUSTER_SYMBOL_LEFT);
    assert(c.compass == OPEN_NAVI_CLUSTER_COMPASS_E);
    assert(c.speed_limit == 50 && c.speed_unit == OPEN_NAVI_CLUSTER_SPEED_KMH);
    assert(p.current_road == "бульвар Тараса Шевченка" && p.next_road == "вулиця Антоновича");
    assert(std::string(c.current_road.bytes, c.current_road.byte_count) == p.current_road);
    // A copy compares equal and rebinds its spans to its own strings.
    auto copy = p;
    copy.bind();
    assert(copy.same_as(p) && copy.cluster.current_road.bytes == copy.current_road.data());
    // At the turn; far from it (no countdown); km distances in tenths.
    in.guidance.distance_to_maneuver_metres = 30.0;
    assert(build_cluster_payload(in).cluster.route_guidance_state ==
           OPEN_NAVI_CLUSTER_ROUTE_MANOEUVRE);
    in.guidance.distance_to_maneuver_metres = 1250.0;
    p = build_cluster_payload(in);
    check_cluster_valid(p);
    assert(p.cluster.route_guidance_state == OPEN_NAVI_CLUSTER_ROUTE_FOLLOW_STREET);
    // Far from the turn the bar stays valid (full); the cluster needs it.
    assert((p.cluster.valid_attributes & OPEN_NAVI_CLUSTER_BAR_PERCENT) &&
           p.cluster.bar_percent == 100);
    assert(p.cluster.distance_maneuver_tenths == 13 &&
           p.cluster.distance_maneuver_unit == OPEN_NAVI_CLUSTER_DISTANCE_KILOMETRES);
    assert(!p.same_as(copy));
    // Compass sectors around north.
    const std::vector<std::pair<double, int>> sectors = {
        {0.0, OPEN_NAVI_CLUSTER_COMPASS_N}, {359.0, OPEN_NAVI_CLUSTER_COMPASS_N},
        {337.4, OPEN_NAVI_CLUSTER_COMPASS_NW}, {22.6, OPEN_NAVI_CLUSTER_COMPASS_NE},
        {-90.0, OPEN_NAVI_CLUSTER_COMPASS_W}, {180.0, OPEN_NAVI_CLUSTER_COMPASS_S}};
    for (const auto& sector : sectors) {
        in.heading_degrees = sector.first;
        assert(build_cluster_payload(in).cluster.compass == sector.second);
    }
    // Recalculating; arrived (no distances).
    in.guidance.recalculating = true;
    p = build_cluster_payload(in);
    assert(p.cluster.route_guidance_state == OPEN_NAVI_CLUSTER_ROUTE_RECALCULATION &&
           p.cluster.navigation_status == OPEN_NAVI_CLUSTER_NAV_ROUTE_CALCULATION);
    check_cluster_valid(p);
    assert(p.cluster.route_guidance_symbol == OPEN_NAVI_CLUSTER_SYMBOL_RECALCULATING);
    in.guidance.recalculating = false;
    in.guidance.arrived = true;
    p = build_cluster_payload(in);
    check_cluster_valid(p);
    assert(p.cluster.route_guidance_state == OPEN_NAVI_CLUSTER_ROUTE_DESTINATION_REACHED);
    assert(!(p.cluster.valid_attributes &
             (OPEN_NAVI_CLUSTER_DISTANCE_DESTINATION | OPEN_NAVI_CLUSTER_DISTANCE_MANEUVER)));
    assert(p.cluster.route_guidance_symbol == OPEN_NAVI_CLUSTER_SYMBOL_DESTINATION_REACHED);
    assert((p.cluster.valid_attributes & OPEN_NAVI_CLUSTER_BAR_PERCENT) &&
           p.cluster.bar_percent == 0);
}

// Every manoeuvre kind maps to a picture of the live symbol table
// (analysis Cluster_TBT_Symbols.md).
void test_cluster_symbols()
{
    using navicore_osmand::cluster_symbol;
    using navicore_osmand::roundabout_symbol;
    using navicore_osmand::redraw_symbol;
    navicore_osmand::ui::GuidanceState g;
    const std::vector<std::pair<ManeuverKind, int>> kinds = {
        {ManeuverKind::Straight, 9}, {ManeuverKind::Left, 1},
        {ManeuverKind::SlightLeft, 3}, {ManeuverKind::SharpLeft, 5},
        {ManeuverKind::Right, 2}, {ManeuverKind::SlightRight, 4},
        {ManeuverKind::SharpRight, 6}, {ManeuverKind::KeepLeft, 7},
        {ManeuverKind::KeepRight, 8}, {ManeuverKind::UTurn, 10},
        {ManeuverKind::Arrive, 15}};
    for (const auto& kind : kinds) {
        g.maneuver.kind = kind.first;
        assert(cluster_symbol(g) == kind.second);
    }
    // OsmAnd TRU (left_side): the right U-turn.
    g.maneuver.kind = ManeuverKind::UTurn;
    g.maneuver.left_side = true;
    assert(cluster_symbol(g) == 28);
    // Roundabouts by exit angle (positive = clockwise, right), nearest
    // clock position; RHT 19..26, LHT (OsmAnd RNLB) 36..43.
    const std::vector<std::pair<double, int>> exits = {
        {0.0, 19}, {10.0, 19}, {-14.0, 19}, {25.0, 23}, {44.0, 23},
        {80.0, 21}, {95.0, 21}, {115.0, 25}, {140.0, 25}, {170.0, 20},
        {180.0, 20}, {-180.0, 20}, {-175.0, 20}, {-160.0, 26}, {-140.0, 26},
        {-100.0, 22}, {-75.0, 22}, {-40.0, 24}, {-20.0, 24}, {450.0, 21},
        {-270.0, 21}, {359.0, 19}};
    for (const auto& exit : exits) {
        assert(roundabout_symbol(exit.first, false) == exit.second);
        assert(roundabout_symbol(exit.first, true) == exit.second + 17);
    }
    assert(roundabout_symbol(std::nan(""), false) == 19);
    g.maneuver.kind = ManeuverKind::Roundabout;
    g.maneuver.left_side = false;
    g.maneuver.turn_angle_degrees = 92.0;
    assert(cluster_symbol(g) == 21);
    g.maneuver.left_side = true;
    assert(cluster_symbol(g) == 38);
    // Arrived and recalculating win over the manoeuvre.
    g.recalculating = true;
    assert(cluster_symbol(g) == 16);
    g.arrived = true;
    assert(cluster_symbol(g) == 27);
    // The redraw symbol is always another valid picture.
    for (int symbol = OPEN_NAVI_CLUSTER_SYMBOL_MIN; symbol <= OPEN_NAVI_CLUSTER_SYMBOL_MAX;
         ++symbol) {
        const int other = redraw_symbol(static_cast<uint8_t>(symbol));
        assert(other != symbol && other >= OPEN_NAVI_CLUSTER_SYMBOL_MIN &&
               other <= OPEN_NAVI_CLUSTER_SYMBOL_MAX);
    }
    assert(redraw_symbol(1) == 50 && redraw_symbol(2) == 49 && redraw_symbol(9) == 56);
    assert(redraw_symbol(21) == 38 && redraw_symbol(38) == 21);
}

// Junction shapes (junction_shape.h): angles relative to the incoming
// direction, positive = right. Lanes: 'R' recommended, '.' not, left to
// right.
struct JunctionCase {
    ManeuverKind kind;
    double route;
    std::vector<double> arms;
    const char* lanes;
    int expected;
};

Maneuver junction_maneuver(const JunctionCase& c)
{
    Maneuver m;
    m.kind = c.kind;
    m.turn_angle_degrees = c.route;
    for (const char* l = c.lanes; *l; ++l) {
        Lane lane;
        lane.recommended = *l == 'R';
        m.lanes.push_back(lane);
    }
    m.junction = navicore_osmand::make_junction_shape(c.route, c.arms);
    navicore_osmand::summarize_lanes(m.lanes, m.junction);
    return m;
}

void test_junction_shapes()
{
    using navicore_osmand::cluster_symbol;
    using navicore_osmand::junction_symbol;
    using navicore_osmand::make_junction_shape;
    using navicore_osmand::summarize_lanes;
    using K = ManeuverKind;
    // The description: normalised, duplicates and the route's own arm
    // merged, non-finite skipped, through road flagged, at most 6 kept.
    auto j = make_junction_shape(370.0, {350.0, -5.0, 14.0, std::nan(""), 92.0, 95.0, -180.0});
    assert(j.valid && std::fabs(j.route_angle - 10.0f) < 1e-4f);
    assert(j.branch_count == 3 && j.through_road && !j.truncated);
    assert(std::fabs(j.branches[0] + 10.0f) < 1e-4f && std::fabs(j.branches[1] - 92.0f) < 1e-4f &&
           std::fabs(j.branches[2] - 180.0f) < 1e-4f);
    j = make_junction_shape(0.0, {-150.0, -120.0, -90.0, -60.0, 60.0, 90.0, 120.0});
    assert(j.branch_count == 6 && j.truncated && !j.through_road);
    assert(!make_junction_shape(std::nan(""), {0.0}).valid);
    // Lanes: a recommended block at an edge asks for a move to that side.
    const std::vector<std::pair<const char*, int>> lane_cases = {
        {"RR.", -1}, {".R", 1}, {"..RR", 1}, {".R.", 0}, {"R.R", 0}, {"RRR", 0},
        {"...", 0}, {"", 0}, {"R", 0}};
    for (const auto& lc : lane_cases) {
        const auto m = junction_maneuver({K::Left, -90.0, {}, lc.first, 0});
        assert(m.junction.lane_change == lc.second);
        assert(static_cast<std::size_t>(m.junction.lane_count) == std::string(lc.first).size());
    }
    assert(junction_maneuver({K::Left, -90.0, {}, "RR.", 0}).junction.recommended_lanes == 3u);

    const std::vector<JunctionCase> cases = {
        // Turns at a crossing, a side road and a T where the road ends.
        {K::Left, -90.0, {0.0, 90.0}, "", 1},
        {K::Right, 90.0, {2.0, -88.0}, "", 2},
        {K::Left, -85.0, {3.0}, "", 50},
        {K::Right, 95.0, {-4.0}, "", 49},
        {K::Left, -90.0, {90.0}, "", 60},
        {K::Right, 88.0, {-92.0}, "", 61},
        {K::Left, -100.0, {5.0, 130.0}, "", 1},     // other side diagonal: still a crossing
        {K::Right, 80.0, {0.0, -90.0, -175.0}, "", 2},  // reverse arm ignored
        // Slight turns: exit, lane change, diagonal crossing, splits, fork.
        {K::SlightLeft, -45.0, {0.0}, "", 11},
        {K::SlightRight, 40.0, {-2.0}, "", 12},
        {K::SlightLeft, -45.0, {0.0}, "R..", 58},
        {K::SlightRight, 45.0, {0.0}, "..R", 59},
        {K::SlightLeft, -45.0, {0.0}, ".R.", 11},
        {K::SlightLeft, -45.0, {0.0, 135.0}, "", 33},
        {K::SlightRight, 45.0, {0.0, -135.0}, "", 30},
        {K::SlightLeft, -35.0, {0.0, 35.0}, "", 55},
        {K::SlightRight, 35.0, {0.0, -38.0}, "", 53},
        {K::SlightLeft, -55.0, {0.0, 50.0}, "", 46},
        {K::SlightRight, 50.0, {0.0, -55.0}, "", 45},
        {K::SlightLeft, -40.0, {40.0}, "", 7},
        {K::SlightRight, 40.0, {-40.0}, "", 8},
        // Sharp turns into a diagonal crossing.
        {K::SharpLeft, -135.0, {0.0, 45.0}, "", 31},
        {K::SharpRight, 140.0, {0.0, -45.0}, "", 32},
        // Keep / straight on.
        {K::KeepRight, 5.0, {-40.0}, "", 13},
        {K::KeepLeft, -5.0, {40.0}, "", 14},
        {K::Straight, 0.0, {-90.0, 90.0}, "", 56},
        {K::KeepLeft, -3.0, {-88.0, 91.0}, "", 56},
        {K::Straight, 2.0, {-90.0}, "", 47},
        {K::Straight, -2.0, {88.0}, "", 48},
        {K::KeepLeft, -2.0, {45.0, -135.0}, "", 34},
        {K::KeepRight, 3.0, {-45.0, 135.0}, "", 35},
        {K::KeepLeft, 0.0, {-35.0, 35.0}, "", 54},
        {K::KeepRight, 0.0, {-55.0, 50.0}, "", 44},
        {K::KeepLeft, -40.0, {0.0}, "", 11},
        {K::KeepLeft, -40.0, {0.0}, "RR..", 58},
        {K::KeepRight, 40.0, {0.0}, "...R", 59},
        {K::KeepRight, 40.0, {-35.0, 0.0}, "", 53},
        {K::KeepRight, 40.0, {-40.0}, "", 8},
        // Fallbacks to the turn-type picture (b1044e1).
        {K::Left, -90.0, {}, "", 1},                 // no other road
        {K::Left, -90.0, {90.0, 27.0}, "", 1},       // arm in a gap: ambiguous
        {K::Right, 90.0, {-90.0, 65.0}, "", 2},      // ambiguous
        {K::Left, 80.0, {0.0, -90.0}, "", 1},        // geometry disagrees with the turn type
        {K::SlightLeft, -45.0, {}, "", 3},
        {K::SlightLeft, -45.0, {0.0, -90.0}, "", 3}, // more roads on the route's side
        {K::SlightLeft, -80.0, {0.0}, "", 3},        // not a slight angle
        {K::SlightRight, 45.0, {0.0, 90.0}, "", 4},
        {K::SharpLeft, -135.0, {0.0}, "", 5},
        {K::SharpRight, 135.0, {-90.0, 0.0}, "", 6},
        {K::KeepLeft, -3.0, {-40.0}, "", 7},         // branch on the side OsmAnd keeps to
        {K::KeepRight, 3.0, {40.0}, "", 8},
        {K::KeepLeft, -10.0, {10.0}, "", 7},         // two roads ahead: narrow fork
        {K::KeepRight, 40.0, {}, "", 8},
        {K::Straight, 0.0, {}, "", 9},
        {K::Straight, 0.0, {-90.0, 90.0, 45.0}, "", 9},
        {K::UTurn, -175.0, {0.0, 90.0}, "", 10},
        {K::Roundabout, 92.0, {0.0, -90.0}, "", 21},
        {K::Arrive, 0.0, {-90.0, 90.0}, "", 15},
    };
    std::set<int> produced;
    for (const auto& c : cases) {
        GuidanceState g;
        g.maneuver = junction_maneuver(c);
        const int symbol = cluster_symbol(g);
        if (symbol != c.expected)
            std::fprintf(stderr, "junction case kind=%d route=%.0f: got %d want %d\n",
                         static_cast<int>(c.kind), c.route, symbol, c.expected);
        assert(symbol == c.expected);
        if (junction_symbol(g.maneuver)) produced.insert(symbol);
    }
    // Every new junction picture is produced by some case.
    for (const int symbol : {1, 2, 7, 8, 11, 12, 13, 14, 30, 31, 32, 33, 34, 35, 44, 45, 46, 47,
                             48, 49, 50, 53, 54, 55, 56, 58, 59, 60, 61})
        assert(produced.count(symbol) == 1);
    // A truncated description (more than six other roads) falls back.
    GuidanceState g;
    g.maneuver = junction_maneuver({K::Right, 90.0, {0.0, -90.0}, "", 0});
    g.maneuver.junction.truncated = true;
    assert(cluster_symbol(g) == 2 && junction_symbol(g.maneuver) == 0);
    g.maneuver = junction_maneuver({K::Left, -90.0, {90.0}, "", 0});
    g.maneuver.junction.valid = false;
    assert(cluster_symbol(g) == 1);
    // Text redraw stand-ins for the junction pictures.
    using navicore_osmand::redraw_symbol;
    assert(redraw_symbol(60) == 1 && redraw_symbol(61) == 2 && redraw_symbol(50) == 1 &&
           redraw_symbol(49) == 2 && redraw_symbol(56) == 9 && redraw_symbol(47) == 56 &&
           redraw_symbol(58) == 11 && redraw_symbol(11) == 3 && redraw_symbol(44) == 54 &&
           redraw_symbol(54) == 44 && redraw_symbol(33) == 3 && redraw_symbol(31) == 5);
}

// A road text change with an unchanged symbol is sent once with another
// symbol, then (next publication) with the real one.
void test_cluster_text_redraw()
{
    using navicore_osmand::ClusterInputs;
    using navicore_osmand::apply_text_redraw;
    using navicore_osmand::build_cluster_payload;
    ClusterInputs in;
    in.now = 12 * 3600;
    in.screen = Screen::Guidance;
    in.guidance.position_available = true;
    in.guidance.distance_to_maneuver_metres = 300.0;
    in.guidance.remaining_metres = 2000.0;
    in.guidance.maneuver.kind = ManeuverKind::Right;
    in.guidance.maneuver.road = "Main Street";
    in.guidance.road = "First Avenue";
    const auto last = build_cluster_payload(in);
    // Only the distance changes: no redraw needed.
    in.guidance.distance_to_maneuver_metres = 250.0;
    auto next = build_cluster_payload(in);
    assert(!apply_text_redraw(next, last));
    assert(next.cluster.route_guidance_symbol == 2);
    // The next road changes, same symbol: a neighbouring symbol first.
    in.guidance.maneuver.road = "High Street";
    next = build_cluster_payload(in);
    assert(apply_text_redraw(next, last));
    check_cluster_valid(next);
    assert(next.cluster.route_guidance_symbol == 49 && next.next_road == "High Street");
    // The real payload then differs from the redraw one by the symbol and
    // is published as is.
    auto real = build_cluster_payload(in);
    assert(!real.same_as(next));
    assert(!apply_text_redraw(real, next));
    assert(real.cluster.route_guidance_symbol == 2);
    // The current road appearing counts as a text change too.
    in.guidance.road.clear();
    const auto no_road = build_cluster_payload(in);
    in.guidance.road = "Second Avenue";
    next = build_cluster_payload(in);
    assert(apply_text_redraw(next, no_road));
    // A symbol change redraws by itself.
    in.guidance.maneuver.kind = ManeuverKind::Left;
    in.guidance.maneuver.road = "Low Street";
    next = build_cluster_payload(in);
    assert(!apply_text_redraw(next, real));
    // Outside guidance there is no symbol and nothing to do.
    in.screen = Screen::FreeDrive;
    next = build_cluster_payload(in);
    assert(!apply_text_redraw(next, real));
}

void test_dev_menu()
{
    NavigationModel model;
    NavigationView view(model, 1024, 574);
    settle(view, 31);
    const int night_x = 1024 - 16 - 32, night_y = 16 + 32 + 4 * 72;
    // A short tap on the day/night button still toggles day/night.
    tap(view, night_x, night_y);
    std::vector<MapCommand> taken;
    model.take_map_commands(taken);
    assert((taken == std::vector<MapCommand>{MapCommand::ToggleNight}));
    // Holding it 1.5 s opens the developer menu instead (no toggle).
    view.pointer(OPEN_NAVI_BACKEND_POINTER_PRESS, night_x, night_y);
    settle(view, 100);
    view.pointer(OPEN_NAVI_BACKEND_POINTER_RELEASE, night_x, night_y);
    settle(view, 2);
    assert(model.dev_menu_open());
    model.take_map_commands(taken);
    assert(taken.empty());

    // Rows update in place; a layout change rebuilds. Toggles push actions.
    std::vector<DevSection> sections = {{"GPS", {{"Fix (raw)", "3"}, {"Rate", "1.0 Hz"}}},
                                        {"BUILD", {{"Backend", "abc"}}}};
    std::vector<DevToggle> toggles = {{"Night map", false, ""},
                                      {"Route simulation", true, "applies at next start"}};
    assert(view.set_dev_info(sections, toggles));
    settle(view, 2);
    sections[0].rows[1].second = "0.5 Hz";
    assert(!model.set_dev_info(sections, toggles));  // values only
    assert(view.set_dev_info(sections, toggles));
    settle(view, 2);
    // Toggle rows: right column, from y 16 + 72, 48 px tall, 54 apart.
    const int toggle_x = 16 + (1024 - 32) - 16 - 125;
    tap(view, toggle_x, 16 + 72 + 54 + 24);
    std::size_t action = 99;
    assert(model.take_dev_action(action) && action == 1u);
    assert(!model.take_dev_action(action));
    // Moving closes it; it does not open while moving.
    assert(view.set_moving(true));
    settle(view, 2);
    assert(!model.dev_menu_open());
    assert(!view.open_dev_menu());
    assert(view.set_moving(false));
    assert(view.open_dev_menu());
    settle(view, 2);
    // Close (top right of the panel).
    tap(view, 16 + (1024 - 32) - 16 - 60, 16 + 12 + 24);
    assert(!model.dev_menu_open());
}

// Key centres of the design keyboard (keyboard at x 16, y 268; 8 px
// padding; rows 70 px apart; flex widths after 12 px button padding).
int key_x(int count, float weights, const float* before, int index)
{
    const float unit = (976.0f - 6.0f * (count - 1) - 12.0f * count) / weights;
    float x = 8.0f;
    for (int i = 0; i < index; ++i)
        x += unit * before[i] + 12.0f + 6.0f;
    return 16 + static_cast<int>(x + (unit * before[index] + 12.0f) / 2.0f);
}

void test_keyboard()
{
    NavigationModel model;
    NavigationView view(model, 1024, 574);
    settle(view, 31);
    view.open_search();
    settle(view, 2);
    const int row0 = 268 + 8 + 32, row3 = 268 + 8 + 3 * 70 + 32;
    const float latin_top[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    const float bottom[6] = {1.5f, 1, 1, 4, 1, 1.5f};
    const float ukrainian_top[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    tap(view, key_x(10, 10.0f, latin_top, 2), row0);          // e
    assert(model.query() == "e");
    tap(view, key_x(6, 10.0f, bottom, 1), row3);              // УКР
    tap(view, key_x(12, 12.0f, ukrainian_top, 3), row0);      // к
    assert(model.query() == "e\u043a");
    // Backspace removes the whole two-byte letter.
    const float ukrainian_third[11] = {1.5f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1.5f};
    tap(view, key_x(11, 12.0f, ukrainian_third, 10), 268 + 8 + 2 * 70 + 32);
    assert(model.query() == "e");
}

void test_reroute()
{
    NavigationModel model;
    NavigationView view(model, 1024, 574);
    settle(view, 31);
    RouteAlternative route;
    route.duration = "5 min";
    route.distance_metres = 1100.0;
    route.duration_seconds = 300.0;
    route.geometry = {{50.0, 30.0}, {50.01, 30.0}};
    assert(view.select_place({"poi:1", "Target", "", "", 50.01, 30.0}));
    assert(view.apply_routes(model.route_revision(), {route}));
    assert(view.start_guidance(0));
    // 0.002 deg east of the route is ~143 m off; on the route is 0 m.
    GuidanceTracker tracker;
    assert(tracker.update(route, {50.005, 30.0}).off_route_metres < 1.0);
    const auto off = tracker.update(route, {50.005, 30.002});
    assert(off.off_route_metres > 100.0 && off.off_route_metres < 200.0);
    // The route runs north here.
    assert(off.route_bearing_known &&
           (off.route_bearing_degrees < 1.0 || off.route_bearing_degrees > 359.0));
    // Past the corner of a route that turns east, the direction there is east,
    // also where the start of the route (heading north) is as near.
    RouteAlternative corner = route;
    corner.geometry = {{50.0, 30.0}, {50.01, 30.0}, {50.01, 30.02}};
    corner.distance_metres = 1112.0 + 1430.0;
    GuidanceTracker corner_tracker;
    const auto east = corner_tracker.update(corner, {50.01, 30.01});
    assert(east.route_bearing_known && std::fabs(east.route_bearing_degrees - 90.0) < 1.0);

    // One recalculation at a time; stale and wrong-screen results are ignored.
    const auto revision = model.request_reroute();
    assert(revision != 0 && model.rerouting());
    assert(model.request_reroute() == 0);
    assert(!model.apply_reroute(revision - 1, {route}));
    auto shown = off;
    shown.recalculating = true;
    assert(view.apply_guidance(shown));
    settle(view, 2);
    // An empty result keeps the old route and ends the attempt.
    assert(model.apply_reroute(revision, {}));
    assert(!model.rerouting() && model.routes().size() == 1);
    // A new route replaces the old one in place; guidance continues.
    const auto retry = model.request_reroute();
    RouteAlternative detour = route;
    detour.duration = "6 min";
    detour.geometry = {{50.005, 30.002}, {50.01, 30.0}};
    assert(model.apply_reroute(retry, {detour}));
    assert(model.screen() == Screen::Guidance && model.routes().front().duration == "6 min");
    shown.recalculating = false;
    assert(view.apply_guidance(shown));
    settle(view, 2);
    // Ending guidance drops a pending recalculation.
    assert(model.request_reroute() != 0);
    assert(model.end_guidance() && !model.rerouting());
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
    test_place_card_and_back();
    test_dev_menu();
    test_cluster_payload();
    test_cluster_symbols();
    test_junction_shapes();
    test_cluster_text_redraw();
    test_keyboard();
    test_reroute();

    assert(compact_font_text(u8"Січневого Повстання") ==
           "Sichnevoho Povstannya");
    assert(compact_font_text("E95") == "E95");

    NavigationModel model;
    assert(model.screen() == Screen::FreeDrive);

    NavigationView view(model, 1024, 574);
    frame(view);
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
    view.pointer(OPEN_NAVI_BACKEND_POINTER_PRESS, 80, 40);
    view.pointer(OPEN_NAVI_BACKEND_POINTER_RELEASE, 80, 40);
    frame(view);
    assert(model.screen() == Screen::FreeDrive);
    for (int i = 2; i < 30; ++i)
        frame(view);
    // Exercise the real provider action constants through LVGL's input queue.
    view.pointer(OPEN_NAVI_BACKEND_POINTER_PRESS, 80, 40);
    frame(view);
    frame(view);
    view.pointer(OPEN_NAVI_BACKEND_POINTER_RELEASE, 80, 40);
    frame(view);
    frame(view);
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
    frame(view);
    view.pointer(OPEN_NAVI_BACKEND_POINTER_PRESS, 100, 120);
    frame(view);
    frame(view);
    view.pointer(OPEN_NAVI_BACKEND_POINTER_RELEASE, 100, 120);
    frame(view);
    frame(view);
    assert(model.destination());
    assert(model.destination()->id == "house-12");
    const auto route_revision = model.route_revision();
    const auto stale_route_revision = route_revision - 1;
    assert(!view.apply_routes(stale_route_revision,
        {{"stale", "99 min", "99 km", "", "Must be rejected", 0.0, 0.0,
          {}, {}, {}}}));
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
        {ManeuverKind::Right, "River Road", 716.0, 1, 0, {}, false, 90.0, {}},
        {ManeuverKind::Arrive, "Riverside Ave 12", 1828.0, 2, 0, {}, false, 0.0, {}}};
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
    // The arrival card returns to free drive by itself after 8 s.
    for (int i = 0; i < 40 && model.screen() == Screen::Guidance; ++i) {
        view.process_input(250);
        (void)view.render(0);
    }
    assert(model.screen() == Screen::FreeDrive);

    model.close_search();
    model.open_search();
    const auto empty_revision = model.set_query("not installed");
    assert(model.apply_search_results(empty_revision, {}));
    assert(model.results().empty());
    assert(model.message() == "No offline results");
    return 0;
}
