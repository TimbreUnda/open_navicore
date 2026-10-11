#include "navigation_view.h"

#include "../diagnostics.h"

#include <lvgl.h>
#include <open_navicore/backend.h>

#include <algorithm>
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
    lv_display_set_buffers(display_, pixels_.data(), nullptr,
                           pixels_.size() * sizeof(uint32_t),
                           LV_DISPLAY_RENDER_MODE_FULL);
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
    results_panel_ = nullptr;
    query_field_ = nullptr;
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
    screen_ = lv_obj_create(nullptr);
    panel(screen_, base);
    if (model_.screen() != Screen::Search)
        lv_obj_set_style_bg_opa(screen_, LV_OPA_TRANSP, 0);
    lv_obj_set_size(screen_, LV_PCT(100), LV_PCT(100));
    switch (model_.screen()) {
    case Screen::FreeDrive: build_free_drive(); break;
    case Screen::Search: build_search(); break;
    case Screen::RoutePreview: build_route_preview(); break;
    case Screen::Guidance: build_guidance(); break;
    }
    lv_screen_load(screen_);
    if (old_screen)
        lv_obj_delete_async(old_screen);
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
    label(rail, "No recent destinations", secondary, 20);
    lv_obj_t* spacer = lv_obj_create(rail);
    panel(spacer, base);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_width(spacer, 1);
    label(rail, "Offline maps", tertiary, 14);
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

    if (width_ > 600) {
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
}

void NavigationView::build_guidance()
{
    build_route_preview();
    lv_obj_t* banner = lv_obj_create(screen_);
    panel(banner, maneuver, 8);
    lv_obj_set_pos(banner, 16, content_top(height_) + 16);
    lv_obj_set_size(banner, std::min<int>(304, width_), 112);
    lv_obj_t* text = label(banner, "Route started", primary, 24);
    lv_obj_center(text);
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
    std::fill(pixels_.begin(), pixels_.end(), 0u);
    lv_tick_inc(elapsed_ms);
    lv_obj_invalidate(screen_);
    (void)lv_timer_handler();
    lv_refr_now(display_);
    if (rendered_frames_ != UINT32_MAX)
        ++rendered_frames_;
    return dirty_;
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

void NavigationView::flush_cb(lv_display_t* display, const lv_area_t*,
                              unsigned char*)
{
    auto* self = static_cast<NavigationView*>(lv_display_get_user_data(display));
    self->dirty_ = true;
    lv_display_flush_ready(display);
}

void NavigationView::pointer_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    auto* self = static_cast<NavigationView*>(lv_indev_get_user_data(indev));
    if (!self->pointer_queue_.empty()) {
        self->last_pointer_ = self->pointer_queue_.front();
        self->pointer_queue_.pop_front();
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
void NavigationView::start_cb(lv_event_t* event)
{
    auto* self = static_cast<NavigationView*>(lv_event_get_user_data(event));
    if (self->model_.start_guidance()) self->rebuild();
}

} // namespace navicore_osmand::ui
