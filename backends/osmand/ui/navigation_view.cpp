#include "navigation_view.h"

#include "../diagnostics.h"
#include "assets/nav_fonts.h"
#include "assets/nav_icons.h"
#include "localization.h"

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
    return loc::format_distance(metres, loc::Rounding::Coarse);
}

std::string duration_text(double seconds)
{
    return loc::format_duration_minutes(
        std::max(0, static_cast<int>(std::ceil(seconds / 60.0))));
}

const char* maneuver_action(ManeuverKind kind)
{
    return loc::maneuver_action(kind);
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
const lv_image_dsc_t* lane_icon(const Lane& lane)
{
    const auto straight = ManeuverKind::Straight;
    if (lane.combined && lane.kind == straight) {
        if (lane.also == ManeuverKind::Right || lane.also == ManeuverKind::SlightRight)
            return &nav_icon_lane_straight_right_40;
        if (lane.also == ManeuverKind::Left || lane.also == ManeuverKind::SlightLeft)
            return &nav_icon_lane_straight_left_40;
    }
    switch (lane.kind) {
    case ManeuverKind::Left: case ManeuverKind::KeepLeft: return &nav_icon_lane_left_40;
    case ManeuverKind::Right: case ManeuverKind::KeepRight: return &nav_icon_lane_right_40;
    case ManeuverKind::SlightLeft: return &nav_icon_lane_slight_left_40;
    case ManeuverKind::SlightRight: return &nav_icon_lane_slight_right_40;
    case ManeuverKind::SharpLeft: return &nav_icon_lane_sharp_left_40;
    case ManeuverKind::SharpRight: return &nav_icon_lane_sharp_right_40;
    case ManeuverKind::UTurn: return &nav_icon_lane_uturn_40;
    default: break;
    }
    return &nav_icon_lane_straight_40;
}

// Guidance status card (No GPS fix, Recalculating): badge, title and a
// wrapped explanation, centred; at least 168 px tall. Returns its height.
int status_card(lv_obj_t* parent, int x, int y, uint32_t background, uint32_t colour,
                const lv_image_dsc_t* image, uint32_t image_colour, const char* title,
                const char* body)
{
    lv_obj_t* card = box(parent, x, y, column, 168, background, 8, true);
    ring(card, 2, colour);
    lv_obj_t* badge = box(card, (column - 64) / 2, 18, 64, 64, colour, 32);
    icon(badge, image, image_colour, 13, 13);
    // A long title (other languages) steps down a size to stay inside the card.
    const lv_font_t* title_font = text_width(title, &nav_font_24_800) > column - 16
        ? &nav_font_20_700 : &nav_font_24_800;
    text(card, title, title_font, colour, (column - text_width(title, title_font)) / 2,
         92 + (lv_font_get_line_height(&nav_font_24_800) - lv_font_get_line_height(title_font)) / 2);
    lv_obj_t* label = text(card, body, &nav_font_20_400, text1, 16, 128);
    lv_obj_set_width(label, column - 32);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(label, 2, 0);  // 26 px lines
    const int lines = wrapped_lines(body, &nav_font_20_400, column - 32);
    const int height = std::max(168, 127 + 26 * lines + 16);
    lv_obj_set_height(card, height);
    return height;
}
} // namespace design

std::string maneuver_text(const Maneuver& maneuver)
{
    if (maneuver.kind == ManeuverKind::Roundabout && maneuver.roundabout_exit > 0)
        return loc::roundabout_exit_text(maneuver.roundabout_exit);
    return maneuver_action(maneuver.kind);
}

} // namespace

NavigationView::NavigationView(NavigationModel& model, uint32_t width,
                               uint32_t height)
    : model_(model), width_(width), height_(height)
{
    keyboard_ukrainian_ = loc::language() == loc::Language::Ukrainian;
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
    // Only LVGL long press: the developer menu (day/night button).
    lv_indev_set_long_press_time(indev_, 1500);
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
    build_pin_marker();
    if (model_.dev_menu_open()) {
        build_dev_menu();
        return;
    }

    lv_obj_t* search = box(screen_, margin, top + margin, column, tile, surface, 8, true);
    card_shadow(search);
    icon(search, &nav_icon_search_26, accent, 18, 19);
    design::text(search, loc::tr(loc::Str::WhereTo), &nav_font_20_600, text1, 56, 20);
    lv_obj_add_event_cb(search, open_search_cb, LV_EVENT_CLICKED, this);

    build_recent_card(top + margin + tile + 8);
    build_place_card();
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
    lv_obj_t* heading = design::text(card, loc::tr(loc::Str::Recent), &nav_font_14_700, text3, 16, 12);
    lv_obj_set_style_text_letter_space(heading, 1, 0);

    if (!rows) {
        icon(card, &nav_icon_clock_40, text3, (column - 40) / 2, 58);
        const char* title = loc::tr(loc::Str::NoRecent);
        design::text(card, title, &nav_font_20_600, text2,
                     (column - text_width(title, &nav_font_20_600)) / 2, 106);
        const char* hint = loc::tr(loc::Str::NoRecentHint);
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
        lv_obj_t* night = add(MapCommand::ToggleNight);
        // A short tap toggles day/night; holding 1.5 s (standing still)
        // opens the developer menu instead.
        lv_obj_remove_event_cb(night, map_command_cb);
        lv_obj_add_event_cb(night, night_short_cb, LV_EVENT_SHORT_CLICKED, this);
        lv_obj_add_event_cb(night, dev_open_cb, LV_EVENT_LONG_PRESSED, this);
        control_night_ = icon(night, &nav_icon_sun_26, text1, 19, 19);
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
    layout_street_pill();  // its row ends at the recenter button
    // Shows the current mode: an arrow for heading-up, N for north-up.
    set_hidden(control_heading_, !view.heading_up);
    set_hidden(control_north_, view.heading_up);
    // Heading-up with no heading yet (no movement seen): dimmed.
    if (control_heading_)
        lv_obj_set_style_image_recolor(
            control_heading_, lv_color_hex(view.heading_known ? accent : design::text3), 0);
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

// Dropped pin (long press): an accent badge standing on a small dot at the
// ground point. 40 x 54 px; the dot centre is 5 px above the bottom.
void NavigationView::build_pin_marker()
{
    using namespace design;
    pin_visible_ = false;
    lv_obj_t* pin = box(screen_, 0, 0, 40, 54, base, 0);
    lv_obj_set_style_bg_opa(pin, LV_OPA_TRANSP, 0);
    lv_obj_t* dot = box(pin, 15, 44, 10, 10, accent, 5);
    ring(dot, 2, white);
    lv_obj_t* badge = box(pin, 0, 0, 40, 40, accent, 20);
    ring(badge, 3, white);
    shadow(badge, 3, 10, 0.45f);
    icon(badge, &nav_icon_badge_place_26, white, 7, 7);
    lv_obj_set_hidden(pin, true);
    pin_marker_ = pin;
}

// Developer menu (polish list): sections of key/value rows in two columns
// and a column of toggles, over the free-drive map. Opened by holding the
// day/night button; never while moving (the model closes it).
void NavigationView::build_dev_menu()
{
    using namespace design;
    dev_built_ = true;
    dev_values_.clear();
    const int top = content_top(height_);
    const int width = static_cast<int>(width_) - 2 * margin;
    const int height = content_height(height_) - 2 * margin;
    lv_obj_t* panel = box(screen_, margin, top + margin, width, height, surface, 12, true);
    shadow(panel, 8, 28, 0.5f);
    design::text(panel, "Developer", &nav_font_24_700, text1, 20, 18);
    lv_obj_t* close = box(panel, width - 16 - 120, 12, 120, 48, control, 8, true);
    design::text(close, "Close", &nav_font_20_600, text1,
                 (120 - text_width("Close", &nav_font_20_600)) / 2, 14);
    lv_obj_add_event_cb(close, dev_close_cb, LV_EVENT_CLICKED, this);

    // Toggles on the right, 48 px rows 6 px apart (a bench screen: below
    // the 64 px driving target on purpose, to fit every switch).
    constexpr int toggle_width = 250;
    const int toggle_x = width - 16 - toggle_width;
    int y = 72;
    const auto& toggles = model_.dev_toggles();
    for (std::size_t i = 0; i < toggles.size() && y + 48 <= height - 8; ++i) {
        const auto& toggle = toggles[i];
        lv_obj_t* row = box(panel, toggle_x, y, toggle_width, 48,
                            toggle.on ? maneuver : control, 8, true);
        clipped_text(row, toggle.label.c_str(), &nav_font_20_600, white, 14,
                     toggle.note.empty() ? 12 : 3, toggle_width - 28);
        if (!toggle.note.empty())
            clipped_text(row, toggle.note.c_str(), &nav_font_14_400, text2, 14, 26,
                         toggle_width - 28);
        lv_obj_add_event_cb(row, dev_toggle_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(i + 1));
        y += 48 + 6;
    }

    // Sections flow down two columns left of the toggles, 20 px rows.
    const int columns_width = toggle_x - 16 - 20;
    const int column_width = (columns_width - 16) / 2;
    const int key_width = 124;
    int column = 0;
    y = 72;
    for (const auto& section : model_.dev_sections()) {
        const int needed = 24 + 20 * static_cast<int>(section.rows.size()) + 10;
        if (y + needed > height - 8 && column == 0) {
            column = 1;
            y = 72;
        }
        const int x = 20 + column * (column_width + 16);
        lv_obj_t* title = design::text(panel, section.title.c_str(), &nav_font_14_800, text3, x, y);
        lv_obj_set_style_text_letter_space(title, 1, 0);
        y += 24;
        for (const auto& row : section.rows) {
            clipped_text(panel, row.first.c_str(), &nav_font_14_400, text2, x, y, key_width - 8);
            dev_values_.push_back(clipped_text(panel, row.second.c_str(), &nav_font_14_600,
                                               text1, x + key_width, y,
                                               column_width - key_width));
            y += 20;
        }
        y += 10;
    }
}

bool NavigationView::open_dev_menu()
{
    if (!model_.open_dev_menu())
        return false;
    trace("ui=dev_menu");
    rebuild();
    return true;
}

bool NavigationView::close_dev_menu()
{
    if (!model_.close_dev_menu())
        return false;
    rebuild();
    return true;
}

bool NavigationView::set_dev_info(std::vector<DevSection> sections,
                                  std::vector<DevToggle> toggles)
{
    const bool layout = model_.set_dev_info(std::move(sections), std::move(toggles));
    if (!dev_built_ || !model_.dev_menu_open())
        return false;
    if (layout) {
        rebuild();
        return true;
    }
    std::size_t index = 0;
    for (const auto& section : model_.dev_sections())
        for (const auto& row : section.rows) {
            if (index < dev_values_.size())
                set_label_text(dev_values_[index], row.second.c_str());
            ++index;
        }
    return true;
}

void NavigationView::night_short_cb(lv_event_t* event)
{
    static_cast<NavigationView*>(lv_event_get_user_data(event))
        ->model_.push_map_command(MapCommand::ToggleNight);
}

void NavigationView::dev_open_cb(lv_event_t* event)
{
    (void)static_cast<NavigationView*>(lv_event_get_user_data(event))->open_dev_menu();
}

void NavigationView::dev_close_cb(lv_event_t* event)
{
    (void)static_cast<NavigationView*>(lv_event_get_user_data(event))->close_dev_menu();
}

void NavigationView::dev_toggle_cb(lv_event_t* event)
{
    auto* self = static_cast<NavigationView*>(lv_event_get_user_data(event));
    const auto value = reinterpret_cast<std::size_t>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_current_target(event))));
    if (value)
        self->model_.push_dev_action(value - 1);
}

void NavigationView::set_pin_marker(bool visible, int x, int y)
{
    if (!pin_marker_)
        return;
    visible = visible && x >= -40 && x < static_cast<int>(width_) + 40 &&
              y >= 0 && y < static_cast<int>(height_) + 54;
    if (!visible) {
        if (pin_visible_)
            set_hidden(pin_marker_, true);
        pin_visible_ = false;
        return;
    }
    if (pin_visible_ && x == pin_x_ && y == pin_y_)
        return;
    lv_obj_set_pos(pin_marker_, x - 20, y - 49);
    set_hidden(pin_marker_, false);
    pin_visible_ = true;
    pin_x_ = x;
    pin_y_ = y;
}

bool NavigationView::set_place_subtitle(const std::string& id, std::string subtitle)
{
    if (!model_.set_place_subtitle(id, std::move(subtitle)))
        return false;
    if (built_screen_ == Screen::FreeDrive)
        rebuild();
    return true;
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
    if (built_screen_ == Screen::Search || (dev_built_ && !model_.dev_menu_open()))
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

// Search (design: "Search · results", full-width keyboard): Back and the
// query field on top, a 2x2 grid of the best results (or a status message)
// below, and the design keyboard - or, while driving, recent destinations.
void NavigationView::build_search()
{
    using namespace design;
    const int top = content_top(height_);
    const bool locked = model_.moving();

    lv_obj_t* back = box(screen_, margin, top + 12, tile, tile, control, 8, true);
    icon(back, &nav_icon_back_28, text1, 18, 18);
    lv_obj_add_event_cb(back, close_search_cb, LV_EVENT_CLICKED, this);

    lv_obj_t* field = box(screen_, 88, top + 12, static_cast<int>(width_) - 88 - margin,
                          tile, surface, 8, true);
    ring(field, 2, line);
    icon(field, locked ? &nav_icon_lock_24 : &nav_icon_search_24, text3, 18, 20);
    query_label_ = design::text(field, "", &nav_font_24_600,
                                text1, 54, 18);
    lv_label_set_long_mode(query_label_, LV_LABEL_LONG_MODE_CLIP);
    query_placeholder_ = design::text(field, loc::tr(loc::Str::SearchPlaceholder), &nav_font_24_400, text3,
                                      60, 18);
    query_cursor_ = box(field, 54, 17, 2, 30, accent, 0);
    set_hidden(query_cursor_, locked);

    results_panel_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(results_panel_);
    lv_obj_set_pos(results_panel_, margin, top + 88);
    lv_obj_set_size(results_panel_, static_cast<int>(width_) - 2 * margin, 172);
    lv_obj_set_clickable(results_panel_, true);  // like the cards: absorbs presses
    no_scroll(results_panel_);

    if (!locked) {
        build_keyboard();
    } else {
        // Typing is limited while moving: recent destinations replace the
        // keyboard (two columns of 64 px rows).
        lv_obj_t* panel_lock = box(screen_, margin, top + 268, static_cast<int>(width_) - 2 * margin,
                                   290, surface, 8, true);
        icon(panel_lock, &nav_icon_lock_30, warning, 16, 21);
        const char* title = loc::tr(loc::Str::TypingPaused);
        design::text(panel_lock, title, &nav_font_20_700, text1, 60, 24);
        design::text(panel_lock, loc::tr(loc::Str::TypingPausedHint), &nav_font_14_400,
                     text2, 60 + text_width(title, &nav_font_20_700) + 14, 27);
        const auto& recents = model_.recents();
        const int column_width = (static_cast<int>(width_) - 2 * margin - 32 - 16) / 2;
        for (std::size_t index = 0; index < std::min<std::size_t>(6, recents.size()); ++index) {
            const auto& item = recents[index];
            const int x = 16 + static_cast<int>(index % 2) * (column_width + 16);
            const int y = 68 + static_cast<int>(index / 2) * 64;
            lv_obj_t* row = box(panel_lock, x, y, column_width, 64, surface, 0, true);
            box(row, 0, 0, column_width, 1, line, 0);  // border-top
            icon(row, &nav_icon_clock_22, text3, 4, 21);
            int right = column_width - 4;
            if (!item.distance.empty()) {
                const int width = text_width(item.distance.c_str(), &nav_font_14_400);
                design::text(row, item.distance.c_str(), &nav_font_14_400, text2, right - width, 24);
                right -= width + 12;
            }
            clipped_text(row, item.title.c_str(), &nav_font_20_600, text1, 38, 21, right - 38);
            lv_obj_add_event_cb(row, recent_cb, LV_EVENT_CLICKED, this);
            lv_obj_set_user_data(row, reinterpret_cast<void*>(index + 1));
        }
        if (recents.empty())
            design::text(panel_lock, loc::tr(loc::Str::NoRecent), &nav_font_20_600, text2, 16, 88);
    }
    refresh_query();
    refresh_results();
}

namespace {
// Design keyboard rows (full-width layout): label, flex weight. The
// language key (УКР/ENG) is ours; the mockup shows the Latin layout only.
struct Key { const char* label; float weight; };
constexpr Key latin[4][12] = {
    {{"q", 1}, {"w", 1}, {"e", 1}, {"r", 1}, {"t", 1}, {"y", 1}, {"u", 1}, {"i", 1}, {"o", 1}, {"p", 1}, {nullptr, 0}, {nullptr, 0}},
    {{"a", 1}, {"s", 1}, {"d", 1}, {"f", 1}, {"g", 1}, {"h", 1}, {"j", 1}, {"k", 1}, {"l", 1}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}},
    {{"⇧", 1.5f}, {"z", 1}, {"x", 1}, {"c", 1}, {"v", 1}, {"b", 1}, {"n", 1}, {"m", 1}, {"⌫", 1.5f}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}},
    {{"123", 1.5f}, {"УКР", 1}, {",", 1}, {"space", 4}, {".", 1}, {"OK", 1.5f}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}}};
constexpr Key latin_upper[4][12] = {
    {{"Q", 1}, {"W", 1}, {"E", 1}, {"R", 1}, {"T", 1}, {"Y", 1}, {"U", 1}, {"I", 1}, {"O", 1}, {"P", 1}, {nullptr, 0}, {nullptr, 0}},
    {{"A", 1}, {"S", 1}, {"D", 1}, {"F", 1}, {"G", 1}, {"H", 1}, {"J", 1}, {"K", 1}, {"L", 1}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}},
    {{"⇧", 1.5f}, {"Z", 1}, {"X", 1}, {"C", 1}, {"V", 1}, {"B", 1}, {"N", 1}, {"M", 1}, {"⌫", 1.5f}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}},
    {{"123", 1.5f}, {"УКР", 1}, {",", 1}, {"space", 4}, {".", 1}, {"OK", 1.5f}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}}};
constexpr Key ukrainian[4][12] = {
    {{"й", 1}, {"ц", 1}, {"у", 1}, {"к", 1}, {"е", 1}, {"н", 1}, {"г", 1}, {"ш", 1}, {"щ", 1}, {"з", 1}, {"х", 1}, {"ї", 1}},
    {{"ф", 1}, {"і", 1}, {"в", 1}, {"а", 1}, {"п", 1}, {"р", 1}, {"о", 1}, {"л", 1}, {"д", 1}, {"ж", 1}, {"є", 1}, {nullptr, 0}},
    {{"⇧", 1.5f}, {"я", 1}, {"ч", 1}, {"с", 1}, {"м", 1}, {"и", 1}, {"т", 1}, {"ь", 1}, {"б", 1}, {"ю", 1}, {"⌫", 1.5f}, {nullptr, 0}},
    {{"123", 1.5f}, {"ENG", 1}, {",", 1}, {"space", 4}, {".", 1}, {"OK", 1.5f}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}}};
constexpr Key ukrainian_upper[4][12] = {
    {{"Й", 1}, {"Ц", 1}, {"У", 1}, {"К", 1}, {"Е", 1}, {"Н", 1}, {"Г", 1}, {"Ш", 1}, {"Щ", 1}, {"З", 1}, {"Х", 1}, {"Ї", 1}},
    {{"Ф", 1}, {"І", 1}, {"В", 1}, {"А", 1}, {"П", 1}, {"Р", 1}, {"О", 1}, {"Л", 1}, {"Д", 1}, {"Ж", 1}, {"Є", 1}, {nullptr, 0}},
    {{"⇧", 1.5f}, {"Я", 1}, {"Ч", 1}, {"С", 1}, {"М", 1}, {"И", 1}, {"Т", 1}, {"Ь", 1}, {"Б", 1}, {"Ю", 1}, {"⌫", 1.5f}, {nullptr, 0}},
    {{"123", 1.5f}, {"ENG", 1}, {",", 1}, {"space", 4}, {".", 1}, {"OK", 1.5f}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}}};
constexpr Key numbers[4][12] = {
    {{"1", 1}, {"2", 1}, {"3", 1}, {"4", 1}, {"5", 1}, {"6", 1}, {"7", 1}, {"8", 1}, {"9", 1}, {"0", 1}, {nullptr, 0}, {nullptr, 0}},
    {{"-", 1}, {"/", 1}, {":", 1}, {";", 1}, {"(", 1}, {")", 1}, {"&", 1}, {"@", 1}, {"\"", 1}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}},
    {{"#", 1}, {"+", 1}, {"=", 1}, {"_", 1}, {"'", 1}, {"?", 1}, {"!", 1}, {"№", 1}, {"⌫", 1.5f}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}},
    {{"ABC", 1.5f}, {",", 1}, {"space", 4}, {".", 1}, {"OK", 1.5f}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}}};
} // namespace

void NavigationView::build_keyboard()
{
    using namespace design;
    if (keyboard_)
        lv_obj_delete(keyboard_);
    const int top = content_top(height_);
    const int width = static_cast<int>(width_) - 2 * margin;
    keyboard_ = box(screen_, margin, top + 268, width, 290, surface, 8, true);
    const int inner = width - 16;
    for (int row = 0; row < 4; ++row) {
        const Key* keys = keyboard_numeric_ ? numbers[row]
            : keyboard_ukrainian_ ? (keyboard_shift_ ? ukrainian_upper[row] : ukrainian[row])
            : (keyboard_shift_ ? latin_upper[row] : latin[row]);
        int count = 0;
        float weights = 0.0f;
        while (count < 12 && keys[count].label) weights += keys[count++].weight;
        // CSS flex with basis 0: only the space beyond each button's 2x6 px
        // padding is shared by weight.
        constexpr float padding = 12.0f;
        const float unit = (inner - 6.0f * (count - 1) - padding * count) / weights;
        float x = 8.0f;
        const int y = 8 + row * 70;
        for (int index = 0; index < count; ++index) {
            const Key& key = keys[index];
            const float key_width = unit * key.weight + padding;
            const int left = static_cast<int>(std::lround(x));
            const int right = static_cast<int>(std::lround(x + key_width));
            const bool ok = std::strcmp(key.label, "OK") == 0;
            lv_obj_t* object = box(keyboard_, left, y, right - left, 64, ok ? accent : control, 6, true);
            lv_obj_set_style_bg_color(object, lv_color_hex(ok ? 0x2f66e0 : line), LV_STATE_PRESSED);
            const float centre = x + key_width / 2.0f - left;
            if (std::strcmp(key.label, "⇧") == 0 || std::strcmp(key.label, "⌫") == 0) {
                const bool shift = std::strcmp(key.label, "⇧") == 0;
                icon(object, shift ? &nav_icon_key_shift_36 : &nav_icon_key_backspace_36,
                     shift && keyboard_shift_ ? accent : text1,
                     static_cast<int>(std::lround(centre - 18.0f)), 19);
            } else {
                const bool small = std::strlen(key.label) > 2;
                const lv_font_t* font = small ? &nav_font_14_600 : &nav_font_24_600;
                const char* shown = std::strcmp(key.label, "space") == 0 ? loc::tr(loc::Str::KeySpace)
                                                                      : key.label;
                design::text(object, shown, font, ok ? white : text1,
                             static_cast<int>(std::lround(centre - text_width(shown, font) / 2.0f)),
                             small ? 23 : 18);
            }
            lv_obj_add_event_cb(object, key_cb, LV_EVENT_CLICKED, this);
            lv_obj_set_user_data(object, const_cast<char*>(key.label));
            x += key_width + 6.0f;
        }
    }
}

void NavigationView::press_key(const char* key)
{
    std::string query = model_.query();
    if (std::strcmp(key, "⇧") == 0) {
        keyboard_shift_ = !keyboard_shift_;
        build_keyboard();
        return;
    }
    if (std::strcmp(key, "УКР") == 0 || std::strcmp(key, "ENG") == 0) {
        keyboard_ukrainian_ = !keyboard_ukrainian_;
        keyboard_shift_ = false;
        build_keyboard();
        return;
    }
    if (std::strcmp(key, "123") == 0 || std::strcmp(key, "ABC") == 0) {
        keyboard_numeric_ = !keyboard_numeric_;
        keyboard_shift_ = false;
        build_keyboard();
        return;
    }
    if (std::strcmp(key, "OK") == 0) {
        // Routes to the highlighted (first) result.
        if (!model_.results().empty() && model_.select_destination(0))
            rebuild();
        return;
    }
    if (std::strcmp(key, "⌫") == 0) {
        if (query.empty())
            return;
        std::size_t end = query.size() - 1;
        while (end > 0 && (static_cast<unsigned char>(query[end]) & 0xc0u) == 0x80u)
            --end;  // whole UTF-8 sequence
        query.erase(end);
    } else {
        query += std::strcmp(key, "space") == 0 ? " " : key;
    }
    trace_result("ui=query_changed", model_.search_revision() + 1u, query.size());
    model_.set_query(std::move(query));
    if (keyboard_shift_) {  // one-shot shift
        keyboard_shift_ = false;
        build_keyboard();
    }
    refresh_query();
    refresh_results();
}

void NavigationView::refresh_query()
{
    if (!query_label_)
        return;
    const std::string& query = model_.query();
    set_label_text(query_label_, query.c_str());
    const bool empty = query.empty();
    set_hidden(query_label_, empty);
    set_hidden(query_placeholder_, !empty);
    const int field_width = static_cast<int>(width_) - 88 - design::margin;
    const int max_width = field_width - 54 - 24;
    const int width = std::min(max_width, design::text_width(query.c_str(), &nav_font_24_600));
    lv_obj_set_width(query_label_, width);
    lv_obj_set_x(query_cursor_, empty ? 54 : 54 + width + 4);
}

void NavigationView::refresh_results()
{
    using namespace design;
    if (!results_panel_)
        return;
    lv_obj_clean(results_panel_);
    const auto& results = model_.results();
    const std::string& message = model_.message();  // English; shown localized
    const int width = static_cast<int>(width_) - 2 * margin;  // before any layout pass
    if (message.empty() && !results.empty()) {
        // Best four results, two per row; the first is the OK target.
        const int card_width = (width - 8) / 2;
        for (std::size_t index = 0; index < std::min<std::size_t>(4, results.size()); ++index) {
            const auto& item = results[index];
            const bool first = index == 0;
            lv_obj_t* card = box(results_panel_, static_cast<int>(index % 2) * (card_width + 8),
                                 static_cast<int>(index / 2) * 90, card_width, 82,
                                 first ? control : surface, 8, true);
            if (first)
                ring(card, 2, accent);
            std::string lower = item.subtitle;
            for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const bool fuel = lower.find("fuel") != std::string::npos ||
                item.subtitle.find(loc::poi_category("fuel")) != std::string::npos;
            icon(card, fuel ? &nav_icon_fuel_24 : &nav_icon_pin_24, text2, 16, 29);
            int right = card_width - 16;
            if (!item.distance.empty()) {
                const int text = text_width(item.distance.c_str(), &nav_font_14_400);
                design::text(card, item.distance.c_str(), &nav_font_14_400, text2, right - text, 32);
                right -= text + 14;
            }
            const bool sub = !item.subtitle.empty();
            clipped_text(card, item.title.c_str(), &nav_font_20_600, text1, 54, sub ? 19 : 29,
                         right - 54);
            if (sub)
                clipped_text(card, item.subtitle.c_str(), &nav_font_14_400, text2, 54, 45, right - 54);
            lv_obj_add_event_cb(card, result_cb, LV_EVENT_CLICKED, this);
            lv_obj_set_user_data(card, reinterpret_cast<void*>(index + 1));
        }
        return;
    }
    // Status: icon and text centred in a 172 px panel.
    const std::string localized = loc::model_message(message);
    const char* text = localized.c_str();
    const lv_image_dsc_t* image = &nav_icon_message_search_32;
    if (model_.query().empty() && message.empty())
        text = loc::tr(loc::Str::TypeToSearch);
    else if (message.rfind("Searching", 0) == 0)
        image = &nav_icon_message_spin_32;
    else if (message.rfind("No offline", 0) == 0)
        image = &nav_icon_message_nores_32;
    else if (message.rfind("Calculating", 0) == 0)
        image = &nav_icon_message_route_32;
    else if (message.rfind("No route", 0) == 0)
        image = &nav_icon_message_alert_32;
    lv_obj_t* panel_status = box(results_panel_, 0, 0, width, 172, surface, 8);
    const int text_w = text_width(text, &nav_font_20_600);
    const int x = (width - (32 + 14 + text_w)) / 2;
    icon(panel_status, image, text3, x, 70);
    design::text(panel_status, text, &nav_font_20_600, text2, x + 46, 74);
}

namespace {
// Place card badge per map icon (design POI_CAT colours).
struct Badge { const lv_image_dsc_t* image; uint32_t colour; };
Badge badge_for(const std::string& icon)
{
    const auto has = [&](const char* word) { return icon.find(word) != std::string::npos; };
    if (has("fuel")) return {&nav_icon_badge_fuel_26, 0x3d7bff};
    if (has("charging")) return {&nav_icon_badge_charging_26, 0x20a46b};
    if (has("parking")) return {&nav_icon_badge_parking_26, 0x4b6bd6};
    if (has("hospital") || has("pharmacy") || has("doctor") || has("clinic"))
        return {&nav_icon_badge_hospital_26, 0xe0524a};
    if (has("restaurant") || has("cafe") || has("fast_food") || has("food") || has("bar"))
        return {&nav_icon_badge_food_26, 0xe08a2c};
    if (has("hotel") || has("motel") || has("hostel") || has("guest"))
        return {&nav_icon_badge_hotel_26, 0x8c62d0};
    if (has("tourism") || has("museum") || has("attraction") || has("monument") ||
        has("theatre") || has("viewpoint"))
        return {&nav_icon_badge_sight_26, 0xc99a2e};
    return {&nav_icon_badge_place_26, 0x3d7bff};
}
} // namespace

// Place card (design proposal G2): a tapped POI with Close and Navigate,
// bottom left over the free-drive map. Navigate calculates the route from
// here: the subtitle line shows "Calculating route…" (Navigate disabled) or
// the failure, and the route opens Route preview.
void NavigationView::build_place_card()
{
    using namespace design;
    const Destination* place = model_.place_card();
    if (!place)
        return;
    const int top = content_top(height_);
    const int height = content_height(height_);
    lv_obj_t* card = box(screen_, margin, top + height - margin - 160, 380, 160, surface, 12, true);
    shadow(card, 8, 28, 0.5f);
    const Badge badge = badge_for(model_.place_card_icon());
    lv_obj_t* circle = box(card, 16, 16, 48, 48, badge.colour, 24);
    icon(circle, badge.image, white, 11, 11);
    clipped_text(card, place->title.c_str(), &nav_font_24_700, text1, 78, 17, 275);
    const bool routing = model_.direct_routing() && !model_.message().empty();
    const bool calculating = routing && model_.calculating_route();
    if (routing)
        clipped_text(card, loc::model_message(model_.message()).c_str(), &nav_font_14_400,
                     calculating ? text2 : danger, 78, 46, 275);
    else if (!place->subtitle.empty())
        clipped_text(card, place->subtitle.c_str(), &nav_font_14_400, text2, 78, 46, 275);
    // Buttons share 348 px as 1fr : 1.4fr with an 8 px gap.
    lv_obj_t* close = box(card, 16, 80, 142, tile, control, 8, true);
    const char* close_text = loc::tr(loc::Str::Close);
    const int close_width = text_width(close_text, &nav_font_20_600);
    design::text(close, close_text, &nav_font_20_600, text1, (142 - close_width) / 2, 20);
    lv_obj_add_event_cb(close, place_close_cb, LV_EVENT_CLICKED, this);
    lv_obj_t* go = box(card, 166, 80, 198, tile, calculating ? control : maneuver, 8,
                       !calculating);
    const char* navigate_text = loc::tr(loc::Str::Navigate);
    const int go_width = 24 + 8 + text_width(navigate_text, &nav_font_20_700);
    const int go_x = (198 - go_width) / 2;
    const uint32_t go_colour = calculating ? text3 : white;
    icon(go, &nav_icon_route_24, go_colour, go_x, 20);
    design::text(go, navigate_text, &nav_font_20_700, go_colour, go_x + 32, 20);
    if (!calculating)
        lv_obj_add_event_cb(go, place_navigate_cb, LV_EVENT_CLICKED, this);
}

// Arrival card (design proposal G5): replaces the manoeuvre and trip cards
// and returns to free drive after 8 s, or on Done.
void NavigationView::build_arrival_card()
{
    using namespace design;
    constexpr uint32_t ok = 0x4cc38a;
    constexpr uint32_t ok_bg = 0x12291f;
    const int top = content_top(height_);
    const Destination* destination = model_.destination();
    std::string where = destination ? destination->title
                                    : std::string(loc::tr(loc::Str::DestinationFallback));
    if (destination && !destination->subtitle.empty())
        where += " · " + destination->subtitle;
    const int lines = std::min(2, wrapped_lines(where.c_str(), &nav_font_20_400, 272));
    const int countdown_y = 131 + 26 * lines + 10;
    const int done_y = countdown_y + 18 + 12;
    lv_obj_t* card = box(screen_, margin, top + margin, column, done_y + tile + 16, ok_bg, 8, true);
    ring(card, 2, ok);
    lv_obj_t* badge = box(card, (column - 64) / 2, 22, 64, 64, ok, 32);
    icon(badge, &nav_icon_flag_38, base, 13, 13);
    const char* title = loc::tr(loc::Str::Arrived);
    design::text(card, title, &nav_font_24_800, ok,
                 (column - text_width(title, &nav_font_24_800)) / 2, 96);
    lv_obj_t* body = clipped_text(card, where.c_str(), &nav_font_20_400, text1, 16, 132, 272,
                                  lines, 26);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    arrival_countdown_ = design::text(card, "", &nav_font_14_400, text2, 16, countdown_y);
    lv_obj_set_width(arrival_countdown_, 272);
    lv_obj_set_style_text_align(arrival_countdown_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* done = box(card, 16, done_y, 272, tile, accent, 8, true);
    design::text(done, loc::tr(loc::Str::Done), &nav_font_20_700, white,
                 (272 - text_width(loc::tr(loc::Str::Done), &nav_font_20_700)) / 2, 20);
    lv_obj_add_event_cb(done, end_cb, LV_EVENT_CLICKED, this);
    arrival_started_ = lv_tick_get();
    arrival_seconds_shown_ = -1;
    update_arrival();
}

void NavigationView::update_arrival()
{
    constexpr int seconds = 8;
    if (!arrival_countdown_)
        return;
    const int left = seconds - static_cast<int>(lv_tick_elaps(arrival_started_) / 1000u);
    if (left <= 0) {
        trace("ui=arrival_auto_end");
        if (model_.end_guidance())
            rebuild();
        return;
    }
    if (left == arrival_seconds_shown_)
        return;
    arrival_seconds_shown_ = left;
    set_label_text(arrival_countdown_, loc::arrival_countdown_text(left).c_str());
}

bool NavigationView::show_place_card(Destination place, std::string icon)
{
    if (!model_.show_place_card(std::move(place), std::move(icon)))
        return false;
    rebuild();
    return true;
}

bool NavigationView::close_place_card()
{
    if (!model_.close_place_card())
        return false;
    rebuild();
    return true;
}

void NavigationView::place_close_cb(lv_event_t* event)
{
    static_cast<NavigationView*>(lv_event_get_user_data(event))->close_place_card();
}

void NavigationView::place_navigate_cb(lv_event_t* event)
{
    auto* self = static_cast<NavigationView*>(lv_event_get_user_data(event));
    const Destination* place = self->model_.place_card();
    if (!place)
        return;
    trace("ui=place_navigate");
    (void)self->select_place(*place);
}

void NavigationView::preview_back_cb(lv_event_t* event)
{
    auto* self = static_cast<NavigationView*>(lv_event_get_user_data(event));
    if (self->model_.back_to_search())
        self->rebuild();
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

    // Back (design G3) returns to the search results.
    const int back_width = 12 + 26 + 6 + text_width(loc::tr(loc::Str::Back), &nav_font_20_600) + 18;
    lv_obj_t* back = box(panel_rail, margin, margin, back_width, tile, control, 8, true);
    icon(back, &nav_icon_back_26, text1, 12, 19);
    design::text(back, loc::tr(loc::Str::Back), &nav_font_20_600, text1, 44, 20);
    lv_obj_add_event_cb(back, preview_back_cb, LV_EVENT_CLICKED, this);

    const Destination* destination = model_.destination();
    const char* title = destination ? destination->title.c_str() : loc::tr(loc::Str::DestinationFallback);
    const int title_y = 24 + tile + 8;
    const int title_lines = std::min(2, wrapped_lines(title, &nav_font_24_700, 296));
    lv_obj_t* title_label = clipped_text(panel_rail, title, &nav_font_24_700, text1,
                                         20, title_y, 296, title_lines, 30);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_MODE_DOTS);
    const int subtitle_y = title_y + 30 * title_lines + 4;
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
        const std::string reason = loc::route_reason(route.reason);
        if (!reason.empty()) {
            const int width = text_width(reason.c_str(), &nav_font_14_600) + 20;
            lv_obj_t* tag = box(card, column - 16 - width, 29, width, 28, base, 6);
            design::text(tag, reason.c_str(), &nav_font_14_600, text2, 10, 5);
        }
        lv_obj_add_event_cb(card, route_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(card, reinterpret_cast<void*>(index + 1));
    }

    lv_obj_t* start = box(panel_rail, margin, height - margin - tile, column, tile,
                          maneuver, 8, true);
    const int label_width = text_width(loc::tr(loc::Str::Start), &nav_font_20_700);
    const int x = (column - (24 + 10 + label_width)) / 2;
    icon(start, &nav_icon_route_24, white, x, 20);
    design::text(start, loc::tr(loc::Str::Start), &nav_font_20_700, white, x + 34, 20);
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

    if (guidance.position_available && guidance.arrived) {
        build_arrival_card();
        build_map_controls(true);
        return;
    }
    {
        // Speed limit (G10): white sign with a red ring at the map edge.
        speed_sign_ = box(screen_, rail, top + margin, tile, tile, white, 32);
        ring(speed_sign_, 6, 0xe0524a);
        shadow(speed_sign_, 4, 14, 0.4f);
        speed_sign_label_ = design::text(speed_sign_, "", &nav_font_24_800, base, 0, 17);
        lv_obj_set_width(speed_sign_label_, tile);
        lv_obj_set_style_text_align(speed_sign_label_, LV_TEXT_ALIGN_CENTER, 0);
        // Current street: pill centred under the camera framing.
        street_pill_ = box(screen_, 0, top + height - 20 - 44, 44, 44, surface, 22);
        shadow(street_pill_, 2, 10, 0.35f);
        street_pill_label_ = clipped_text(street_pill_, "", &nav_font_20_600, text1, 18, 10, 284);
        lane_strip_ = box(screen_, margin, 0, column, tile, surface, 8, true);
        ring(lane_strip_, 1, line);
        lane_signature_.clear();
    }
    if (guidance.position_available && guidance.recalculating) {
        // Off route (design G8): a new route from here is on its way.
        (void)status_card(screen_, margin, top + margin, 0x141d33, accent,
                          &nav_icon_recalc_38, white, loc::tr(loc::Str::Recalculating),
                          loc::tr(loc::Str::OffRoute));
    } else if (guidance.position_available) {
        guidance_card_ = box(screen_, margin, top + margin, column, 112, maneuver, 8, true);
        card_shadow(guidance_card_);
        lv_obj_set_style_clip_corner(guidance_card_, true, 0);
        guidance_glyph_ = icon(guidance_card_, design::glyph(guidance.maneuver.kind, true),
                               white, 16, 20);
        guidance_exit_ = design::text(guidance_card_, "", &nav_font_14_800, white, 0, 0);
        lv_obj_set_width(guidance_exit_, 18);
        lv_obj_set_style_text_align(guidance_exit_, LV_TEXT_ALIGN_CENTER, 0);
        guidance_simulation_ = box(guidance_card_, 102, 14, 0, 22, warning, 4);
        lv_obj_t* tag = design::text(guidance_simulation_, loc::tr(loc::Str::SimulationTag),
                                     &nav_font_14_800, base, 8, 2);
        lv_obj_set_style_text_letter_space(tag, 1, 0);
        lv_obj_set_width(guidance_simulation_,
                         text_width(loc::tr(loc::Str::SimulationTag), &nav_font_14_800, 1) + 16);
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
        const int card_height = status_card(
            screen_, margin, top + margin, warning_bg, warning, &nav_icon_no_gps_38, base,
            loc::tr(loc::Str::NoGps), loc::tr(loc::Str::NoGpsBody));
        const int next_y = top + margin + card_height + 16;
        lv_obj_t* next = design::text(screen_, loc::tr(loc::Str::NextOnRoute), &nav_font_14_700, text3,
                                      margin, next_y);
        lv_obj_set_style_text_letter_space(next, 1, 0);
        guidance_next_glyph_ = icon(screen_, design::glyph(guidance.maneuver.kind, false),
                                    text2, margin, next_y + 26 + 12);
        guidance_instruction_ = clipped_text(screen_, "", &nav_font_20_400, text2,
                                             margin + 44, next_y + 26 + 16, column - 44, 2, 26);
    }

    // Trip card: remaining time over distance, Overview and End.
    lv_obj_t* trip = box(screen_, margin, top + height - margin - 88, column, 88, surface, 8, true);
    card_shadow(trip);
    guidance_time_ = design::text(trip, "", &nav_font_28_800, text1, 16, 15);
    guidance_remaining_ = design::text(trip, "", &nav_font_20_400, text2, 16, 48);
    // A longer label than the design's "End" gives up some padding.
    const int end_text = text_width(loc::tr(loc::Str::End), &nav_font_20_700);
    const int end_pad = end_text > 56 ? 32 : 44;
    const int end_width = end_text + end_pad;
    lv_obj_t* end = box(trip, column - 12 - end_width, 12, end_width, tile, danger_bg, 8, true);
    design::text(end, loc::tr(loc::Str::End), &nav_font_20_700, danger, end_pad / 2, 20);
    lv_obj_add_event_cb(end, end_cb, LV_EVENT_CLICKED, this);
    trip_text_limit_ = column - 12 - end_width - 8 - tile - 12 - 16;
    // Never runs under the buttons, whatever the language.
    lv_label_set_long_mode(guidance_remaining_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(guidance_remaining_, trip_text_limit_,
                    lv_font_get_line_height(&nav_font_20_400));
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
    const int lines = std::min(3, design::wrapped_lines(lv_label_get_text(guidance_action_),
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
        set_label_text(guidance_remaining_, loc::tr(loc::Str::EtaPaused));
        refresh_road_widgets();
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
    const std::string next_distance = guidance.arrived ? distance_text(0.0) :
        loc::format_in_distance(guidance.distance_to_maneuver_metres);
    set_label_text(guidance_distance_, next_distance.c_str());
    set_label_text(guidance_action_, maneuver_text(guidance.maneuver).c_str());
    if (guidance_road_) {
        const char* road = !guidance.maneuver.road.empty()
            ? guidance.maneuver.road.c_str()
            : (destination ? destination->title.c_str() : loc::tr(loc::Str::RouteFallback));
        set_label_text(guidance_road_, road);
    }
    const std::string time = duration_text(guidance.remaining_seconds);
    set_label_text(guidance_time_, time.c_str());
    if (guidance_time_) {
        // Long durations ("1 h 15 min", "1 год 15 хв") step down a font size
        // instead of running under the buttons.
        const lv_font_t* font = &nav_font_28_800;
        if (design::text_width(time.c_str(), font) > trip_text_limit_)
            font = &nav_font_24_700;
        if (design::text_width(time.c_str(), font) > trip_text_limit_)
            font = &nav_font_20_700;
        lv_obj_set_style_text_font(guidance_time_, font, 0);
        lv_obj_set_y(guidance_time_, 15 + (lv_font_get_line_height(&nav_font_28_800) -
                                           lv_font_get_line_height(font)) / 2);
    }
    set_label_text(guidance_remaining_, distance_text(guidance.remaining_metres).c_str());
    layout_guidance_card();
    refresh_road_widgets();
}

// The pill takes the free part of its row: from the left column's edge to
// the right margin (less the recenter button while it shows). Centred under
// the camera framing when the name fits there, shifted otherwise; only a
// name longer than the whole row is cut.
void NavigationView::layout_street_pill()
{
    using namespace design;
    if (!street_pill_ || !street_pill_label_)
        return;
    const MapArea area = map_area();
    const int left = area.x;
    int right = area.x + area.width - margin;
    if (control_recenter_ && !lv_obj_is_hidden(control_recenter_))
        right -= tile + margin;
    const int text = text_width(lv_label_get_text(street_pill_label_), &nav_font_20_600);
    const int width = std::clamp(text + 36, 44, std::max(44, right - left));
    const int centre = area.x + area.width / 2;
    const int x = std::clamp(centre - width / 2, left, std::max(left, right - width));
    lv_obj_set_size(street_pill_label_, width - 36, 24);
    lv_obj_set_width(street_pill_, width);
    lv_obj_set_x(street_pill_, x);
}

void NavigationView::refresh_road_widgets()
{
    using namespace design;
    const auto& guidance = model_.guidance();
    const bool live = guidance.position_available && !guidance.arrived;
    if (speed_sign_) {
        const bool shown = live && guidance.speed_limit_kmh > 0;
        set_hidden(speed_sign_, !shown);
        if (shown)
            set_label_text(speed_sign_label_, std::to_string(guidance.speed_limit_kmh).c_str());
    }
    if (street_pill_) {
        const bool shown = live && !guidance.road.empty();
        set_hidden(street_pill_, !shown);
        if (shown) {
            set_label_text(street_pill_label_, guidance.road.c_str());
            layout_street_pill();
        }
    }
    if (!lane_strip_)
        return;
    // Lanes for the coming turn, inside the countdown distance.
    const auto& lanes = guidance.maneuver.lanes;
    const bool shown = live && guidance_card_ && !lanes.empty() &&
        guidance.distance_to_maneuver_metres <= countdown_metres;
    set_hidden(lane_strip_, !shown);
    if (!shown)
        return;
    // Style values: valid before LVGL's layout pass.
    lv_obj_set_y(lane_strip_, lv_obj_get_style_y(guidance_card_, LV_PART_MAIN) +
                                  lv_obj_get_style_height(guidance_card_, LV_PART_MAIN) + 8);
    std::string signature;
    for (const auto& lane : lanes) {
        signature += static_cast<char>('a' + static_cast<int>(lane.kind));
        signature += lane.combined ? static_cast<char>('a' + static_cast<int>(lane.also)) : '-';
        signature += lane.recommended ? '1' : '0';
    }
    if (signature == lane_signature_)
        return;
    lane_signature_ = signature;
    lv_obj_clean(lane_strip_);
    const int count = static_cast<int>(lanes.size());
    const float width = (column - 12.0f) / count - 4.0f;
    static const lv_point_precise_t dash[2] = {{1, 0}, {1, 52}};
    for (int index = 0; index < count; ++index) {
        const Lane& lane = lanes[static_cast<std::size_t>(index)];
        const int x = static_cast<int>(std::lround(6.0f + index * (width + 4.0f) + 2.0f));
        const int w = static_cast<int>(std::lround(width));
        lv_obj_t* cell = box(lane_strip_, x, 6, w, 52, maneuver, 6);
        if (!lane.recommended)
            lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        // CSS border-left: 2px dashed line, inside the cell.
        lv_obj_t* divider = lv_line_create(cell);
        lv_obj_remove_style_all(divider);
        lv_line_set_points(divider, dash, 2);
        lv_obj_set_style_line_width(divider, 2, 0);
        lv_obj_set_style_line_color(divider, lv_color_hex(line), 0);
        lv_obj_set_style_line_dash_width(divider, 6, 0);
        lv_obj_set_style_line_dash_gap(divider, 6, 0);
        icon(cell, lane_icon(lane), lane.recommended ? white : text3,
             2 + (w - 2 - 40) / 2, 6);
    }
}

void NavigationView::clear_object_references() noexcept
{
    results_panel_ = nullptr;
    query_label_ = nullptr;
    query_cursor_ = nullptr;
    query_placeholder_ = nullptr;
    keyboard_ = nullptr;
    arrival_countdown_ = nullptr;
    lane_strip_ = nullptr;
    lane_signature_.clear();
    speed_sign_ = nullptr;
    speed_sign_label_ = nullptr;
    street_pill_ = nullptr;
    street_pill_label_ = nullptr;
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
    dev_values_.clear();
    dev_built_ = false;
    pin_marker_ = nullptr;
    pin_visible_ = false;
    map_drag_ = false;
    long_pressed_ = false;
    last_tap_valid_ = false;
}

void NavigationView::pointer(int action, int32_t x, int32_t y, int contact)
{
    if (rendered_frames_ < input_quarantine_frames) {
        trace_pointer("quarantined", static_cast<uint32_t>(action), x, y);
        return;
    }
    if (contact != 0) {
        // In order with the first finger's samples; LVGL never sees it.
        if (pointer_queue_.size() == 32)
            pointer_queue_.pop_front();
        pointer_queue_.push_back({action, x, y, contact});
        return;
    }
    if (action == OPEN_NAVI_BACKEND_POINTER_PRESS) {
        input_sequence_active_ = true;
    } else if (!input_sequence_active_) {
        trace_pointer("orphaned", static_cast<uint32_t>(action), x, y);
        return;
    }
    if (pointer_queue_.size() == 32)
        pointer_queue_.pop_front();
    pointer_queue_.push_back({action, x, y});
    trace_pointer("queued", static_cast<uint32_t>(action), x, y);
    if (action == OPEN_NAVI_BACKEND_POINTER_RELEASE)
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
    check_long_press();
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
    update_arrival();
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
        model_.guidance().position_available != guidance.position_available ||
        model_.guidance().arrived != guidance.arrived ||
        model_.guidance().recalculating != guidance.recalculating;
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
    // Second-finger samples ahead of the next first-finger one.
    while (!self->pointer_queue_.empty() && self->pointer_queue_.front().contact != 0) {
        const PointerSample sample = self->pointer_queue_.front();
        self->pointer_queue_.pop_front();
        self->handle_map_gesture(sample);
    }
    if (!self->pointer_queue_.empty()) {
        self->last_pointer_ = self->pointer_queue_.front();
        self->pointer_queue_.pop_front();
        self->handle_map_gesture(self->last_pointer_);
    }
    if (self->last_pointer_.action == OPEN_NAVI_BACKEND_POINTER_PRESS)
        self->pointer_down_ = true;
    if (self->last_pointer_.action == OPEN_NAVI_BACKEND_POINTER_RELEASE)
        self->pointer_down_ = false;
    data->point.x = self->last_pointer_.x;
    data->point.y = self->last_pointer_.y;
    data->state = self->pointer_down_ ? LV_INDEV_STATE_PRESSED
                                      : LV_INDEV_STATE_RELEASED;
    data->continue_reading = !self->pointer_queue_.empty();
}

// Bare-map gestures: a press that moves past the slop is a drag (absolute
// positions to the model); a short press that stays inside it is a tap, and
// a second tap close to the first soon after is a double tap (the first tap
// still acts at once, so cards do not wait or flicker). A press held inside
// the slop for long_press_ms is a long press (check_long_press); the drag
// and the tap of that press are then dropped.
void NavigationView::handle_map_gesture(const PointerSample& sample)
{
    constexpr int32_t slop_px = 10;
    constexpr uint32_t tap_ms = 500;
    constexpr uint32_t double_tap_ms = 350;  // first release to second release
    constexpr int32_t double_tap_px = 30;
    if (sample.contact != 0) {
        handle_second_contact(sample);
        return;
    }
    if ((pinching_ || gesture_consumed_) &&
        sample.action != OPEN_NAVI_BACKEND_POINTER_PRESS) {
        // The first finger of a pinch: moves the pinch; no drag or tap.
        drag_last_x_ = sample.x;
        drag_last_y_ = sample.y;
        if (pinching_ && sample.action == OPEN_NAVI_BACKEND_POINTER_MOVE)
            model_.move_pinch(sample.x, sample.y, second_x_, second_y_);
        if (sample.action == OPEN_NAVI_BACKEND_POINTER_RELEASE) {
            if (pinching_)
                model_.end_pinch();
            pinching_ = false;
            gesture_consumed_ = false;
            map_drag_ = false;
            map_dragging_ = false;
        }
        return;
    }
    if (sample.action == OPEN_NAVI_BACKEND_POINTER_PRESS) {
        map_drag_ = false;
        map_dragging_ = false;
        long_pressed_ = false;
        pinching_ = false;
        gesture_consumed_ = false;
        if (built_screen_ == Screen::Search || !screen_)
            return;
        lv_point_t point{sample.x, sample.y};
        lv_obj_t* hit = lv_indev_search_obj(screen_, &point);
        // Only presses on bare map (the transparent screen or the
        // non-clickable vehicle and pin markers) pan the map.
        bool on_pin = false;
        for (lv_obj_t* object = hit; object && pin_marker_ && !on_pin;
             object = lv_obj_get_parent(object))
            on_pin = object == pin_marker_;
        if (hit == screen_ || hit == nullptr || hit == vehicle_marker_ || on_pin) {
            map_drag_ = true;
            press_x_ = drag_last_x_ = sample.x;
            press_y_ = drag_last_y_ = sample.y;
            press_tick_ = lv_tick_get();
        }
        return;
    }
    if (!map_drag_)
        return;
    if (long_pressed_) {
        if (sample.action == OPEN_NAVI_BACKEND_POINTER_RELEASE) {
            map_drag_ = false;
            long_pressed_ = false;
            last_tap_valid_ = false;
        }
        return;
    }
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
    if (sample.action == OPEN_NAVI_BACKEND_POINTER_RELEASE) {
        if (map_dragging_) {
            model_.end_drag();
            last_tap_valid_ = false;
        } else if (lv_tick_elaps(press_tick_) <= tap_ms) {
            const bool second = last_tap_valid_ &&
                lv_tick_elaps(last_tap_tick_) <= double_tap_ms &&
                std::abs(press_x_ - last_tap_x_) <= double_tap_px &&
                std::abs(press_y_ - last_tap_y_) <= double_tap_px;
            if (second) {
                model_.push_map_touch(MapGesture::DoubleTap, last_tap_x_, last_tap_y_);
                last_tap_valid_ = false;
            } else {
                model_.push_map_touch(MapGesture::Tap, press_x_, press_y_);
                last_tap_valid_ = true;
                last_tap_tick_ = lv_tick_get();
                last_tap_x_ = press_x_;
                last_tap_y_ = press_y_;
            }
        }
        map_drag_ = false;
        map_dragging_ = false;
    }
}

// A second finger on the map while the first one is down there starts a
// pinch (from a drag too); lifting either finger ends it, and the other
// one then does nothing until it lifts.
void NavigationView::handle_second_contact(const PointerSample& sample)
{
    if (sample.action == OPEN_NAVI_BACKEND_POINTER_PRESS) {
        if (!map_drag_ || long_pressed_ || pinching_ || gesture_consumed_)
            return;
        if (map_dragging_)
            model_.end_drag();
        map_dragging_ = false;
        pinching_ = true;
        last_tap_valid_ = false;
        second_x_ = sample.x;
        second_y_ = sample.y;
        model_.begin_pinch(drag_last_x_, drag_last_y_, sample.x, sample.y);
        return;
    }
    if (!pinching_)
        return;
    second_x_ = sample.x;
    second_y_ = sample.y;
    if (sample.action == OPEN_NAVI_BACKEND_POINTER_RELEASE) {
        model_.end_pinch();
        pinching_ = false;
        gesture_consumed_ = true;
        return;
    }
    model_.move_pinch(drag_last_x_, drag_last_y_, sample.x, sample.y);
}

void NavigationView::check_long_press()
{
    constexpr uint32_t long_press_ms = 600;
    if (!map_drag_ || map_dragging_ || long_pressed_ || pinching_ || gesture_consumed_ ||
        !pointer_down_ ||
        lv_tick_elaps(press_tick_) < long_press_ms)
        return;
    long_pressed_ = true;
    last_tap_valid_ = false;
    model_.push_map_touch(MapGesture::LongPress, press_x_, press_y_);
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
void NavigationView::key_cb(lv_event_t* event)
{
    auto* self = static_cast<NavigationView*>(lv_event_get_user_data(event));
    const auto* key = static_cast<const char*>(lv_obj_get_user_data(
        static_cast<lv_obj_t*>(lv_event_get_current_target(event))));
    if (key)
        self->press_key(key);
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
