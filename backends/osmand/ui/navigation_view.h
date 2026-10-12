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
    bool render(uint32_t elapsed_ms);

    const uint32_t* pixels() const noexcept { return pixels_.data(); }
    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }
    bool dirty() const noexcept { return dirty_; }
    DirtyRegion dirty_region() const noexcept { return dirty_region_; }

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

private:
    struct PointerSample { int action; int32_t x; int32_t y; };

    void create_display();
    void destroy_display() noexcept;
    void rebuild();
    void build_free_drive();
    void build_search();
    void build_route_preview();
    void build_guidance();
    void refresh_guidance();
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

    NavigationModel& model_;
    Screen built_screen_ = Screen::FreeDrive;
    uint32_t width_;
    uint32_t height_;
    std::vector<uint32_t> pixels_;
    std::vector<uint32_t> draw_buffer_;
    lv_display_t* display_ = nullptr;
    lv_indev_t* indev_ = nullptr;
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* results_panel_ = nullptr;
    lv_obj_t* query_field_ = nullptr;
    lv_obj_t* guidance_symbol_ = nullptr;
    lv_obj_t* guidance_simulation_ = nullptr;
    lv_obj_t* guidance_distance_ = nullptr;
    lv_obj_t* guidance_action_ = nullptr;
    lv_obj_t* guidance_road_ = nullptr;
    lv_obj_t* guidance_following_ = nullptr;
    lv_obj_t* guidance_lanes_ = nullptr;
    lv_obj_t* guidance_summary_ = nullptr;
    lv_obj_t* guidance_instruction_ = nullptr;
    std::deque<PointerSample> pointer_queue_;
    PointerSample last_pointer_{2, 0, 0};
    uint32_t rendered_frames_ = 0;
    bool input_sequence_active_ = false;
    bool pointer_down_ = false;
    bool dirty_ = false;
    bool full_clear_pending_ = true;
    DirtyRegion dirty_region_{};
};

} // namespace navicore_osmand::ui
