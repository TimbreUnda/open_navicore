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
        columns_ = (width + cell_width_ - 1u) / cell_width_;
        bands_ = (height + band_height_ - 1u) / band_height_;
        cells_.assign(static_cast<std::size_t>(columns_) * bands_, 0u);
        dirty_y = 0u;
        dirty_height = height;
        changed = true;
    }
    if (dirty_y >= height_ || dirty_height == 0u) {
        if (changed)
            rebuild_regions();
        return;
    }
    const uint32_t end_row = dirty_y + std::min(dirty_height, height_ - dirty_y);
    const uint32_t first_band = dirty_y / band_height_;
    const uint32_t last_band = (end_row - 1u) / band_height_;
    std::vector<uint8_t>& band_cells = scratch_;
    band_cells.resize(columns_);
    for (uint32_t band = first_band; band <= last_band; ++band) {
        std::fill(band_cells.begin(), band_cells.end(), 0u);
        const uint32_t top = band * band_height_;
        const uint32_t bottom = std::min(height_, top + band_height_);
        for (uint32_t y = top; y < bottom; ++y) {
            const uint32_t* row = pixels + static_cast<std::size_t>(y) * width_;
            for (uint32_t column = 0; column < columns_; ++column) {
                if (band_cells[column])
                    continue;
                const uint32_t left = column * cell_width_;
                const uint32_t right = std::min(width_, left + cell_width_);
                for (uint32_t x = left; x < right; ++x) {
                    if ((row[x] >> 24) != 0u) {
                        band_cells[column] = 1u;
                        break;
                    }
                }
            }
        }
        uint8_t* stored = cells_.data() + static_cast<std::size_t>(band) * columns_;
        if (!std::equal(band_cells.begin(), band_cells.end(), stored)) {
            std::copy(band_cells.begin(), band_cells.end(), stored);
            changed = true;
        }
    }
    if (changed)
        rebuild_regions();
}

void OverlayOccupancy::rebuild_regions()
{
    regions_.clear();
    // Indices of rects that end at the previous band, for vertical merging.
    std::vector<std::size_t> previous;
    for (uint32_t band = 0; band < bands_; ++band) {
        const uint32_t top = band * band_height_;
        const uint32_t bottom = std::min(height_, top + band_height_);
        const uint8_t* cells = cells_.data() + static_cast<std::size_t>(band) * columns_;
        open_.clear();
        for (uint32_t column = 0; column < columns_;) {
            if (!cells[column]) {
                ++column;
                continue;
            }
            const uint32_t first = column;
            while (column < columns_ && cells[column])
                ++column;
            const uint32_t x = first * cell_width_;
            const uint32_t right = std::min(width_, column * cell_width_);
            bool merged = false;
            for (const std::size_t index : previous) {
                Rect& rect = regions_[index];
                if (rect.x == x && rect.width == right - x &&
                    rect.y + rect.height == top) {
                    rect.height += bottom - top;
                    open_.push_back(index);
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                regions_.push_back({x, top, right - x, bottom - top});
                open_.push_back(regions_.size() - 1u);
            }
        }
        previous.swap(open_);
    }
    ++revision_;
}

} // namespace navicore_osmand::ui
