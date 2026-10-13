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

// Map interactions the UI requests; the engine applies them to the camera.
enum class MapCommand {
    ZoomIn,
    ZoomOut,
    Recenter,
    ToggleOrientation,
    ToggleTilt,
    ToggleNight,
    Overview
};

// What the camera is doing, published by the engine so the UI can show the
// right control state (recenter only while not following, etc.).
struct MapViewState {
    bool following = true;
    bool heading_up = true;
    bool tilted = false;
    bool night = false;

    bool operator==(const MapViewState& other) const noexcept
    {
        return following == other.following &&
               heading_up == other.heading_up && tilted == other.tilted &&
               night == other.night;
    }
    bool operator!=(const MapViewState& other) const noexcept
    {
        return !(*this == other);
    }
};

// A one-finger map drag in absolute screen pixels. The engine keeps the
// map point under the start position under the finger (exact with
// rotation and tilt), so only positions are passed, never deltas.
struct MapDrag {
    bool active = false;   // finger still down
    bool started = false;  // first report of this drag
    int start_x = 0, start_y = 0;
    int x = 0, y = 0;
};

// Renderer-thread UI state only. Slow search and routing work is deliberately
// not called from this model: a coordinator copies requests to a worker and
// applies owned results only when their revision still matches.
class NavigationModel {
public:
    static constexpr std::size_t maximum_recents = 5;

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

    // Map interaction queue (UI -> engine). take_* swap into caller-owned
    // storage so a steady frame loop does not allocate.
    void push_map_command(MapCommand command);
    void take_map_commands(std::vector<MapCommand>& output);
    void add_pan(int dx, int dy) noexcept;
    bool take_pan(int& dx, int& dy) noexcept;
    void begin_drag(int start_x, int start_y, int x, int y) noexcept;
    void move_drag(int x, int y) noexcept;
    void end_drag() noexcept;
    // True when the drag changed since the last call; `started` is reported
    // once per drag.
    bool take_drag(MapDrag& out) noexcept;
    // Taps on bare map (bounded; oldest kept).
    void push_map_tap(int x, int y) noexcept;
    bool take_map_tap(int& x, int& y) noexcept;
    const MapViewState& map_view() const noexcept { return map_view_; }
    bool set_map_view(const MapViewState& state) noexcept;

    // Driver-distraction state: text entry is limited while moving.
    bool moving() const noexcept { return moving_; }
    bool set_moving(bool moving) noexcept;

    // Recent destinations, newest first. Starting guidance records one.
    const std::vector<Destination>& recents() const noexcept { return recents_; }
    std::uint64_t recents_revision() const noexcept { return recents_revision_; }
    void set_recents(std::vector<Destination> recents);
    // Routes to a recent destination through the normal search/route flow.
    std::uint64_t select_recent(std::size_t index);
    // Routes to a place picked on the map (a POI) through the same flow;
    // 0 while guiding.
    std::uint64_t select_place(Destination place);
    // Route preview Back: returns to the search results.
    bool back_to_search() noexcept;

    // Place card over free drive for a tapped POI (design G2): the place
    // with its subtitle ("Fuel · 0.4 km") and the map icon of its category.
    const Destination* place_card() const noexcept;
    const std::string& place_card_icon() const noexcept { return place_icon_; }
    bool show_place_card(Destination place, std::string icon);
    bool close_place_card() noexcept;

private:
    void remember_recent(const Destination& destination);

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
    std::vector<MapCommand> map_commands_;
    int pan_dx_ = 0;
    int pan_dy_ = 0;
    MapDrag drag_;
    bool drag_pending_ = false;
    int taps_[4][2] = {};
    int tap_count_ = 0;
    MapViewState map_view_;
    bool moving_ = false;
    std::vector<Destination> recents_;
    std::uint64_t recents_revision_ = 0;
    Destination place_;
    std::string place_icon_;
    bool has_place_ = false;
};

} // namespace navicore_osmand::ui
