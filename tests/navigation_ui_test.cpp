#include "guidance_tracker.h"
#include "ui/compact_text.h"
#include "ui/navigation_model.h"
#include "ui/navigation_view.h"
#include <open_navicore/backend.h>

#include <algorithm>
#include <cassert>
#include <cstdint>

using namespace navicore_osmand::ui;
using navicore_osmand::GuidanceTracker;

int main()
{
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
