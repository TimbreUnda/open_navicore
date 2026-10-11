#include "ui/navigation_model.h"
#include "ui/navigation_view.h"
#include <open_navicore/backend.h>

#include <cassert>

using namespace navicore_osmand::ui;

int main()
{
    NavigationModel model;
    assert(model.screen() == Screen::FreeDrive);

    NavigationView view(model, 1024, 574);
    assert(view.render(16));
    // Startup Screen events are quarantined until the first stable frames.
    view.pointer(MOCK_NAV_BACKEND_POINTER_PRESS, 80, 40);
    view.pointer(MOCK_NAV_BACKEND_POINTER_RELEASE, 80, 40);
    assert(view.render(16));
    assert(model.screen() == Screen::FreeDrive);
    for (int frame = 2; frame < 30; ++frame)
        assert(view.render(16));
    // Exercise the real provider action constants through LVGL's input queue.
    view.pointer(MOCK_NAV_BACKEND_POINTER_PRESS, 80, 40);
    assert(view.render(16));
    assert(view.render(16));
    view.pointer(MOCK_NAV_BACKEND_POINTER_RELEASE, 80, 40);
    assert(view.render(16));
    assert(view.render(16));
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
    assert(view.render(16));
    view.pointer(MOCK_NAV_BACKEND_POINTER_PRESS, 100, 120);
    assert(view.render(16));
    assert(view.render(16));
    view.pointer(MOCK_NAV_BACKEND_POINTER_RELEASE, 100, 120);
    assert(view.render(16));
    assert(view.render(16));
    assert(model.destination());
    assert(model.destination()->id == "house-12");
    const auto route_revision = model.route_revision();
    const auto stale_route_revision = route_revision - 1;
    assert(!view.apply_routes(stale_route_revision,
        {{"stale", "99 min", "99 km", "", "Must be rejected", 0.0, 0.0, {}}}));
    assert(view.apply_routes(route_revision,
        {{"fast", "14 min", "10.6 km", "08:15", "Fastest", 0.0, 0.0, {}},
         {"quiet", "17 min", "11.2 km", "08:18", "Fewer major roads", 0.0, 0.0, {}}}));
    assert(model.screen() == Screen::RoutePreview);
    assert(model.routes().size() == 2);
    assert(model.select_route(1));
    assert(model.start_guidance());
    assert(model.screen() == Screen::Guidance);
    assert(view.render(16));

    model.close_search();
    model.open_search();
    const auto empty_revision = model.set_query("not installed");
    assert(model.apply_search_results(empty_revision, {}));
    assert(model.results().empty());
    assert(model.message() == "No offline results");
    return 0;
}
