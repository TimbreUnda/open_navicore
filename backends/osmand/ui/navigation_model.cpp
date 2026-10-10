#include "navigation_model.h"

#include <utility>

namespace navicore_osmand::ui {

const Destination* NavigationModel::destination() const noexcept
{
    return has_destination_ ? &destination_ : nullptr;
}

void NavigationModel::open_search()
{
    screen_ = Screen::Search;
    message_.clear();
}

void NavigationModel::close_search() noexcept
{
    screen_ = Screen::FreeDrive;
    query_.clear();
    results_.clear();
    routes_.clear();
    has_destination_ = false;
    ++search_revision_;
    ++route_revision_;
    message_.clear();
}

std::uint64_t NavigationModel::set_query(std::string query)
{
    query_ = std::move(query);
    results_.clear();
    routes_.clear();
    has_destination_ = false;
    ++search_revision_;
    ++route_revision_;
    message_ = query_.empty() ? std::string() : "Searching installed maps...";
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
    message_ = "Calculating route...";
    return route_revision_;
}

bool NavigationModel::apply_routes(std::uint64_t revision,
                                   std::vector<RouteAlternative> routes)
{
    if (revision != route_revision_ || !has_destination_ ||
        screen_ != Screen::Search)
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
    return true;
}

} // namespace navicore_osmand::ui
