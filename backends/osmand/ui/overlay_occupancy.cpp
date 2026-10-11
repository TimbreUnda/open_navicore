#include "overlay_occupancy.h"

#include <algorithm>

namespace navicore_osmand::ui {

void OverlayOccupancy::update(const uint32_t* pixels, uint32_t width,
                              uint32_t height, uint32_t dirty_y,
                              uint32_t dirty_height)
{
    if (!pixels || width == 0u || height == 0u)
        return;
    bool changed = false;
    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        rows_.assign(height, Span{width, 0u});
        dirty_y = 0u;
        dirty_height = height;
        changed = true;
    }
    if (dirty_y >= height_)
        dirty_height = 0u;
    const uint32_t end_row = dirty_y + std::min(dirty_height, height_ - dirty_y);
    for (uint32_t y = dirty_y; y < end_row; ++y) {
        const uint32_t* row = pixels + static_cast<std::size_t>(y) * width_;
        Span span{width_, 0u};
        for (uint32_t x = 0; x < width_; ++x) {
            if ((row[x] >> 24) != 0u) {
                span.begin = x;
                break;
            }
        }
        if (span.begin < width_) {
            for (uint32_t x = width_; x > span.begin; --x) {
                if ((row[x - 1u] >> 24) != 0u) {
                    span.end = x;
                    break;
                }
            }
        }
        if (span.begin != rows_[y].begin || span.end != rows_[y].end) {
            rows_[y] = span;
            changed = true;
        }
    }
    if (changed)
        rebuild_regions();
}

void OverlayOccupancy::rebuild_regions()
{
    regions_.clear();
    for (uint32_t top = 0; top < height_; top += band_height_) {
        const uint32_t bottom = std::min(height_, top + band_height_);
        uint32_t begin = width_;
        uint32_t end = 0u;
        for (uint32_t y = top; y < bottom; ++y) {
            if (rows_[y].begin >= rows_[y].end)
                continue;
            begin = std::min(begin, rows_[y].begin);
            end = std::max(end, rows_[y].end);
        }
        if (begin >= end)
            continue;
        // Merge vertically adjacent bands with an identical span into one quad.
        if (!regions_.empty()) {
            Rect& previous = regions_.back();
            if (previous.y + previous.height == top && previous.x == begin &&
                previous.width == end - begin) {
                previous.height += bottom - top;
                continue;
            }
        }
        regions_.push_back({begin, top, end - begin, bottom - top});
    }
    ++revision_;
}

} // namespace navicore_osmand::ui
