#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace navicore_osmand::ui {

enum class Screen { FreeDrive, Search, RoutePreview, Guidance };

enum class ManeuverKind {
    Straight,
    Left,
    SlightLeft,
    SharpLeft,
    Right,
    SlightRight,
    SharpRight,
    KeepLeft,
    KeepRight,
    UTurn,
    Roundabout,
    Arrive
};

struct Destination {
    std::string id;
    std::string title;
    std::string subtitle;
    std::string distance;
    double latitude = 0.0;
    double longitude = 0.0;
};

struct GeoPoint {
    double latitude = 0.0;
    double longitude = 0.0;
};

struct Maneuver {
    ManeuverKind kind = ManeuverKind::Straight;
    std::string road;
    double distance_from_start_metres = 0.0;
    std::size_t geometry_index = 0;
    int roundabout_exit = 0;
};

struct RouteAlternative {
    std::string id;
    std::string duration;
    std::string distance;
    std::string arrival;
    std::string reason;
    double duration_seconds = 0.0;
    double distance_metres = 0.0;
    std::vector<GeoPoint> geometry;
    std::vector<Maneuver> maneuvers;
};

struct GuidanceState {
    bool position_available = false;
    bool simulated = false;
    bool arrived = false;
    GeoPoint position;
    double progress_metres = 0.0;
    double remaining_metres = 0.0;
    double remaining_seconds = 0.0;
    double distance_to_maneuver_metres = 0.0;
    Maneuver maneuver;
    Maneuver following_maneuver;
    bool has_following_maneuver = false;
};

// Renderer-thread UI state only. Slow search and routing work is deliberately
// not called from this model: a coordinator copies requests to a worker and
// applies owned results only when their revision still matches.
class NavigationModel {
public:
    Screen screen() const noexcept { return screen_; }
    const std::string& query() const noexcept { return query_; }
    const std::vector<Destination>& results() const noexcept { return results_; }
    const Destination* destination() const noexcept;
    const std::vector<RouteAlternative>& routes() const noexcept { return routes_; }
    std::size_t selected_route() const noexcept { return selected_route_; }
    const std::string& message() const noexcept { return message_; }
    const GuidanceState& guidance() const noexcept { return guidance_; }
    std::uint64_t search_revision() const noexcept { return search_revision_; }
    std::uint64_t route_revision() const noexcept { return route_revision_; }

    void open_search();
    void close_search() noexcept;
    std::uint64_t set_query(std::string query);
    bool apply_search_results(std::uint64_t revision,
                              std::vector<Destination> results);
    std::uint64_t select_destination(std::size_t index);
    bool apply_routes(std::uint64_t revision,
                      std::vector<RouteAlternative> routes);
    bool select_route(std::size_t index) noexcept;
    bool start_guidance() noexcept;
    bool update_guidance(GuidanceState guidance) noexcept;
    bool end_guidance() noexcept;

private:
    Screen screen_ = Screen::FreeDrive;
    std::string query_;
    std::vector<Destination> results_;
    Destination destination_;
    bool has_destination_ = false;
    std::vector<RouteAlternative> routes_;
    std::size_t selected_route_ = 0;
    std::string message_;
    GuidanceState guidance_;
    std::uint64_t search_revision_ = 0;
    std::uint64_t route_revision_ = 0;
};

} // namespace navicore_osmand::ui
