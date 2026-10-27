#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
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

// One lane at a manoeuvre: its main direction, an optional second one
// (e.g. straight or right), and whether the route uses it.
struct Lane {
    ManeuverKind kind = ManeuverKind::Straight;
    ManeuverKind also = ManeuverKind::Straight;
    bool combined = false;  // `also` is valid
    bool recommended = false;
};

// The junction at a manoeuvre, for the cluster picture: angles in degrees
// relative to the incoming direction of travel, positive clockwise (right),
// in (-180, 180]; 0 is straight on. From OsmAnd's attached roads at the
// junction node (navigation_worker.cpp, junction_shape.h).
struct JunctionShape {
    static constexpr std::size_t kMaxBranches = 6;
    bool valid = false;              // geometry known (not for roundabouts)
    float route_angle = 0.0f;        // the road the route takes
    // The other roads leaving the node (not the incoming road, not the
    // route), in no particular order.
    uint8_t branch_count = 0;
    float branches[kMaxBranches] = {};
    bool truncated = false;          // more than kMaxBranches other roads
    bool through_road = false;       // a branch within 25 degrees of straight
    // Lanes (from Maneuver::lanes): count, recommended ones (bit 0 = the
    // leftmost lane) and -1/+1 when the recommended lanes are a block at
    // the left/right edge with others beside them (a move toward that
    // side), else 0.
    uint8_t lane_count = 0;
    uint16_t recommended_lanes = 0;
    int8_t lane_change = 0;
};

struct Maneuver {
    ManeuverKind kind = ManeuverKind::Straight;
    std::string road;
    double distance_from_start_metres = 0.0;
    std::size_t geometry_index = 0;
    int roundabout_exit = 0;
    std::vector<Lane> lanes;  // left to right; empty when unknown
    // OsmAnd TurnType::isLeftSide(): a clockwise (left-hand traffic)
    // roundabout, or a right U-turn (TRU).
    bool left_side = false;
    // OsmAnd TurnType::getTurnAngle(): degrees, positive clockwise (right);
    // for a roundabout the exit relative to the entry direction.
    double turn_angle_degrees = 0.0;
    JunctionShape junction;
};

// A stretch of the route on one road: from start_metres until the next.
struct RoadStretch {
    double start_metres = 0.0;
    std::string name;
    int speed_limit_kmh = 0;  // 0 when unknown
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
    std::vector<RoadStretch> roads;
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
    // Distance from the position to the nearest point of the route, and
    // whether a new route from here is being calculated.
    double off_route_metres = 0.0;
    bool recalculating = false;
    // The route's direction (degrees clockwise from north) where the car has
    // got to along it; with the driving direction it tells a wrong way.
    bool route_bearing_known = false;
    double route_bearing_degrees = 0.0;
    // The road at the current position and its speed limit (0: unknown).
    std::string road;
    int speed_limit_kmh = 0;
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
    bool night = true;
    // A heading is known, so heading-up can turn the map; until then the
    // heading-up button is shown dimmed (the map stays as it is).
    bool heading_known = true;

    bool operator==(const MapViewState& other) const noexcept
    {
        return following == other.following &&
               heading_up == other.heading_up && tilted == other.tilted &&
               night == other.night && heading_known == other.heading_known;
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

// A two-finger map gesture in absolute screen pixels: the fingers' distance
// sets the zoom, their angle the rotation, and the map point under their
// midpoint stays under it (pinch-zoom and two-finger rotate together).
struct MapPinch {
    bool active = false;   // both fingers still down
    bool started = false;  // first report of this gesture
    int x0 = 0, y0 = 0;    // first finger
    int x1 = 0, y1 = 0;    // second finger
};

// Developer menu (bench diagnostics): read-only sections of key/value rows
// filled by the engine, and toggles whose taps the engine applies.
struct DevSection {
    std::string title;
    std::vector<std::pair<std::string, std::string>> rows;
};
struct DevToggle {
    std::string label;
    bool on = false;
    std::string note;  // e.g. "next start"; empty for an action button
};

// Bare-map touches the engine acts on (taps pick POIs and places, a double
// tap zooms in there, a long press drops a pin).
enum class MapGesture { Tap, DoubleTap, LongPress };
struct MapTouch {
    MapGesture gesture = MapGesture::Tap;
    int x = 0, y = 0;
};

// Renderer-thread UI state only. Slow search and routing work is deliberately
// not called from this model: a coordinator copies requests to a worker and
// applies owned results only when their revision still matches.
class NavigationModel {
public:
    static constexpr std::size_t maximum_recents = 5;
    static constexpr const char* calculating_message = "Calculating route…";

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
    void begin_pinch(int x0, int y0, int x1, int y1) noexcept;
    void move_pinch(int x0, int y0, int x1, int y1) noexcept;
    void end_pinch() noexcept;
    // True when the pinch changed since the last call (like take_drag).
    bool take_pinch(MapPinch& out) noexcept;
    // Touches on bare map (bounded; oldest kept).
    void push_map_touch(MapGesture gesture, int x, int y) noexcept;
    bool take_map_touch(MapTouch& out) noexcept;
    const MapViewState& map_view() const noexcept { return map_view_; }
    bool set_map_view(const MapViewState& state) noexcept;

    // Developer menu over free drive; never opens while moving.
    bool dev_menu_open() const noexcept { return dev_open_ && screen_ == Screen::FreeDrive; }
    bool open_dev_menu() noexcept;
    bool close_dev_menu() noexcept;
    const std::vector<DevSection>& dev_sections() const noexcept { return dev_sections_; }
    const std::vector<DevToggle>& dev_toggles() const noexcept { return dev_toggles_; }
    // True when the row/toggle layout changed (not only values).
    bool set_dev_info(std::vector<DevSection> sections, std::vector<DevToggle> toggles);
    void push_dev_action(std::size_t toggle) noexcept;
    bool take_dev_action(std::size_t& toggle) noexcept;

    // Driver-distraction state: text entry is limited while moving.
    bool moving() const noexcept { return moving_; }
    bool set_moving(bool moving) noexcept;

    // Recent destinations, newest first. Starting guidance records one.
    const std::vector<Destination>& recents() const noexcept { return recents_; }
    std::uint64_t recents_revision() const noexcept { return recents_revision_; }
    void set_recents(std::vector<Destination> recents);
    // Routes to a recent destination: from Search through the search flow,
    // from free drive directly (see select_place).
    std::uint64_t select_recent(std::size_t index);
    // Routes to a place picked on the map (a POI) or a recent destination.
    // From free drive the route is calculated directly: the screen stays
    // free drive, the place card shows the state, and the route opens Route
    // preview. From Search it becomes a one-result search. 0 while guiding.
    std::uint64_t select_place(Destination place);
    // A direct route from free drive is being calculated or has failed
    // (message() says which).
    bool direct_routing() const noexcept { return direct_route_ && screen_ == Screen::FreeDrive; }
    bool calculating_route() const noexcept
    {
        return has_destination_ && routes_.empty() && message_ == calculating_message;
    }
    // The query still needs a search. Not once a destination is picked from
    // it: a recent sets the query to its title, and a late search for that
    // would cancel the destination's route on the one worker.
    bool search_wanted() const noexcept
    {
        return screen_ == Screen::Search && !query_.empty() && !has_destination_;
    }
    // Route preview Back: returns to where the route was picked (the search
    // results, or free drive with the place card).
    bool back_to_search() noexcept;

    // Off-route recalculation during guidance: request_reroute() opens a
    // new route revision for the same destination (0 if not guiding or one
    // is pending); apply_reroute() replaces the route in place. An empty
    // result ends the attempt and keeps the old route.
    std::uint64_t request_reroute() noexcept;
    bool apply_reroute(std::uint64_t revision, std::vector<RouteAlternative> routes);
    bool rerouting() const noexcept { return rerouting_; }

    // Place card over free drive for a tapped POI (design G2): the place
    // with its subtitle ("Fuel · 0.4 km") and the map icon of its category.
    const Destination* place_card() const noexcept;
    const std::string& place_card_icon() const noexcept { return place_icon_; }
    bool show_place_card(Destination place, std::string icon);
    bool close_place_card() noexcept;
    // A late subtitle (a dropped pin's address) for the card showing `id`.
    bool set_place_subtitle(const std::string& id, std::string subtitle);
    // Dropped pins: their card's place id starts with this.
    static constexpr const char* pin_prefix = "pin:";
    bool place_card_is_pin() const noexcept
    {
        return has_place_ && place_.id.rfind(pin_prefix, 0) == 0;
    }

private:
    void remember_recent(const Destination& destination);
    void cancel_direct_route() noexcept;

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
    MapPinch pinch_;
    bool pinch_pending_ = false;
    MapTouch touches_[4];
    int touch_count_ = 0;
    MapViewState map_view_;
    bool moving_ = false;
    std::vector<Destination> recents_;
    std::uint64_t recents_revision_ = 0;
    Destination place_;
    std::string place_icon_;
    bool has_place_ = false;
    bool rerouting_ = false;
    bool direct_route_ = false;  // the route was picked from free drive
    bool dev_open_ = false;
    std::vector<DevSection> dev_sections_;
    std::vector<DevToggle> dev_toggles_;
    std::size_t dev_actions_[4] = {};
    int dev_action_count_ = 0;
};

} // namespace navicore_osmand::ui
