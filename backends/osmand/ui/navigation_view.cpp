#include "navigation_view.h"

#include "../diagnostics.h"
#include "assets/nav_fonts.h"
#include "assets/nav_icons.h"

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
constexpr int marker_size = 64;  // nav_icon_vehicle_64
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

// ---- Uconnect navigation design (Nav component, 1024x574) ----
// Tokens and geometry come from the mockups; every element below is placed
// at the mockup's pixel position, relative to its card.
namespace design {
// Colours shared with the search screen (base, surface, control, line,
// accent, maneuver) are the file-level tokens above.
constexpr uint32_t text1 = 0xeef0f3;
constexpr uint32_t text2 = 0xa4abb5;
constexpr uint32_t text3 = 0x6f7782;
constexpr uint32_t warning = 0xf6c86a;
constexpr uint32_t warning_bg = 0x2e2612;
constexpr uint32_t danger = 0xff8a80;
constexpr uint32_t danger_bg = 0x3a1614;
constexpr uint32_t white = 0xffffff;
constexpr int margin = 16;      // screen edge to cards and controls
constexpr int column = 304;     // card width in the 336 px column
constexpr int rail = 336;
constexpr int tile = 64;    // touch target / control size
// Turn countdown bar spans the last 400 m before a manoeuvre.
constexpr double countdown_metres = 400.0;

// CSS box-shadow 0 Y B rgba(0,0,0,A): LVGL spreads its blur over the full
// width, CSS over twice the blur radius.
void shadow(lv_obj_t* object, int offset_y, int blur, float alpha)
{
    lv_obj_set_style_shadow_width(object, blur * 2, 0);
    lv_obj_set_style_shadow_offset_y(object, offset_y, 0);
    lv_obj_set_style_shadow_color(object, lv_color_hex(0), 0);
    lv_obj_set_style_shadow_opa(object, static_cast<lv_opa_t>(alpha * 255.0f + 0.5f), 0);
}
void card_shadow(lv_obj_t* object) { shadow(object, 4, 16, 0.45f); }

// A plain rectangle without theme styles. Clickable boxes absorb presses
// (cards over the map must not pan it); decorations pass them to the parent.
lv_obj_t* box(lv_obj_t* parent, int x, int y, int width, int height,
              uint32_t background, int radius, bool clickable = false)
{
    lv_obj_t* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    no_scroll(object);
    lv_obj_set_clickable(object, clickable);
    return object;
}

void border(lv_obj_t* object, int width, uint32_t colour)
{
    lv_obj_set_style_border_width(object, width, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(colour), 0);
    lv_obj_set_style_border_opa(object, LV_OPA_COVER, 0);
}

// CSS inset box-shadow ring: drawn inside the box without moving its
// children (an LVGL border would inset the content area).
void ring(lv_obj_t* object, int width, uint32_t colour)
{
    lv_obj_set_style_outline_width(object, width, 0);
    lv_obj_set_style_outline_pad(object, -width, 0);
    lv_obj_set_style_outline_color(object, lv_color_hex(colour), 0);
    lv_obj_set_style_outline_opa(object, LV_OPA_COVER, 0);
}

int text_width(const char* text, const lv_font_t* font, int letter_space = 0)
{
    lv_point_t size{};
    lv_text_get_size(&size, text, font, letter_space, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    return size.x;
}

// A label whose top is the CSS text box top (fonts use browser metrics).
lv_obj_t* text(lv_obj_t* parent, const char* value, const lv_font_t* font,
               uint32_t colour, int x, int y)
{
    lv_obj_t* object = lv_label_create(parent);
    lv_obj_remove_style_all(object);
    lv_label_set_text(object, value);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(colour), 0);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_clickable(object, false);
    return object;
}

// Fixed-width text ending in an ellipsis after `lines` lines.
lv_obj_t* clipped_text(lv_obj_t* parent, const char* value, const lv_font_t* font,
                       uint32_t colour, int x, int y, int width, int lines = 1,
                       int line_height = 0)
{
    lv_obj_t* object = text(parent, value, font, colour, x, y);
    const int line = line_height ? line_height : lv_font_get_line_height(font);
    lv_obj_set_style_text_line_space(object, line - lv_font_get_line_height(font), 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(object, width, line * lines - (line - lv_font_get_line_height(font)));
    return object;
}

// Number of lines `value` wraps to in `width` (for line-clamped text).
int wrapped_lines(const char* value, const lv_font_t* font, int width)
{
    lv_point_t size{};
    lv_text_get_size(&size, value, font, 0, 0, width, LV_TEXT_FLAG_NONE);
    const int line = lv_font_get_line_height(font);
    return std::max(1, static_cast<int>((size.y + line / 2) / line));
}

// Design icons are A8 masks drawn in the given colour.
lv_obj_t* icon(lv_obj_t* parent, const lv_image_dsc_t* image, uint32_t colour,
               int x, int y)
{
    lv_obj_t* object = lv_image_create(parent);
    lv_obj_remove_style_all(object);
    lv_image_set_src(object, image);
    lv_obj_set_style_image_recolor(object, lv_color_hex(colour), 0);
    lv_obj_set_style_image_recolor_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_clickable(object, false);
    return object;
}

const lv_image_dsc_t* glyph(ManeuverKind kind, bool large)
{
    switch (kind) {
    case ManeuverKind::Left: return large ? &nav_icon_glyph_left_72 : &nav_icon_glyph_left_32;
    case ManeuverKind::SlightLeft: return large ? &nav_icon_glyph_slight_left_72 : &nav_icon_glyph_slight_left_32;
    case ManeuverKind::SharpLeft: return large ? &nav_icon_glyph_sharp_left_72 : &nav_icon_glyph_sharp_left_32;
    case ManeuverKind::Right: return large ? &nav_icon_glyph_right_72 : &nav_icon_glyph_right_32;
    case ManeuverKind::SlightRight: return large ? &nav_icon_glyph_slight_right_72 : &nav_icon_glyph_slight_right_32;
    case ManeuverKind::SharpRight: return large ? &nav_icon_glyph_sharp_right_72 : &nav_icon_glyph_sharp_right_32;
    case ManeuverKind::KeepLeft: return large ? &nav_icon_glyph_keep_left_72 : &nav_icon_glyph_keep_left_32;
    case ManeuverKind::KeepRight: return large ? &nav_icon_glyph_keep_right_72 : &nav_icon_glyph_keep_right_32;
    case ManeuverKind::UTurn: return large ? &nav_icon_glyph_uturn_72 : &nav_icon_glyph_uturn_32;
    case ManeuverKind::Roundabout: return large ? &nav_icon_glyph_roundabout_72 : &nav_icon_glyph_roundabout_32;
    case ManeuverKind::Arrive: return large ? &nav_icon_glyph_arrive_72 : &nav_icon_glyph_arrive_32;
    case ManeuverKind::Straight: break;
    }
    return large ? &nav_icon_glyph_straight_72 : &nav_icon_glyph_straight_32;
}
} // namespace design

std::string maneuver_text(const Maneuver& maneuver)
{
    if (maneuver.kind == ManeuverKind::Roundabout && maneuver.roundabout_exit > 0) {
        static const char* const ordinals[] = {"1st", "2nd", "3rd"};
        char text[64] = {};
        if (maneuver.roundabout_exit <= 3)
            std::snprintf(text, sizeof(text), "At the roundabout, take the %s exit",
                          ordinals[maneuver.roundabout_exit - 1]);
        else
            std::snprintf(text, sizeof(text), "At the roundabout, take the %dth exit",
                          maneuver.roundabout_exit);
        return text;
    }
    return maneuver_action(maneuver.kind);
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
    // Read on demand from process_input(), not on LVGL's 33 ms timer.
    lv_indev_set_mode(indev_, LV_INDEV_MODE_EVENT);
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
    // Design Montserrat with Cyrillic (search results are Ukrainian names).
    const lv_font_t* font = &nav_font_14_400;
    if (font_size >= 28) font = &nav_font_28_800;
    else if (font_size >= 24) font = &nav_font_24_700;
    else if (font_size >= 18) font = &nav_font_20_600;
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

// Free drive (design: "Free drive · recent · night · following"): the map
// fills the screen; "Where to?" and up to three recents float over it.
void NavigationView::build_free_drive()
{
    using namespace design;
    const int top = content_top(height_);
    build_vehicle_marker();  // below the cards

    lv_obj_t* search = box(screen_, margin, top + margin, column, tile, surface, 8, true);
    card_shadow(search);
    icon(search, &nav_icon_search_26, accent, 18, 19);
    design::text(search, "Where to?", &nav_font_20_600, text1, 56, 20);
    lv_obj_add_event_cb(search, open_search_cb, LV_EVENT_CLICKED, this);

    build_recent_card(top + margin + tile + 8);
    build_map_controls(true);
}

// Recent destinations card: RECENT, then rows of clock, title over
// subtitle, distance; or the empty state.
void NavigationView::build_recent_card(int y)
{
    using namespace design;
    constexpr std::size_t visible = 3;
    const auto& recents = model_.recents();
    const std::size_t rows = std::min(visible, recents.size());
    const int height = rows ? 12 + 22 + static_cast<int>(rows) * 64 + 12 : 192;
    lv_obj_t* card = box(screen_, margin, y, column, height, surface, 8, true);
    card_shadow(card);
    lv_obj_t* heading = design::text(card, "RECENT", &nav_font_14_700, text3, 16, 12);
    lv_obj_set_style_text_letter_space(heading, 1, 0);

    if (!rows) {
        icon(card, &nav_icon_clock_40, text3, (column - 40) / 2, 58);
        const char* title = "No recent destinations";
        design::text(card, title, &nav_font_20_600, text2,
                     (column - text_width(title, &nav_font_20_600)) / 2, 106);
        const char* hint = "Places you navigate to appear here.";
        design::text(card, hint, &nav_font_14_400, text3,
                     (column - text_width(hint, &nav_font_14_400)) / 2, 138);
        return;
    }
    for (std::size_t index = 0; index < rows; ++index) {
        const auto& item = recents[index];
        lv_obj_t* row = box(card, 12, 34 + static_cast<int>(index) * 64, 280, 64,
                            surface, 0, true);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        border(row, 1, line);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        icon(row, &nav_icon_clock_22, text3, 4, 20);
        int right = 276;  // row width minus 4 px padding
        if (!item.distance.empty()) {
            const int width = text_width(item.distance.c_str(), &nav_font_14_400);
            design::text(row, item.distance.c_str(), &nav_font_14_400, text2,
                         right - width, 23);
            right -= width + 12;
        }
        const bool has_subtitle = !item.subtitle.empty();
        clipped_text(row, item.title.c_str(), &nav_font_20_600, text1,
                     38, has_subtitle ? 11 : 20, right - 38);
        if (has_subtitle)
            clipped_text(row, item.subtitle.c_str(), &nav_font_14_400, text2, 38, 35,
                         right - 38);
        lv_obj_add_event_cb(row, recent_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(index + 1));
    }
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

// Map controls (design toolbar): 64 px tiles 16 px from the right edge,
// 8 px apart; recenter is the round accent button bottom right.
void NavigationView::build_map_controls(bool full)
{
    using namespace design;
    control_recenter_ = control_heading_ = control_north_ = nullptr;
    control_tilt_ = control_night_ = nullptr;
    const MapArea area = map_area();
    if (area.width < tile + 2 * margin)
        return;
    const int x = area.x + area.width - margin - tile;
    int y = area.y + margin;
    const auto add = [&](MapCommand command) {
        lv_obj_t* object = box(screen_, x, y, tile, tile, surface, 12, true);
        shadow(object, 2, 10, 0.35f);
        lv_obj_add_event_cb(object, map_command_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(object, reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(command) + 1u));
        y += tile + 8;
        return object;
    };
    icon(add(MapCommand::ZoomIn), &nav_icon_plus_28, text1, 18, 18);
    icon(add(MapCommand::ZoomOut), &nav_icon_minus_28, text1, 18, 18);
    if (full) {
        lv_obj_t* orientation = add(MapCommand::ToggleOrientation);
        control_heading_ = icon(orientation, &nav_icon_heading_28, accent, 18, 18);
        control_north_ = design::text(orientation, "N", &nav_font_24_800, accent, 0, 0);
        lv_obj_center(control_north_);
        control_tilt_ = design::text(add(MapCommand::ToggleTilt), "2D", &nav_font_20_700,
                                     text1, 0, 20);
        lv_obj_align(control_tilt_, LV_ALIGN_TOP_MID, 0, 20);
        control_night_ = icon(add(MapCommand::ToggleNight), &nav_icon_sun_26, text1, 19, 19);
        // Recenter sits apart at the bottom so it is found without looking
        // at the zoom column; it only shows while the camera is free.
        lv_obj_t* recenter = box(screen_, x, area.y + area.height - margin - tile,
                                 tile, tile, accent, tile / 2, true);
        shadow(recenter, 4, 14, 0.4f);
        icon(recenter, &nav_icon_gps_30, white, 17, 17);
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
    set_hidden(control_heading_, !view.heading_up);
    set_hidden(control_north_, view.heading_up);
    set_label_text(control_tilt_, view.tilted ? "3D" : "2D");
    // The button offers the other palette: a sun on the night map.
    if (control_night_)
        lv_image_set_src(control_night_, view.night ? &nav_icon_sun_26 : &nav_icon_moon_26);
}

// Vehicle marker (design map layer): accent dot with a white ring, heading
// pointer and soft halo, pre-rendered from the mockup; rotated as an image.
void NavigationView::build_vehicle_marker()
{
    marker_visible_ = false;
    lv_obj_t* marker = lv_image_create(screen_);
    lv_obj_remove_style_all(marker);
    lv_image_set_src(marker, &nav_icon_vehicle_64);
    lv_image_set_pivot(marker, marker_size / 2, marker_size / 2);
    lv_obj_set_clickable(marker, false);
    lv_obj_set_hidden(marker, true);
    vehicle_marker_ = marker;
}

void NavigationView::set_vehicle_marker(bool visible, int x, int y,
                                        float rotation_degrees)
{
    if (!vehicle_marker_)
        return;
    // Floating screens show the map, and the car, under and around the cards.
    MapArea area = map_area();
    if (built_screen_ == Screen::FreeDrive || built_screen_ == Screen::Guidance) {
        area.x = 0;
        area.width = static_cast<int>(width_);
    }
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
        lv_image_set_rotation(vehicle_marker_,
                              static_cast<int32_t>(rotation_degrees * 10.0f));
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

std::uint64_t NavigationView::select_place(Destination place)
{
    const std::uint64_t revision = model_.select_place(std::move(place));
    if (revision)
        rebuild();
    return revision;
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

// Route preview (design: "Route preview · one route (today)"): opaque rail
// with destination, route cards and Start; the map to the right.
void NavigationView::build_route_preview()
{
    using namespace design;
    const int top = content_top(height_);
    const int height = content_height(height_);
    build_vehicle_marker();
    lv_obj_t* panel_rail = box(screen_, 0, top, rail, height, surface, 0, true);
    box(screen_, rail, top, 1, height, line, 0);  // box-shadow 1px 0 0 line

    const Destination* destination = model_.destination();
    const char* title = destination ? destination->title.c_str() : "Destination";
    const int title_lines = std::min(2, wrapped_lines(title, &nav_font_24_700, 296));
    lv_obj_t* title_label = clipped_text(panel_rail, title, &nav_font_24_700, text1,
                                         20, 24, 296, title_lines, 30);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_MODE_DOTS);
    const int subtitle_y = 24 + 30 * title_lines + 4;
    if (destination && !destination->subtitle.empty())
        clipped_text(panel_rail, destination->subtitle.c_str(), &nav_font_14_400, text2,
                     20, subtitle_y, 296);

    const auto& routes = model_.routes();
    int y = subtitle_y + 18 + 16;
    for (std::size_t index = 0; index < routes.size(); ++index, y += 94) {
        const auto& route = routes[index];
        const bool selected = index == model_.selected_route();
        lv_obj_t* card = box(panel_rail, margin, y, column, 86, control, 8, true);
        if (!selected)
            lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
        ring(card, selected ? 2 : 1, selected ? accent : line);
        design::text(card, route.duration.c_str(), &nav_font_28_700, text1, 16, 14);
        design::text(card, route.distance.c_str(), &nav_font_20_400, text2, 16, 48);
        if (!route.reason.empty()) {
            const int width = text_width(route.reason.c_str(), &nav_font_14_600) + 20;
            lv_obj_t* tag = box(card, column - 16 - width, 29, width, 28, base, 6);
            design::text(tag, route.reason.c_str(), &nav_font_14_600, text2, 10, 5);
        }
        lv_obj_add_event_cb(card, route_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(card, reinterpret_cast<void*>(index + 1));
    }

    lv_obj_t* start = box(panel_rail, margin, height - margin - tile, column, tile,
                          maneuver, 8, true);
    const int label_width = text_width("Start", &nav_font_20_700);
    const int x = (column - (24 + 10 + label_width)) / 2;
    icon(start, &nav_icon_route_24, white, x, 20);
    design::text(start, "Start", &nav_font_20_700, white, x + 34, 20);
    lv_obj_add_event_cb(start, start_cb, LV_EVENT_CLICKED, this);
    build_map_controls(false);
}

// Guidance (design: "Guidance · turn within 400 m"): manoeuvre card top
// left, trip card bottom left, both floating over the full-width map.
void NavigationView::build_guidance()
{
    using namespace design;
    const int top = content_top(height_);
    const int height = content_height(height_);
    const auto& guidance = model_.guidance();
    build_vehicle_marker();

    if (guidance.position_available) {
        guidance_card_ = box(screen_, margin, top + margin, column, 112, maneuver, 8, true);
        card_shadow(guidance_card_);
        lv_obj_set_style_clip_corner(guidance_card_, true, 0);
        guidance_glyph_ = icon(guidance_card_, design::glyph(guidance.maneuver.kind, true),
                               white, 16, 20);
        guidance_exit_ = design::text(guidance_card_, "", &nav_font_14_800, white, 0, 0);
        lv_obj_set_width(guidance_exit_, 18);
        lv_obj_set_style_text_align(guidance_exit_, LV_TEXT_ALIGN_CENTER, 0);
        guidance_simulation_ = box(guidance_card_, 102, 14, 0, 22, warning, 4);
        lv_obj_t* tag = design::text(guidance_simulation_, "ROUTE SIMULATION",
                                     &nav_font_14_800, base, 8, 2);
        lv_obj_set_style_text_letter_space(tag, 1, 0);
        lv_obj_set_width(guidance_simulation_,
                         text_width("ROUTE SIMULATION", &nav_font_14_800, 1) + 16);
        guidance_distance_ = design::text(guidance_card_, "", &nav_font_28_800, white, 102, 0);
        guidance_action_ = clipped_text(guidance_card_, "", &nav_font_20_600, white,
                                        102, 0, 186, 2);
        guidance_road_ = clipped_text(guidance_card_, "", &nav_font_20_400, white, 102, 0, 186);
        lv_obj_set_style_text_opa(guidance_road_, static_cast<lv_opa_t>(0.8f * 255 + 0.5f), 0);
        guidance_bar_ = box(guidance_card_, 0, 0, column, 6, white, 0);
        lv_obj_set_style_bg_opa(guidance_bar_, static_cast<lv_opa_t>(0.22f * 255 + 0.5f), 0);
        guidance_bar_fill_ = box(guidance_card_, 0, 0, 0, 6, white, 0);
    } else {
        // No GPS fix: warning card, then the next instruction.
        lv_obj_t* card = box(screen_, margin, top + margin, column, 168, warning_bg, 8, true);
        ring(card, 2, warning);
        lv_obj_t* badge = box(card, (column - 64) / 2, 18, 64, 64, warning, 32);
        icon(badge, &nav_icon_no_gps_38, base, 13, 13);
        const char* title = "No GPS fix";
        design::text(card, title, &nav_font_24_800, warning,
                     (column - text_width(title, &nav_font_24_800)) / 2, 92);
        lv_obj_t* body = design::text(card, "Guidance is paused until a position is available.",
                                      &nav_font_20_400, text1, 16, 128);
        lv_obj_set_width(body, column - 32);
        lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(body, 2, 0);
        const int lines = wrapped_lines(lv_label_get_text(body), &nav_font_20_400, column - 32);
        const int card_height = std::max(168, 127 + 26 * lines + 16);
        lv_obj_set_height(card, card_height);
        const int next_y = top + margin + card_height + 16;
        lv_obj_t* next = design::text(screen_, "NEXT ON ROUTE", &nav_font_14_700, text3,
                                      margin, next_y);
        lv_obj_set_style_text_letter_space(next, 1, 0);
        guidance_next_glyph_ = icon(screen_, design::glyph(guidance.maneuver.kind, false),
                                    text2, margin, next_y + 26 + 12);
        guidance_instruction_ = clipped_text(screen_, "", &nav_font_20_400, text2,
                                             margin + 44, next_y + 26 + 16, column - 44);
    }

    // Trip card: remaining time over distance, Overview and End.
    lv_obj_t* trip = box(screen_, margin, top + height - margin - 88, column, 88, surface, 8, true);
    card_shadow(trip);
    guidance_time_ = design::text(trip, "", &nav_font_28_800, text1, 16, 15);
    guidance_remaining_ = design::text(trip, "", &nav_font_20_400, text2, 16, 48);
    const int end_width = text_width("End", &nav_font_20_700) + 44;
    lv_obj_t* end = box(trip, column - 12 - end_width, 12, end_width, tile, danger_bg, 8, true);
    design::text(end, "End", &nav_font_20_700, danger, 22, 20);
    lv_obj_add_event_cb(end, end_cb, LV_EVENT_CLICKED, this);
    lv_obj_t* overview = box(trip, column - 12 - end_width - 8 - tile, 12, tile, tile,
                             control, 8, true);
    icon(overview, &nav_icon_overview_26, text1, 19, 19);
    lv_obj_add_event_cb(overview, map_command_cb, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(overview, reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(MapCommand::Overview) + 1u));

    build_map_controls(true);
    refresh_guidance();
}

void NavigationView::layout_guidance_card()
{
    if (!guidance_card_)
        return;
    const auto& guidance = model_.guidance();
    const bool simulated = guidance.simulated;
    const int lines = std::min(2, design::wrapped_lines(lv_label_get_text(guidance_action_),
                                                &nav_font_20_600, 186));
    // Column: [tag 22 + 2], distance line 32, 2, action 24/line, 2, road 24.
    const int column_top = 14 + (simulated ? 24 : 0);
    const int column_height = (simulated ? 24 : 0) + 32 + 2 + 24 * lines + 2 + 24;
    const int height = column_height + 28;
    lv_obj_set_height(guidance_card_, height);
    set_hidden(guidance_simulation_, !simulated);
    lv_obj_set_y(guidance_distance_, column_top - 1);  // 28 px text in a 32 px line
    lv_obj_set_y(guidance_action_, column_top + 34);
    lv_obj_set_height(guidance_action_, 24 * lines);
    lv_obj_set_y(guidance_road_, column_top + 34 + 24 * lines + 2);
    const int glyph_y = (height - 72) / 2;
    lv_obj_set_y(guidance_glyph_, glyph_y);
    lv_obj_set_pos(guidance_exit_, 16 + 27, glyph_y + 17);

    // Countdown to a turn: the bar empties over the last 400 m.
    const auto kind = guidance.maneuver.kind;
    const bool near = !guidance.arrived && kind != ManeuverKind::Straight &&
        kind != ManeuverKind::Arrive &&
        guidance.distance_to_maneuver_metres <= design::countdown_metres;
    set_hidden(guidance_bar_, !near);
    set_hidden(guidance_bar_fill_, !near);
    if (near) {
        const double fraction = std::clamp(
            guidance.distance_to_maneuver_metres / design::countdown_metres, 0.0, 1.0);
        lv_obj_set_y(guidance_bar_, height - 6);
        lv_obj_set_pos(guidance_bar_fill_, 0, height - 6);
        lv_obj_set_width(guidance_bar_fill_,
                         static_cast<int32_t>(std::lround(design::column * fraction)));
    }
}

void NavigationView::refresh_guidance()
{
    const auto& guidance = model_.guidance();
    const Destination* destination = model_.destination();
    if (!guidance.position_available) {
        if (guidance_instruction_) {
            std::string instruction = maneuver_text(guidance.maneuver);
            if (!guidance.maneuver.road.empty())
                instruction += " · " + guidance.maneuver.road;
            set_label_text(guidance_instruction_, instruction.c_str());
        }
        if (guidance_next_glyph_)
            lv_image_set_src(guidance_next_glyph_, design::glyph(guidance.maneuver.kind, false));
        set_label_text(guidance_time_, "--:--");
        set_label_text(guidance_remaining_, "ETA paused");
        return;
    }

    if (guidance_glyph_ && lv_image_get_src(guidance_glyph_) !=
                               design::glyph(guidance.maneuver.kind, true))
        lv_image_set_src(guidance_glyph_, design::glyph(guidance.maneuver.kind, true));
    if (guidance_exit_) {
        const int exit = guidance.maneuver.kind == ManeuverKind::Roundabout
            ? guidance.maneuver.roundabout_exit : 0;
        const std::string number = exit > 0 ? std::to_string(exit) : std::string();
        set_label_text(guidance_exit_, number.c_str());
    }
    const std::string next_distance = guidance.arrived ? "0 m" :
        distance_text(guidance.distance_to_maneuver_metres);
    set_label_text(guidance_distance_, next_distance.c_str());
    set_label_text(guidance_action_, maneuver_text(guidance.maneuver).c_str());
    if (guidance_road_) {
        const char* road = !guidance.maneuver.road.empty()
            ? guidance.maneuver.road.c_str()
            : (destination ? destination->title.c_str() : "Route");
        set_label_text(guidance_road_, road);
    }
    const std::string time = duration_text(guidance.remaining_seconds);
    set_label_text(guidance_time_, time.c_str());
    set_label_text(guidance_remaining_, distance_text(guidance.remaining_metres).c_str());
    layout_guidance_card();
}

void NavigationView::clear_object_references() noexcept
{
    results_panel_ = nullptr;
    query_field_ = nullptr;
    guidance_card_ = nullptr;
    guidance_glyph_ = nullptr;
    guidance_exit_ = nullptr;
    guidance_simulation_ = nullptr;
    guidance_distance_ = nullptr;
    guidance_action_ = nullptr;
    guidance_road_ = nullptr;
    guidance_bar_ = nullptr;
    guidance_bar_fill_ = nullptr;
    guidance_time_ = nullptr;
    guidance_remaining_ = nullptr;
    guidance_next_glyph_ = nullptr;
    guidance_instruction_ = nullptr;
    control_recenter_ = nullptr;
    control_heading_ = nullptr;
    control_north_ = nullptr;
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

void NavigationView::process_input(uint32_t elapsed_ms)
{
    lv_tick_inc(elapsed_ms);
    if (!indev_)
        return;
    // One read per queued sample (the callback consumes one each), and at
    // least one per frame so held presses keep LVGL's press timing.
    std::size_t reads = 0;
    do {
        lv_indev_read(indev_);
    } while (!pointer_queue_.empty() && ++reads < 64);
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

// Bare-map gestures: a press that moves past the slop is a drag (absolute
// positions to the model); a short press that stays inside it is a tap.
void NavigationView::handle_map_gesture(const PointerSample& sample)
{
    constexpr int32_t slop_px = 10;
    constexpr uint32_t tap_ms = 500;
    if (sample.action == MOCK_NAV_BACKEND_POINTER_PRESS) {
        map_drag_ = false;
        map_dragging_ = false;
        if (built_screen_ == Screen::Search || !screen_)
            return;
        lv_point_t point{sample.x, sample.y};
        lv_obj_t* hit = lv_indev_search_obj(screen_, &point);
        // Only presses on bare map (the transparent screen or the
        // non-clickable vehicle marker) pan the map.
        if (hit == screen_ || hit == nullptr || hit == vehicle_marker_) {
            map_drag_ = true;
            press_x_ = drag_last_x_ = sample.x;
            press_y_ = drag_last_y_ = sample.y;
            press_tick_ = lv_tick_get();
        }
        return;
    }
    if (!map_drag_)
        return;
    drag_last_x_ = sample.x;
    drag_last_y_ = sample.y;
    if (!map_dragging_) {
        const int32_t dx = sample.x - press_x_, dy = sample.y - press_y_;
        if (dx * dx + dy * dy > slop_px * slop_px) {
            map_dragging_ = true;
            model_.begin_drag(press_x_, press_y_, sample.x, sample.y);
        }
    } else {
        model_.move_drag(sample.x, sample.y);
    }
    if (sample.action == MOCK_NAV_BACKEND_POINTER_RELEASE) {
        if (map_dragging_)
            model_.end_drag();
        else if (lv_tick_elaps(press_tick_) <= tap_ms)
            model_.push_map_tap(press_x_, press_y_);
        map_drag_ = false;
        map_dragging_ = false;
    }
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
