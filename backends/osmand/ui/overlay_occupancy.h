#pragma once

#include <cstdint>
#include <vector>

namespace navicore_osmand::ui {

// Tracks which parts of a little-endian LVGL ARGB8888 canvas contain any
// non-transparent pixel, so the compositor blends only those areas instead of
// the whole surface. The canvas is divided into band_height x cell_width
// cells; only bands touched by LVGL's dirty rows are rescanned. Separate
// widgets in the same band (a left rail and a right control column) stay
// separate quads instead of one full-width span.
class OverlayOccupancy {
public:
    struct Rect {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    explicit OverlayOccupancy(uint32_t band_height = 32u,
                              uint32_t cell_width = 32u)
        : band_height_(band_height ? band_height : 1u),
          cell_width_(cell_width ? cell_width : 1u) {}

    // Rescans bands overlapping rows [y, y + height). A size change rescans
    // the whole canvas.
    void update(const uint32_t* pixels, uint32_t width, uint32_t height,
                uint32_t dirty_y, uint32_t dirty_height);
    const std::vector<Rect>& regions() const noexcept { return regions_; }
    // Increments whenever regions() changes; zero means never computed.
    uint64_t revision() const noexcept { return revision_; }

private:
    void rebuild_regions();

    uint32_t band_height_;
    uint32_t cell_width_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t columns_ = 0;
    uint32_t bands_ = 0;
    std::vector<uint8_t> cells_;   // bands_ x columns_, 1 = occupied
    std::vector<Rect> regions_;
    std::vector<std::size_t> open_; // scratch for vertical merging
    std::vector<uint8_t> scratch_;  // one band of cells being rescanned
    uint64_t revision_ = 0;
};

} // namespace navicore_osmand::ui
