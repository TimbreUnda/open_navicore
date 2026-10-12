#include "navigation_view.h"

#include "../diagnostics.h"

#include <lvgl.h>
#include <open_navicore/backend.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace navicore_osmand::ui {
namespace {
constexpr uint32_t base = 0x0d0f12;
constexpr uint32_t surface = 0x16191e;
constexpr uint32_t control = 0x1f232a;
constexpr uint32_t line = 0x2a2f37;
constexpr uint32_t primary = 0xeef0f3;
constexpr uint32_t secondary = 0xa4abb5;
constexpr uint32_t tertiary = 0x6f7782;
constexpr uint32_t accent = 0x3d7bff;
constexpr uint32_t maneuver = 0x1f4fd1;
constexpr int stock_chrome_top = 72;
constexpr int stock_chrome_bottom = 122;
constexpr uint32_t input_quarantine_frames = 30;
// Car touch targets: at least 64 px with 8 px spacing, 16 px from edges.
constexpr int map_button = 64;
constexpr int map_gap = 8;
constexpr int map_margin = 16;
constexpr int marker_size = 52;
// Marker updates below these thresholds are not redrawn.
constexpr int marker_move_threshold = 1;
constexpr float marker_rotation_threshold = 2.0f;

lv_color_t color(uint32_t value) { return lv_color_hex(value); }

void no_scroll(lv_obj_t* object)
{
    lv_obj_set_scrollable(object, false);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

void panel(lv_obj_t* object, uint32_t background, int radius = 0)
{
    lv_obj_set_style_bg_color(object, color(background), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    no_scroll(object);
}

int content_top(uint32_t height)
{
    return height >= 700u ? stock_chrome_top : 0;
}

int content_height(uint32_t height)
{
    const int top = content_top(height);
    const int bottom = height >= 700u ? stock_chrome_bottom : 0;
    return std::max(1, static_cast<int>(height) - top - bottom);
}

std::string distance_text(double metres)
{
    char text[32] = {};
    metres = std::max(0.0, metres);
    if (metres < 100.0) {
        const double rounded = std::round(metres / 5.0) * 5.0;
        std::snprintf(text, sizeof(text), "%.0f m", rounded);
    } else if (metres < 1000.0) {
        const double rounded = std::round(metres / 10.0) * 10.0;
        std::snprintf(text, sizeof(text), "%.0f m", rounded);
    } else {
        std::snprintf(text, sizeof(text), "%.1f km", metres / 1000.0);
    }
    return text;
}

std::string duration_text(double seconds)
{
    const int minutes = std::max(0, static_cast<int>(std::ceil(seconds / 60.0)));
    char text[32] = {};
    if (minutes < 60)
        std::snprintf(text, sizeof(text), "%d min", minutes);
    else
        std::snprintf(text, sizeof(text), "%d h %02d min", minutes / 60,
                      minutes % 60);
    return text;
}

const char* maneuver_action(ManeuverKind kind)
{
    switch (kind) {
    case ManeuverKind::Left: return "Turn left";
    case ManeuverKind::SlightLeft: return "Slight left";
    case ManeuverKind::SharpLeft: return "Sharp left";
    case ManeuverKind::Right: return "Turn right";
    case ManeuverKind::SlightRight: return "Slight right";
    case ManeuverKind::SharpRight: return "Sharp right";
    case ManeuverKind::KeepLeft: return "Keep left";
    case ManeuverKind::KeepRight: return "Keep right";
    case ManeuverKind::UTurn: return "Make a U-turn";
    case ManeuverKind::Roundabout: return "Enter roundabout";
    case ManeuverKind::Arrive: return "Arriving at";
    case ManeuverKind::Straight: return "Continue";
    }
    return "Continue";
}

const char* maneuver_symbol(ManeuverKind kind)
{
    switch (kind) {
    case ManeuverKind::Left:
    case ManeuverKind::SlightLeft:
    case ManeuverKind::SharpLeft:
    case ManeuverKind::KeepLeft: return "<";
    case ManeuverKind::Right:
    case ManeuverKind::SlightRight:
    case ManeuverKind::SharpRight:
    case ManeuverKind::KeepRight: return ">";
    case ManeuverKind::UTurn: return "U";
    case ManeuverKind::Roundabout: return "O";
    case ManeuverKind::Arrive: return "X";
    case ManeuverKind::Straight: return "^";
    }
    return "^";
}

void set_label_text(lv_obj_t* label, const char* text)
{
    if (!label || !text)
        return;
    const char* current = lv_label_get_text(label);
    if (!current || std::strcmp(current, text) != 0)
        lv_label_set_text(label, text);
}

void set_hidden(lv_obj_t* object, bool hidden)
{
    if (!object)
        return;
    if (lv_obj_is_hidden(object) != hidden)
        lv_obj_set_hidden(object, hidden);
}

} // namespace

NavigationView::NavigationView(NavigationModel& model, uint32_t width,
                               uint32_t height)
    : model_(model), width_(width), height_(height)
{
    if (width == 0 || height == 0)
        throw std::invalid_argument("LVGL display dimensions must be nonzero");
    if (!lv_is_initialized())
        lv_init();
    create_display();
}

NavigationView::~NavigationView() { destroy_display(); }

void NavigationView::create_display()
{
    pixels_.assign(static_cast<std::size_t>(width_) * height_, 0u);
    display_ = lv_display_create(static_cast<int32_t>(width_),
                                 static_cast<int32_t>(height_));
    if (!display_)
        throw std::runtime_error("lv_display_create failed");
    lv_display_set_user_data(display_, this);
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_ARGB8888);
    // LVGL draws invalidated areas straight into the retained canvas, which
    // the compositor uploads; no intermediate draw buffer or flush copy.
    lv_display_set_buffers(display_, pixels_.data(), nullptr,
                           pixels_.size() * sizeof(uint32_t),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display_, flush_cb);
    lv_display_set_default(display_);

    indev_ = lv_indev_create();
    lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev_, display_);
    lv_indev_set_user_data(indev_, this);
    lv_indev_set_read_cb(indev_, pointer_cb);
    rebuild();
}

void NavigationView::destroy_display() noexcept
{
    if (indev_)
        lv_indev_delete(indev_);
    indev_ = nullptr;
    if (display_)
        lv_display_delete(display_);
    display_ = nullptr;
    screen_ = nullptr;
    clear_object_references();
}

void NavigationView::resize(uint32_t width, uint32_t height)
{
    if (width == width_ && height == height_)
        return;
    if (width == 0 || height == 0)
        throw std::invalid_argument("LVGL display dimensions must be nonzero");
    destroy_display();
    width_ = width;
    height_ = height;
    create_display();
}

lv_obj_t* NavigationView::label(lv_obj_t* parent, const char* text,
                                uint32_t text_color, int font_size)
{
    lv_obj_t* object = lv_label_create(parent);
    lv_label_set_text(object, text);
    lv_obj_set_style_text_color(object, color(text_color), 0);
    const lv_font_t* font = &lv_font_montserrat_14;
    if (font_size >= 28) font = &lv_font_montserrat_28;
    else if (font_size >= 24) font = &lv_font_montserrat_24;
    else if (font_size >= 20) font = &lv_font_montserrat_20;
    lv_obj_set_style_text_font(object, font, 0);
    return object;
}

lv_obj_t* NavigationView::button(lv_obj_t* parent, const char* text)
{
    lv_obj_t* object = lv_button_create(parent);
    panel(object, control, 8);
    lv_obj_set_height(object, 64);
    lv_obj_t* caption = label(object, text, primary, 20);
    lv_obj_center(caption);
    return object;
}

void NavigationView::rebuild()
{
    lv_display_set_default(display_);
    lv_obj_t* old_screen = screen_;
    clear_object_references();
    screen_ = lv_obj_create(nullptr);
    panel(screen_, base);
    if (model_.screen() != Screen::Search)
        lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, 0);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    // Builders query map_area(), which depends on the screen being built.
    built_screen_ = model_.screen();
    switch (built_screen_) {
    case Screen::FreeDrive: build_free_drive(); break;
    case Screen::Search: build_search(); break;
    case Screen::RoutePreview: build_route_preview(); break;
    case Screen::Guidance: build_guidance(); break;
    }
    lv_screen_load(screen_);
    if (old_screen)
        lv_obj_delete_async(old_screen);
    full_clear_pending_ = true;
    lv_obj_invalidate(screen_);
}

void NavigationView::build_free_drive()
{
    const int rail_width = std::min<int>(336, width_);
    lv_obj_t* rail = lv_obj_create(screen_);
    panel(rail, base);
    lv_obj_set_pos(rail, 0, content_top(height_));
    lv_obj_set_size(rail, rail_width, content_height(height_));
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(rail, 16, 0);
    lv_obj_set_style_pad_row(rail, 12, 0);

    lv_obj_t* search = button(rail, "Where to?");
    lv_obj_set_width(search, LV_PCT(100));
    lv_obj_add_event_cb(search, open_search_cb, LV_EVENT_CLICKED, this);

    lv_obj_t* shortcuts = lv_obj_create(rail);
    panel(shortcuts, base);
    lv_obj_set_width(shortcuts, LV_PCT(100));
    lv_obj_set_height(shortcuts, 80);
    lv_obj_set_flex_flow(shortcuts, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(shortcuts, 8, 0);
    for (const char* title : {"Home", "Work"}) {
        lv_obj_t* shortcut = button(shortcuts, title);
        lv_obj_set_flex_grow(shortcut, 1);
        lv_obj_set_height(shortcut, 80);
    }
    label(rail, "RECENT", tertiary, 14);
    build_recents(rail);
    lv_obj_t* spacer = lv_obj_create(rail);
    panel(spacer, base);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_width(spacer, 1);
    label(rail, "Offline maps", tertiary, 14);
    build_map_controls(true);
    build_vehicle_marker();
}

void NavigationView::build_recents(lv_obj_t* parent)
{
    const auto& recents = model_.recents();
    if (recents.empty()) {
        label(parent, "No recent destinations", secondary, 20);
        return;
    }
    for (std::size_t index = 0; index < recents.size(); ++index) {
        lv_obj_t* row = lv_button_create(parent);
        panel(row, surface, 8);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, map_button);
        lv_obj_set_style_pad_left(row, 16, 0);
        lv_obj_t* title = label(row, recents[index].title.c_str(), primary, 18);
        lv_obj_set_width(title, LV_PCT(100));
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_event_cb(row, recent_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(index + 1));
    }
}

int NavigationView::rail_width() const noexcept
{
    return width_ > 600u ? std::min<int>(336, static_cast<int>(width_)) : 0;
}

NavigationView::MapArea NavigationView::map_area() const noexcept
{
    MapArea area;
    area.y = content_top(height_);
    area.height = content_height(height_);
    if (built_screen_ == Screen::Search) {
        area.x = 0;
        area.width = static_cast<int>(width_);
        return area;
    }
    area.x = rail_width();
    area.width = std::max(1, static_cast<int>(width_) - area.x);
    return area;
}

void NavigationView::build_map_controls(bool full)
{
    control_recenter_ = control_orientation_ = nullptr;
    control_tilt_ = control_night_ = nullptr;
    const MapArea area = map_area();
    if (area.width < map_button + 2 * map_margin)
        return;
    const int x = area.x + area.width - map_margin - map_button;
    int y = area.y + map_margin;
    const auto add = [&](const char* text, MapCommand command, int font) {
        lv_obj_t* object = lv_button_create(screen_);
        panel(object, surface, 12);
        lv_obj_set_style_bg_opa(object, LV_OPA_90, 0);
        lv_obj_set_size(object, map_button, map_button);
        lv_obj_set_pos(object, x, y);
        lv_obj_t* caption = label(object, text, primary, font);
        lv_obj_center(caption);
        lv_obj_add_event_cb(object, map_command_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(object, reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(command) + 1u));
        y += map_button + map_gap;
        return caption;
    };
    (void)add(LV_SYMBOL_PLUS, MapCommand::ZoomIn, 24);
    (void)add(LV_SYMBOL_MINUS, MapCommand::ZoomOut, 24);
    if (full) {
        control_orientation_ = add("N", MapCommand::ToggleOrientation, 20);
        control_tilt_ = add("2D", MapCommand::ToggleTilt, 20);
        control_night_ = add("Day", MapCommand::ToggleNight, 14);
        // Recenter sits apart at the bottom so it is found without looking
        // at the zoom column; it only shows while the camera is free.
        lv_obj_t* recenter = lv_button_create(screen_);
        panel(recenter, accent, 32);
        lv_obj_set_size(recenter, map_button, map_button);
        lv_obj_set_pos(recenter, x,
                       area.y + area.height - map_margin - map_button);
        lv_obj_t* caption = label(recenter, LV_SYMBOL_GPS, primary, 24);
        lv_obj_center(caption);
        lv_obj_add_event_cb(recenter, map_command_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(recenter, reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(MapCommand::Recenter) + 1u));
        control_recenter_ = recenter;
    }
    apply_map_view();
}

void NavigationView::apply_map_view()
{
    const auto& view = model_.map_view();
    set_hidden(control_recenter_, view.following);
    // Shows the current mode: an arrow for heading-up, N for north-up.
    set_label_text(control_orientation_, view.heading_up ? LV_SYMBOL_UP : "N");
    set_label_text(control_tilt_, view.tilted ? "3D" : "2D");
    set_label_text(control_night_, view.night ? "Night" : "Day");
}

void NavigationView::build_vehicle_marker()
{
    marker_visible_ = false;
    lv_obj_t* halo = lv_obj_create(screen_);
    panel(halo, accent, marker_size / 2);
    lv_obj_set_style_bg_opa(halo, LV_OPA_30, 0);
    lv_obj_set_size(halo, marker_size, marker_size);
    lv_obj_set_style_transform_pivot_x(halo, marker_size / 2, 0);
    lv_obj_set_style_transform_pivot_y(halo, marker_size / 2, 0);
    lv_obj_set_clickable(halo, false);
    lv_obj_t* dot = lv_obj_create(halo);
    panel(dot, accent, 13);
    lv_obj_set_size(dot, 26, 26);
    lv_obj_set_style_border_width(dot, 4, 0);
    lv_obj_set_style_border_color(dot, color(primary), 0);
    lv_obj_set_clickable(dot, false);
    lv_obj_center(dot);
    // Heading pointer at the top edge; the whole halo rotates.
    lv_obj_t* pointer = label(halo, LV_SYMBOL_UP, primary, 14);
    lv_obj_set_clickable(pointer, false);
    lv_obj_align(pointer, LV_ALIGN_TOP_MID, 0, -2);
    lv_obj_set_hidden(halo, true);
    vehicle_marker_ = halo;
}

void NavigationView::set_vehicle_marker(bool visible, int x, int y,
                                        float rotation_degrees)
{
    if (!vehicle_marker_)
        return;
    const MapArea area = map_area();
    visible = visible && x >= area.x && x < area.x + area.width &&
              y >= area.y && y < area.y + area.height;
    if (!visible) {
        if (marker_visible_)
            set_hidden(vehicle_marker_, true);
        marker_visible_ = false;
        return;
    }
    rotation_degrees = std::fmod(rotation_degrees, 360.0f);
    if (rotation_degrees < 0.0f) rotation_degrees += 360.0f;
    float rotation_delta = std::fabs(rotation_degrees - marker_rotation_);
    rotation_delta = std::min(rotation_delta, 360.0f - rotation_delta);
    if (marker_visible_ &&
        std::abs(x - marker_x_) < marker_move_threshold &&
        std::abs(y - marker_y_) < marker_move_threshold &&
        rotation_delta < marker_rotation_threshold)
        return;
    lv_obj_set_pos(vehicle_marker_, x - marker_size / 2, y - marker_size / 2);
    if (rotation_delta >= marker_rotation_threshold || !marker_visible_) {
        lv_obj_set_style_transform_rotation(
            vehicle_marker_, static_cast<int32_t>(rotation_degrees * 10.0f), 0);
        marker_rotation_ = rotation_degrees;
    }
    set_hidden(vehicle_marker_, false);
    marker_visible_ = true;
    marker_x_ = x;
    marker_y_ = y;
}

bool NavigationView::set_moving(bool moving)
{
    if (!model_.set_moving(moving))
        return false;
    if (built_screen_ == Screen::Search)
        rebuild();
    return true;
}

std::uint64_t NavigationView::select_recent(std::size_t index)
{
    const std::uint64_t revision = model_.select_recent(index);
    if (revision)
        rebuild();
    return revision;
}

void NavigationView::build_search()
{
    const int top = content_top(height_);
    const int available_height = content_height(height_);
    lv_obj_t* header = lv_obj_create(screen_);
    panel(header, base);
    lv_obj_set_pos(header, 0, top);
    lv_obj_set_size(header, LV_PCT(100), 80);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(header, 12, 0);
    lv_obj_set_style_pad_column(header, 12, 0);

    lv_obj_t* back = button(header, "<");
    lv_obj_set_size(back, 64, 56);
    lv_obj_add_event_cb(back, close_search_cb, LV_EVENT_CLICKED, this);
    query_field_ = lv_textarea_create(header);
    lv_obj_set_flex_grow(query_field_, 1);
    lv_obj_set_height(query_field_, 56);
    lv_textarea_set_one_line(query_field_, true);
    lv_textarea_set_placeholder_text(query_field_, "Street, house or place");
    lv_textarea_set_text(query_field_, model_.query().c_str());
    lv_obj_set_style_bg_color(query_field_, color(surface), 0);
    lv_obj_set_style_border_color(query_field_, color(accent), 0);
    lv_obj_set_style_border_width(query_field_, 2, 0);
    lv_obj_set_style_text_color(query_field_, color(primary), 0);
    lv_obj_set_style_text_font(query_field_, &lv_font_montserrat_20, 0);
    lv_obj_add_event_cb(query_field_, query_changed_cb, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t* body = lv_obj_create(screen_);
    panel(body, base);
    lv_obj_set_pos(body, 0, top + 80);
    lv_obj_set_size(body, width_, std::max(1, available_height - 80));
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    results_panel_ = lv_obj_create(body);
    panel(results_panel_, base);
    lv_obj_set_size(results_panel_, std::min<int>(420, width_), LV_PCT(100));
    lv_obj_set_flex_flow(results_panel_, LV_FLEX_FLOW_COLUMN);

    if (model_.moving()) {
        // Typing while driving is limited: keep the field read-only and offer
        // one-tap recent destinations in place of the keyboard.
        lv_obj_set_disabled(query_field_, true);
        lv_obj_t* locked = lv_obj_create(body);
        panel(locked, base);
        lv_obj_set_flex_grow(locked, 1);
        lv_obj_set_height(locked, LV_PCT(100));
        lv_obj_set_flex_flow(locked, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(locked, 20, 0);
        lv_obj_set_style_pad_row(locked, 10, 0);
        label(locked, "Typing is paused while driving", 0xf6c86a, 20);
        label(locked, "RECENT", tertiary, 14);
        build_recents(locked);
    } else if (width_ > 600) {
        lv_obj_t* keyboard = lv_keyboard_create(body);
        lv_obj_set_flex_grow(keyboard, 1);
        lv_obj_set_height(keyboard, LV_PCT(100));
        lv_keyboard_set_textarea(keyboard, query_field_);
        lv_obj_set_style_bg_color(keyboard, color(base), 0);
        lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_20,
                                   LV_PART_ITEMS);
    }
    refresh_results();
}

void NavigationView::refresh_results()
{
    if (!results_panel_)
        return;
    lv_obj_clean(results_panel_);
    const auto& results = model_.results();
    for (std::size_t index = 0; index < results.size(); ++index) {
        const auto& item = results[index];
        lv_obj_t* row = lv_button_create(results_panel_);
        panel(row, index == 0 ? 0x16233f : base);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 82);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, color(line), 0);
        lv_obj_set_style_pad_left(row, 20, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        label(row, item.title.c_str(), primary, 20);
        std::string detail = item.subtitle;
        if (!item.distance.empty()) detail += "  " + item.distance;
        label(row, detail.c_str(), secondary, 14);
        lv_obj_add_event_cb(row, result_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(index + 1));
    }
    if (results.empty()) {
        const char* text = model_.query().empty() ?
            "Type to search installed maps" : model_.message().c_str();
        lv_obj_t* empty = label(results_panel_, text, tertiary, 14);
        lv_obj_set_style_pad_all(empty, 20, 0);
    }
}

void NavigationView::build_route_preview()
{
    const int rail_width = std::min<int>(336, width_);
    lv_obj_t* rail = lv_obj_create(screen_);
    panel(rail, base);
    lv_obj_set_pos(rail, 0, content_top(height_));
    lv_obj_set_size(rail, rail_width, content_height(height_));
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(rail, 16, 0);
    lv_obj_set_style_pad_row(rail, 10, 0);
    const Destination* destination = model_.destination();
    label(rail, destination ? destination->title.c_str() : "Destination",
          primary, 20);
    if (destination) label(rail, destination->subtitle.c_str(), secondary, 14);

    const auto& routes = model_.routes();
    for (std::size_t index = 0; index < routes.size(); ++index) {
        const auto& route = routes[index];
        lv_obj_t* row = lv_button_create(rail);
        panel(row, index == model_.selected_route() ? 0x16233f : surface, 8);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 86);
        lv_obj_set_style_border_width(row,
            index == model_.selected_route() ? 2 : 1, 0);
        lv_obj_set_style_border_color(row,
            color(index == model_.selected_route() ? accent : line), 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        std::string summary = route.duration + "  " + route.distance;
        label(row, summary.c_str(), primary, 20);
        label(row, route.reason.c_str(), secondary, 14);
        lv_obj_add_event_cb(row, route_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(index + 1));
    }
    lv_obj_t* spacer = lv_obj_create(rail);
    panel(spacer, base);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_t* start = button(rail, "Start");
    lv_obj_set_width(start, LV_PCT(100));
    lv_obj_set_style_bg_color(start, color(maneuver), 0);
    lv_obj_add_event_cb(start, start_cb, LV_EVENT_CLICKED, this);
    build_map_controls(false);
}

void NavigationView::build_guidance()
{
    const int rail_width = std::min<int>(336, width_);
    const auto& guidance = model_.guidance();
    const Destination* destination = model_.destination();
    lv_obj_t* rail = lv_obj_create(screen_);
    panel(rail, base);
    lv_obj_set_pos(rail, 0, content_top(height_));
    lv_obj_set_size(rail, rail_width, content_height(height_));
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);

    if (!guidance.position_available) {
        lv_obj_t* warning = lv_obj_create(rail);
        panel(warning, 0x2a2414);
        lv_obj_set_width(warning, LV_PCT(100));
        lv_obj_set_height(warning, 132);
        lv_obj_set_style_pad_all(warning, 20, 0);
        lv_obj_set_style_pad_row(warning, 8, 0);
        lv_obj_set_flex_flow(warning, LV_FLEX_FLOW_COLUMN);
        label(warning, "No GPS fix", 0xf6c86a, 24);
        lv_obj_t* explanation = label(
            warning, "Guidance is paused until a position is available.",
            0xe6dcc4, 14);
        lv_obj_set_width(explanation, LV_PCT(100));
        lv_label_set_long_mode(explanation, LV_LABEL_LONG_WRAP);

        lv_obj_t* next = lv_obj_create(rail);
        panel(next, base);
        lv_obj_set_width(next, LV_PCT(100));
        lv_obj_set_height(next, 86);
        lv_obj_set_style_pad_all(next, 18, 0);
        lv_obj_set_style_pad_row(next, 5, 0);
        lv_obj_set_flex_flow(next, LV_FLEX_FLOW_COLUMN);
        label(next, "NEXT ON ROUTE", tertiary, 14);
        std::string instruction = maneuver_action(guidance.maneuver.kind);
        if (!guidance.maneuver.road.empty())
            instruction += "  " + guidance.maneuver.road;
        guidance_instruction_ = label(next, instruction.c_str(), secondary, 18);
    } else {
        lv_obj_t* primary_card = lv_obj_create(rail);
        panel(primary_card, maneuver);
        lv_obj_set_width(primary_card, LV_PCT(100));
        lv_obj_set_height(primary_card, 142);
        lv_obj_set_style_pad_all(primary_card, 18, 0);
        lv_obj_set_style_pad_column(primary_card, 14, 0);
        lv_obj_set_flex_flow(primary_card, LV_FLEX_FLOW_ROW);
        guidance_symbol_ = label(primary_card,
            maneuver_symbol(guidance.maneuver.kind), primary, 28);
        lv_obj_set_width(guidance_symbol_, 48);
        lv_obj_set_style_text_align(guidance_symbol_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t* copy = lv_obj_create(primary_card);
        panel(copy, maneuver);
        lv_obj_set_flex_grow(copy, 1);
        lv_obj_set_height(copy, LV_PCT(100));
        lv_obj_set_flex_flow(copy, LV_FLEX_FLOW_COLUMN);
        guidance_simulation_ = label(copy, "ROUTE SIMULATION", 0xffd166, 14);
        if (!guidance.simulated)
            lv_obj_set_hidden(guidance_simulation_, true);
        const std::string next_distance = guidance.arrived ? "0 m" :
            distance_text(guidance.distance_to_maneuver_metres);
        guidance_distance_ = label(copy, next_distance.c_str(), primary, 28);
        guidance_action_ = label(
            copy, maneuver_action(guidance.maneuver.kind), primary, 20);
        const char* road = !guidance.maneuver.road.empty()
            ? guidance.maneuver.road.c_str()
            : (destination ? destination->title.c_str() : "Route");
        guidance_road_ = label(copy, road, 0xc9d6ff, 14);
        lv_obj_set_width(guidance_road_, LV_PCT(100));
        lv_label_set_long_mode(guidance_road_, LV_LABEL_LONG_DOT);

        lv_obj_t* following = lv_obj_create(rail);
        panel(following, 0x183ea8);
        lv_obj_set_width(following, LV_PCT(100));
        lv_obj_set_height(following, 48);
        lv_obj_set_style_pad_left(following, 20, 0);
        lv_obj_set_flex_flow(following, LV_FLEX_FLOW_ROW);
        if (guidance.has_following_maneuver) {
            std::string then = "Then  ";
            then += maneuver_action(guidance.following_maneuver.kind);
            if (!guidance.following_maneuver.road.empty())
                then += "  " + guidance.following_maneuver.road;
            guidance_following_ = label(
                following, then.c_str(), 0xc9d6ff, 14);
        } else {
            guidance_following_ = label(
                following, "Continue to destination", 0xc9d6ff, 14);
        }
        lv_obj_t* lanes = lv_obj_create(rail);
        panel(lanes, base);
        lv_obj_set_width(lanes, LV_PCT(100));
        lv_obj_set_height(lanes, 44);
        lv_obj_set_style_pad_left(lanes, 20, 0);
        guidance_lanes_ = label(lanes, guidance.simulated ?
            "SIMULATION - Lane info unavailable" :
            "Lane info not available here", tertiary, 14);
    }

    lv_obj_t* spacer = lv_obj_create(rail);
    panel(spacer, base);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_width(spacer, 1);

    lv_obj_t* summary = lv_obj_create(rail);
    panel(summary, base);
    lv_obj_set_width(summary, LV_PCT(100));
    lv_obj_set_height(summary, 58);
    lv_obj_set_style_pad_left(summary, 20, 0);
    lv_obj_set_style_border_width(summary, 1, 0);
    lv_obj_set_style_border_side(summary, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(summary, color(line), 0);
    if (guidance.position_available) {
        const std::string trip = duration_text(guidance.remaining_seconds) +
            "  " + distance_text(guidance.remaining_metres);
        guidance_summary_ = label(summary, trip.c_str(), primary, 20);
    } else {
        guidance_summary_ = label(summary, "--:--  ETA paused", tertiary, 20);
    }

    lv_obj_t* controls = lv_obj_create(rail);
    panel(controls, base);
    lv_obj_set_width(controls, LV_PCT(100));
    lv_obj_set_height(controls, 76);
    lv_obj_set_style_pad_all(controls, 8, 0);
    lv_obj_set_style_pad_column(controls, 8, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_t* info = button(controls, "Overview");
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_height(info, 60);
    lv_obj_add_event_cb(info, map_command_cb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(info, reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(MapCommand::Overview) + 1u));
    lv_obj_t* end = button(controls, "End");
    lv_obj_set_flex_grow(end, 1);
    lv_obj_set_height(end, 60);
    lv_obj_set_style_bg_color(end, color(0x3a1614), 0);
    lv_obj_set_style_text_color(end, color(0xff8a80), 0);
    lv_obj_add_event_cb(end, end_cb, LV_EVENT_CLICKED, this);

    build_map_controls(true);
    build_vehicle_marker();
}

void NavigationView::refresh_guidance()
{
    const auto& guidance = model_.guidance();
    const Destination* destination = model_.destination();
    if (!guidance.position_available) {
        if (guidance_instruction_) {
            std::string instruction = maneuver_action(guidance.maneuver.kind);
            if (!guidance.maneuver.road.empty())
                instruction += "  " + guidance.maneuver.road;
            set_label_text(guidance_instruction_, instruction.c_str());
        }
        set_label_text(guidance_summary_, "--:--  ETA paused");
        return;
    }

    set_label_text(guidance_symbol_, maneuver_symbol(guidance.maneuver.kind));
    set_hidden(guidance_simulation_, !guidance.simulated);
    const std::string next_distance = guidance.arrived ? "0 m" :
        distance_text(guidance.distance_to_maneuver_metres);
    set_label_text(guidance_distance_, next_distance.c_str());
    set_label_text(guidance_action_, maneuver_action(guidance.maneuver.kind));
    if (guidance_road_) {
        const char* road = !guidance.maneuver.road.empty()
            ? guidance.maneuver.road.c_str()
            : (destination ? destination->title.c_str() : "Route");
        set_label_text(guidance_road_, road);
    }
    if (guidance_following_) {
        std::string following = "Continue to destination";
        if (guidance.has_following_maneuver) {
            following = "Then  ";
            following += maneuver_action(guidance.following_maneuver.kind);
            if (!guidance.following_maneuver.road.empty())
                following += "  " + guidance.following_maneuver.road;
        }
        set_label_text(guidance_following_, following.c_str());
    }
    set_label_text(guidance_lanes_, guidance.simulated ?
        "SIMULATION - Lane info unavailable" :
        "Lane info not available here");
    if (guidance_summary_) {
        const std::string trip = duration_text(guidance.remaining_seconds) +
            "  " + distance_text(guidance.remaining_metres);
        set_label_text(guidance_summary_, trip.c_str());
    }
}

void NavigationView::clear_object_references() noexcept
{
    results_panel_ = nullptr;
    query_field_ = nullptr;
    guidance_symbol_ = nullptr;
    guidance_simulation_ = nullptr;
    guidance_distance_ = nullptr;
    guidance_action_ = nullptr;
    guidance_road_ = nullptr;
    guidance_following_ = nullptr;
    guidance_lanes_ = nullptr;
    guidance_summary_ = nullptr;
    guidance_instruction_ = nullptr;
    control_recenter_ = nullptr;
    control_orientation_ = nullptr;
    control_tilt_ = nullptr;
    control_night_ = nullptr;
    vehicle_marker_ = nullptr;
    marker_visible_ = false;
    map_drag_ = false;
}

void NavigationView::pointer(int action, int32_t x, int32_t y)
{
    if (rendered_frames_ < input_quarantine_frames) {
        trace_pointer("quarantined", static_cast<uint32_t>(action), x, y);
        return;
    }
    if (action == MOCK_NAV_BACKEND_POINTER_PRESS) {
        input_sequence_active_ = true;
    } else if (!input_sequence_active_) {
        trace_pointer("orphaned", static_cast<uint32_t>(action), x, y);
        return;
    }
    if (pointer_queue_.size() == 32)
        pointer_queue_.pop_front();
    pointer_queue_.push_back({action, x, y});
    trace_pointer("queued", static_cast<uint32_t>(action), x, y);
    if (action == MOCK_NAV_BACKEND_POINTER_RELEASE)
        input_sequence_active_ = false;
}

bool NavigationView::render(uint32_t elapsed_ms)
{
    dirty_ = false;
    dirty_region_ = {};
    dirty_regions_.clear();
    lv_tick_inc(elapsed_ms);
    const bool forced_present = full_clear_pending_;
    if (full_clear_pending_) {
        std::fill(pixels_.begin(), pixels_.end(), 0u);
        dirty_region_ = {0u, 0u, width_, height_};
        dirty_regions_.assign(1, dirty_region_);
        dirty_ = true;
        lv_obj_invalidate(screen_);
        full_clear_pending_ = false;
    }
    (void)lv_timer_handler();
    lv_refr_now(display_);
    if (rendered_frames_ != UINT32_MAX)
        ++rendered_frames_;
    return dirty_ || forced_present;
}

void NavigationView::open_search()
{
    trace("ui=open_search");
    model_.open_search();
    rebuild();
}
std::uint64_t NavigationView::set_query(std::string query)
{
    if (model_.screen() != Screen::Search) model_.open_search();
    const std::uint64_t revision = model_.set_query(std::move(query));
    rebuild();
    return revision;
}
bool NavigationView::apply_search_results(std::uint64_t revision,
                                          std::vector<Destination> results)
{
    trace_result("ui=search_results", revision, results.size());
    const bool accepted = model_.apply_search_results(revision,
                                                      std::move(results));
    if (accepted)
        refresh_results();
    return accepted;
}
std::uint64_t NavigationView::select_result(std::size_t index)
{
    const std::uint64_t revision = model_.select_destination(index);
    rebuild();
    return revision;
}
bool NavigationView::apply_routes(std::uint64_t revision,
                                  std::vector<RouteAlternative> routes)
{
    const bool accepted = model_.apply_routes(revision, std::move(routes));
    rebuild();
    return accepted;
}

bool NavigationView::apply_guidance(GuidanceState guidance)
{
    const bool structure_changed = built_screen_ != model_.screen() ||
        model_.guidance().position_available != guidance.position_available;
    const bool accepted = model_.update_guidance(std::move(guidance));
    if (accepted) {
        if (structure_changed)
            rebuild();
        else
            refresh_guidance();
    }
    return accepted;
}

void NavigationView::flush_cb(lv_display_t* display, const lv_area_t* area,
                              unsigned char* source_bytes)
{
    auto* self = static_cast<NavigationView*>(lv_display_get_user_data(display));
    // Direct mode: pixels are already in the canvas; only track the area.
    if (area && source_bytes && area->x1 >= 0 && area->y1 >= 0 &&
        area->x2 >= area->x1 && area->y2 >= area->y1 &&
        static_cast<uint32_t>(area->x2) < self->width_ &&
        static_cast<uint32_t>(area->y2) < self->height_) {
        const uint32_t x = static_cast<uint32_t>(area->x1);
        const uint32_t y = static_cast<uint32_t>(area->y1);
        const uint32_t width = static_cast<uint32_t>(area->x2 - area->x1 + 1);
        const uint32_t height = static_cast<uint32_t>(area->y2 - area->y1 + 1);
        self->add_dirty(x, y, width, height);
        if (!self->dirty_) {
            self->dirty_region_ = {x, y, width, height};
        } else {
            const uint32_t x2 = std::max(
                self->dirty_region_.x + self->dirty_region_.width, x + width);
            const uint32_t y2 = std::max(
                self->dirty_region_.y + self->dirty_region_.height, y + height);
            self->dirty_region_.x = std::min(self->dirty_region_.x, x);
            self->dirty_region_.y = std::min(self->dirty_region_.y, y);
            self->dirty_region_.width = x2 - self->dirty_region_.x;
            self->dirty_region_.height = y2 - self->dirty_region_.y;
        }
        self->dirty_ = true;
    }
    lv_display_flush_ready(display);
}

void NavigationView::add_dirty(uint32_t x, uint32_t y, uint32_t width,
                               uint32_t height)
{
    constexpr uint32_t touch = 16u;   // merge areas closer than this
    constexpr std::size_t maximum = 8u;
    DirtyRegion area{x, y, width, height};
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t i = 0; i < dirty_regions_.size(); ++i) {
            const DirtyRegion& other = dirty_regions_[i];
            if (area.x > other.x + other.width + touch ||
                other.x > area.x + area.width + touch ||
                area.y > other.y + other.height + touch ||
                other.y > area.y + area.height + touch)
                continue;
            const uint32_t x2 = std::max(area.x + area.width,
                                         other.x + other.width);
            const uint32_t y2 = std::max(area.y + area.height,
                                         other.y + other.height);
            area.x = std::min(area.x, other.x);
            area.y = std::min(area.y, other.y);
            area.width = x2 - area.x;
            area.height = y2 - area.y;
            dirty_regions_.erase(dirty_regions_.begin() +
                                 static_cast<std::ptrdiff_t>(i));
            merged = true;
            break;
        }
    }
    dirty_regions_.push_back(area);
    if (dirty_regions_.size() > maximum) {
        DirtyRegion box = dirty_regions_.front();
        for (const auto& item : dirty_regions_) {
            const uint32_t x2 = std::max(box.x + box.width, item.x + item.width);
            const uint32_t y2 = std::max(box.y + box.height, item.y + item.height);
            box.x = std::min(box.x, item.x);
            box.y = std::min(box.y, item.y);
            box.width = x2 - box.x;
            box.height = y2 - box.y;
        }
        dirty_regions_.assign(1, box);
    }
}

void NavigationView::pointer_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    auto* self = static_cast<NavigationView*>(lv_indev_get_user_data(indev));
    if (!self->pointer_queue_.empty()) {
        self->last_pointer_ = self->pointer_queue_.front();
        self->pointer_queue_.pop_front();
        self->handle_map_gesture(self->last_pointer_);
    }
    if (self->last_pointer_.action == MOCK_NAV_BACKEND_POINTER_PRESS)
        self->pointer_down_ = true;
    if (self->last_pointer_.action == MOCK_NAV_BACKEND_POINTER_RELEASE)
        self->pointer_down_ = false;
    data->point.x = self->last_pointer_.x;
    data->point.y = self->last_pointer_.y;
    data->state = self->pointer_down_ ? LV_INDEV_STATE_PRESSED
                                      : LV_INDEV_STATE_RELEASED;
    data->continue_reading = !self->pointer_queue_.empty();
}

void NavigationView::handle_map_gesture(const PointerSample& sample)
{
    if (sample.action == MOCK_NAV_BACKEND_POINTER_PRESS) {
        map_drag_ = false;
        if (built_screen_ == Screen::Search || !screen_)
            return;
        lv_point_t point{sample.x, sample.y};
        lv_obj_t* hit = lv_indev_search_obj(screen_, &point);
        // Only presses on bare map (the transparent screen or the
        // non-clickable vehicle marker) pan the map.
        if (hit == screen_ || hit == nullptr || hit == vehicle_marker_) {
            map_drag_ = true;
            drag_last_x_ = sample.x;
            drag_last_y_ = sample.y;
        }
        return;
    }
    if (!map_drag_)
        return;
    model_.add_pan(sample.x - drag_last_x_, sample.y - drag_last_y_);
    drag_last_x_ = sample.x;
    drag_last_y_ = sample.y;
    if (sample.action == MOCK_NAV_BACKEND_POINTER_RELEASE)
        map_drag_ = false;
}

void NavigationView::map_command_cb(lv_event_t* event)
{
    auto* self = static_cast<NavigationView*>(lv_event_get_user_data(event));
    const auto value = reinterpret_cast<std::uintptr_t>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(
            lv_event_get_current_target(event))));
    if (value)
        self->model_.push_map_command(static_cast<MapCommand>(value - 1u));
}

void NavigationView::recent_cb(lv_event_t* event)
{
    auto* self = static_cast<NavigationView*>(lv_event_get_user_data(event));
    const auto value = reinterpret_cast<std::size_t>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(
            lv_event_get_current_target(event))));
    if (value)
        (void)self->select_recent(value - 1);
}

void NavigationView::open_search_cb(lv_event_t* event)
{
    trace("ui=open_search_click");
    static_cast<NavigationView*>(lv_event_get_user_data(event))->open_search();
}
void NavigationView::close_search_cb(lv_event_t* event)
{
    auto* self = static_cast<NavigationView*>(lv_event_get_user_data(event));
    self->model_.close_search(); self->rebuild();
}
void NavigationView::query_changed_cb(lv_event_t* event)
{
    auto* self = static_cast<NavigationView*>(lv_event_get_user_data(event));
    const char* query = lv_textarea_get_text(self->query_field_);
    trace_result("ui=query_changed", self->model_.search_revision() + 1u,
                 query ? std::strlen(query) : 0u);
    self->model_.set_query(query ? query : "");
    self->refresh_results();
}
void NavigationView::result_cb(lv_event_t* event)
{
    auto* self = static_cast<NavigationView*>(lv_event_get_user_data(event));
    const auto value = reinterpret_cast<std::size_t>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(
            lv_event_get_current_target(event))));
    trace_result("ui=result_click", self->model_.route_revision(), value);
    if (value && self->model_.select_destination(value - 1))
        self->rebuild();
}
void NavigationView::route_cb(lv_event_t* event)
{
    auto* self = static_cast<NavigationView*>(lv_event_get_user_data(event));
    const auto value = reinterpret_cast<std::size_t>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(
            lv_event_get_current_target(event))));
    if (value && self->model_.select_route(value - 1)) self->rebuild();
}
bool NavigationView::start_guidance(std::size_t route)
{
    if (!model_.select_route(route) || !model_.start_guidance())
        return false;
    rebuild();
    return true;
}
void NavigationView::start_cb(lv_event_t* event)
{
    auto* self = static_cast<NavigationView*>(lv_event_get_user_data(event));
    if (self->model_.start_guidance()) self->rebuild();
}

void NavigationView::end_cb(lv_event_t* event)
{
    auto* self = static_cast<NavigationView*>(lv_event_get_user_data(event));
    if (self->model_.end_guidance()) self->rebuild();
}

} // namespace navicore_osmand::ui
