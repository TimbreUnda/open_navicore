#include "navigation_model.h"

#include <algorithm>
#include <utility>

namespace navicore_osmand::ui {

const Destination* NavigationModel::destination() const noexcept
{
    return has_destination_ ? &destination_ : nullptr;
}

void NavigationModel::open_search()
{
    cancel_direct_route();
    screen_ = Screen::Search;
    message_.clear();
    has_place_ = false;
}

void NavigationModel::close_search() noexcept
{
    screen_ = Screen::FreeDrive;
    query_.clear();
    results_.clear();
    routes_.clear();
    has_destination_ = false;
    direct_route_ = false;
    ++search_revision_;
    ++route_revision_;
    message_.clear();
    guidance_ = {};
}

std::uint64_t NavigationModel::set_query(std::string query)
{
    query_ = std::move(query);
    results_.clear();
    routes_.clear();
    has_destination_ = false;
    ++search_revision_;
    ++route_revision_;
    message_ = query_.empty() ? std::string() : "Searching installed maps…";
    return search_revision_;
}

bool NavigationModel::apply_search_results(std::uint64_t revision,
                                           std::vector<Destination> results)
{
    if (revision != search_revision_ || screen_ != Screen::Search ||
        query_.empty())
        return false;
    results_ = std::move(results);
    message_ = results_.empty() ? "No offline results" : std::string();
    return true;
}

std::uint64_t NavigationModel::select_destination(std::size_t index)
{
    if (screen_ != Screen::Search || index >= results_.size())
        return 0;
    destination_ = results_[index];
    has_destination_ = true;
    routes_.clear();
    selected_route_ = 0;
    ++route_revision_;
    message_ = calculating_message;
    return route_revision_;
}

bool NavigationModel::apply_routes(std::uint64_t revision,
                                   std::vector<RouteAlternative> routes)
{
    if (revision != route_revision_ || !has_destination_ ||
        (screen_ != Screen::Search && !direct_routing()))
        return false;
    routes_ = std::move(routes);
    if (routes_.empty()) {
        message_ = "No route available in installed maps";
        return true;
    }
    message_.clear();
    screen_ = Screen::RoutePreview;
    return true;
}

bool NavigationModel::select_route(std::size_t index) noexcept
{
    if (screen_ != Screen::RoutePreview || index >= routes_.size())
        return false;
    selected_route_ = index;
    return true;
}

bool NavigationModel::start_guidance() noexcept
{
    if (screen_ != Screen::RoutePreview || routes_.empty() ||
        selected_route_ >= routes_.size())
        return false;
    screen_ = Screen::Guidance;
    guidance_ = {};
    if (has_destination_)
        remember_recent(destination_);
    return true;
}

bool NavigationModel::update_guidance(GuidanceState guidance) noexcept
{
    if (screen_ != Screen::Guidance)
        return false;
    guidance_ = std::move(guidance);
    return true;
}

bool NavigationModel::end_guidance() noexcept
{
    if (screen_ != Screen::Guidance)
        return false;
    rerouting_ = false;
    direct_route_ = false;
    has_place_ = false;
    screen_ = Screen::FreeDrive;
    query_.clear();
    results_.clear();
    routes_.clear();
    has_destination_ = false;
    selected_route_ = 0;
    guidance_ = {};
    message_.clear();
    ++search_revision_;
    ++route_revision_;
    return true;
}

void NavigationModel::push_map_command(MapCommand command)
{
    // Bounded: a stuck engine must not let taps grow memory.
    if (map_commands_.size() < 32u)
        map_commands_.push_back(command);
}

void NavigationModel::take_map_commands(std::vector<MapCommand>& output)
{
    output.clear();
    output.swap(map_commands_);
}

void NavigationModel::add_pan(int dx, int dy) noexcept
{
    pan_dx_ += dx;
    pan_dy_ += dy;
}

bool NavigationModel::take_pan(int& dx, int& dy) noexcept
{
    dx = pan_dx_;
    dy = pan_dy_;
    pan_dx_ = pan_dy_ = 0;
    return dx != 0 || dy != 0;
}

void NavigationModel::begin_drag(int start_x, int start_y, int x, int y) noexcept
{
    drag_ = {true, true, start_x, start_y, x, y};
    drag_pending_ = true;
}

void NavigationModel::move_drag(int x, int y) noexcept
{
    if (!drag_.active)
        return;
    drag_.x = x;
    drag_.y = y;
    drag_pending_ = true;
}

void NavigationModel::end_drag() noexcept
{
    if (!drag_.active)
        return;
    drag_.active = false;
    drag_pending_ = true;
}

bool NavigationModel::take_drag(MapDrag& out) noexcept
{
    if (!drag_pending_)
        return false;
    out = drag_;
    drag_.started = false;
    drag_pending_ = false;
    return true;
}

void NavigationModel::begin_pinch(int x0, int y0, int x1, int y1) noexcept
{
    pinch_ = {true, true, x0, y0, x1, y1};
    pinch_pending_ = true;
}

void NavigationModel::move_pinch(int x0, int y0, int x1, int y1) noexcept
{
    if (!pinch_.active)
        return;
    pinch_.x0 = x0;
    pinch_.y0 = y0;
    pinch_.x1 = x1;
    pinch_.y1 = y1;
    pinch_pending_ = true;
}

void NavigationModel::end_pinch() noexcept
{
    if (!pinch_.active)
        return;
    pinch_.active = false;
    pinch_pending_ = true;
}

bool NavigationModel::take_pinch(MapPinch& out) noexcept
{
    if (!pinch_pending_)
        return false;
    out = pinch_;
    pinch_.started = false;
    pinch_pending_ = false;
    return true;
}

void NavigationModel::push_map_touch(MapGesture gesture, int x, int y) noexcept
{
    if (touch_count_ >= 4)
        return;
    touches_[touch_count_] = {gesture, x, y};
    ++touch_count_;
}

bool NavigationModel::take_map_touch(MapTouch& out) noexcept
{
    if (touch_count_ == 0)
        return false;
    out = touches_[0];
    for (int i = 1; i < touch_count_; ++i)
        touches_[i - 1] = touches_[i];
    --touch_count_;
    return true;
}

bool NavigationModel::set_map_view(const MapViewState& state) noexcept
{
    if (state == map_view_)
        return false;
    map_view_ = state;
    return true;
}

bool NavigationModel::set_moving(bool moving) noexcept
{
    if (moving == moving_)
        return false;
    moving_ = moving;
    return true;
}

void NavigationModel::set_recents(std::vector<Destination> recents)
{
    if (recents.size() > maximum_recents)
        recents.resize(maximum_recents);
    recents_ = std::move(recents);
    ++recents_revision_;
}

void NavigationModel::remember_recent(const Destination& destination)
{
    const auto same = [&](const Destination& item) {
        return (!destination.id.empty() && item.id == destination.id) ||
               (item.title == destination.title &&
                item.latitude == destination.latitude &&
                item.longitude == destination.longitude);
    };
    recents_.erase(std::remove_if(recents_.begin(), recents_.end(), same),
                   recents_.end());
    recents_.insert(recents_.begin(), destination);
    if (recents_.size() > maximum_recents)
        recents_.resize(maximum_recents);
    ++recents_revision_;
}

std::uint64_t NavigationModel::select_recent(std::size_t index)
{
    if (index >= recents_.size() || screen_ == Screen::Guidance)
        return 0;
    return select_place(recents_[index]);
}

std::uint64_t NavigationModel::select_place(Destination place)
{
    if (screen_ == Screen::Search) {
        query_ = place.title;
        ++search_revision_;
        results_ = {std::move(place)};
        return select_destination(0);
    }
    if (screen_ != Screen::FreeDrive)
        return 0;
    // The card shows the place while its route is calculated; a recent
    // destination gets one too, so the state and a failure have a place.
    if (!has_place_ || place_.id != place.id || place_.title != place.title) {
        place_icon_.clear();
        has_place_ = true;
    }
    place_ = place;
    destination_ = std::move(place);
    has_destination_ = true;
    direct_route_ = true;
    routes_.clear();
    selected_route_ = 0;
    ++route_revision_;
    message_ = calculating_message;
    return route_revision_;
}

void NavigationModel::cancel_direct_route() noexcept
{
    if (!direct_route_)
        return;
    direct_route_ = false;
    if (screen_ == Screen::FreeDrive) {
        has_destination_ = false;
        routes_.clear();
        ++route_revision_;
        message_.clear();
    }
}


std::uint64_t NavigationModel::request_reroute() noexcept
{
    if (screen_ != Screen::Guidance || !has_destination_ || rerouting_)
        return 0;
    rerouting_ = true;
    return ++route_revision_;
}

bool NavigationModel::apply_reroute(std::uint64_t revision,
                                    std::vector<RouteAlternative> routes)
{
    if (screen_ != Screen::Guidance || !rerouting_ || revision != route_revision_)
        return false;
    rerouting_ = false;
    if (!routes.empty()) {
        routes_ = std::move(routes);
        selected_route_ = 0;
    }
    return true;
}

bool NavigationModel::back_to_search() noexcept
{
    if (screen_ != Screen::RoutePreview)
        return false;
    // A route picked in free drive returns there, with its place card.
    screen_ = direct_route_ ? Screen::FreeDrive : Screen::Search;
    direct_route_ = false;
    routes_.clear();
    has_destination_ = false;
    selected_route_ = 0;
    ++route_revision_;
    message_.clear();
    return true;
}

const Destination* NavigationModel::place_card() const noexcept
{
    return has_place_ ? &place_ : nullptr;
}

bool NavigationModel::show_place_card(Destination place, std::string icon)
{
    if (screen_ != Screen::FreeDrive)
        return false;
    cancel_direct_route();
    place_ = std::move(place);
    place_icon_ = std::move(icon);
    has_place_ = true;
    return true;
}

bool NavigationModel::set_place_subtitle(const std::string& id, std::string subtitle)
{
    if (!has_place_ || place_.id != id || place_.subtitle == subtitle)
        return false;
    place_.subtitle = std::move(subtitle);
    if (has_destination_ && destination_.id == id)
        destination_.subtitle = place_.subtitle;
    return true;
}

bool NavigationModel::close_place_card() noexcept
{
    if (!has_place_)
        return false;
    cancel_direct_route();
    has_place_ = false;
    return true;
}

} // namespace navicore_osmand::ui
