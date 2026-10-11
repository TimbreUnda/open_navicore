#pragma once

#include <string>

namespace navicore_osmand::ui {

// Convert map text to the Latin subset compiled into the compact LVGL font.
std::string compact_font_text(const std::string& utf8);

} // namespace navicore_osmand::ui
