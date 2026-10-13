#pragma once

#include "navigation_model.h"
#include <lvgl.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace navicore_osmand::ui {

// LVGL's software renderer owns this ARGB8888 canvas. The borrowed EGL target
// is deliberately absent from this class; GlOverlayCompositor is the sole GL
// boundary and is called only by OsmAndEngine::frame on the renderer thread.
class NavigationView {
public:
    struct DirtyRegion {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    NavigationView(NavigationModel& model, uint32_t width, uint32_t height);
    ~NavigationView();
    NavigationView(const NavigationView&) = delete;
    NavigationView& operator=(const NavigationView&) = delete;

    void resize(uint32_t width, uint32_t height);
    void pointer(int action, int32_t x, int32_t y);
    // Advances LVGL time by the real frame interval and reads every queued
    // pointer sample, so map drags reach the camera in the same frame.
    // Call once per frame before the camera update.
    void process_input(uint32_t elapsed_ms);
    bool render(uint32_t elapsed_ms);

    const uint32_t* pixels() const noexcept { return pixels_.data(); }
    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }
    bool dirty() const noexcept { return dirty_; }
    DirtyRegion dirty_region() const noexcept { return dirty_region_; }
    // Separate dirty areas of the last render (at most 8, merged when they
    // touch). Far-apart updates, e.g. the map marker and the guidance rail,
    // stay separate so their bounding box is not re-uploaded every frame.
    const std::vector<DirtyRegion>& dirty_regions() const noexcept
    {
        return dirty_regions_;
    }

    // Deterministic entry points used by host tests; production input reaches
    // the same model transitions through LVGL callbacks.
    void open_search();
    std::uint64_t set_query(std::string query);
    bool apply_search_results(std::uint64_t revision,
                              std::vector<Destination> results);
    std::uint64_t select_result(std::size_t index);
    bool apply_routes(std::uint64_t revision,
                      std::vector<RouteAlternative> routes);
    bool apply_guidance(GuidanceState guidance);
    bool start_guidance(std::size_t route);
    std::uint64_t select_recent(std::size_t index);
    // Routes to a place picked on the map (e.g. a POI).
    std::uint64_t select_place(Destination place);

    // Part of the window the camera frames: right of the 336 px rail. Free
    // drive and guidance float their cards over a full-width map, so the
    // map (and the vehicle marker) is visible beyond this area there.
    struct MapArea { int x = 0; int y = 0; int width = 0; int height = 0; };
    MapArea map_area() const noexcept;
    // Re-reads model.map_view() into the map control buttons.
    void apply_map_view();
    // Driver-distraction state; rebuilds the search screen when it changes.
    bool set_moving(bool moving);
    // Vehicle marker at a window position; rotation is clockwise degrees
    // relative to screen-up. Small changes are ignored to limit redraw.
    void set_vehicle_marker(bool visible, int x, int y, float rotation_degrees);

private:
    struct PointerSample { int action; int32_t x; int32_t y; };

    void create_display();
    void destroy_display() noexcept;
    void rebuild();
    void build_free_drive();
    void build_search();
    void build_route_preview();
    void build_guidance();
    void build_map_controls(bool full);
    void build_vehicle_marker();
    void build_recents(_lv_obj_t* parent);
    void build_recent_card(int y);
    void refresh_guidance();
    // Places the guidance card contents for the current text (simulation tag,
    // one- or two-line action, countdown bar) without rebuilding the screen.
    void layout_guidance_card();
    int rail_width() const noexcept;
    void handle_map_gesture(const PointerSample& sample);
    void clear_object_references() noexcept;
    void refresh_results();
    _lv_obj_t* label(_lv_obj_t* parent, const char* text, uint32_t color,
                     int font_size = 14);
    _lv_obj_t* button(_lv_obj_t* parent, const char* text);

    static void flush_cb(lv_display_t* display, const lv_area_t* area,
                         unsigned char* pixels);
    static void pointer_cb(lv_indev_t* indev, lv_indev_data_t* data);
    static void open_search_cb(lv_event_t* event);
    static void close_search_cb(lv_event_t* event);
    static void query_changed_cb(lv_event_t* event);
    static void result_cb(lv_event_t* event);
    static void route_cb(lv_event_t* event);
    static void start_cb(lv_event_t* event);
    static void end_cb(lv_event_t* event);
    static void map_command_cb(lv_event_t* event);
    static void recent_cb(lv_event_t* event);

    NavigationModel& model_;
    Screen built_screen_ = Screen::FreeDrive;
    uint32_t width_;
    uint32_t height_;
    std::vector<uint32_t> pixels_;
    lv_display_t* display_ = nullptr;
    lv_indev_t* indev_ = nullptr;
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* results_panel_ = nullptr;
    lv_obj_t* query_field_ = nullptr;
    lv_obj_t* guidance_card_ = nullptr;
    lv_obj_t* guidance_glyph_ = nullptr;
    lv_obj_t* guidance_exit_ = nullptr;
    lv_obj_t* guidance_simulation_ = nullptr;
    lv_obj_t* guidance_distance_ = nullptr;
    lv_obj_t* guidance_action_ = nullptr;
    lv_obj_t* guidance_road_ = nullptr;
    lv_obj_t* guidance_bar_ = nullptr;
    lv_obj_t* guidance_bar_fill_ = nullptr;
    lv_obj_t* guidance_time_ = nullptr;
    lv_obj_t* guidance_remaining_ = nullptr;
    lv_obj_t* guidance_next_glyph_ = nullptr;
    lv_obj_t* guidance_instruction_ = nullptr;
    lv_obj_t* control_recenter_ = nullptr;
    lv_obj_t* control_heading_ = nullptr;
    lv_obj_t* control_north_ = nullptr;
    lv_obj_t* control_tilt_ = nullptr;
    lv_obj_t* control_night_ = nullptr;
    lv_obj_t* vehicle_marker_ = nullptr;
    bool marker_visible_ = false;
    int marker_x_ = 0;
    int marker_y_ = 0;
    float marker_rotation_ = 0.0f;
    bool map_drag_ = false;      // pressed on bare map
    bool map_dragging_ = false;  // moved past the slop: a drag, not a tap
    int32_t drag_last_x_ = 0;
    int32_t drag_last_y_ = 0;
    int32_t press_x_ = 0;
    int32_t press_y_ = 0;
    uint32_t press_tick_ = 0;
    std::deque<PointerSample> pointer_queue_;
    PointerSample last_pointer_{2, 0, 0};
    uint32_t rendered_frames_ = 0;
    bool input_sequence_active_ = false;
    bool pointer_down_ = false;
    bool dirty_ = false;
    bool full_clear_pending_ = true;
    DirtyRegion dirty_region_{};
    std::vector<DirtyRegion> dirty_regions_;
    void add_dirty(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
};

} // namespace navicore_osmand::ui
