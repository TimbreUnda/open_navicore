#pragma once

#include <cstdint>
#include <vector>

namespace navicore_osmand::ui {

// Tracks which parts of a little-endian LVGL ARGB8888 canvas contain any
// non-transparent pixel, so the compositor blends only those bands instead of
// the whole surface. Rows are rescanned only when LVGL reports them dirty.
class OverlayOccupancy {
public:
    struct Rect {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    explicit OverlayOccupancy(uint32_t band_height = 32u)
        : band_height_(band_height ? band_height : 1u) {}

    // Rescans rows [y, y + height). A size change rescans the whole canvas.
    void update(const uint32_t* pixels, uint32_t width, uint32_t height,
                uint32_t dirty_y, uint32_t dirty_height);
    const std::vector<Rect>& regions() const noexcept { return regions_; }
    // Increments whenever regions() changes; zero means never computed.
    uint64_t revision() const noexcept { return revision_; }

private:
    struct Span { uint32_t begin; uint32_t end; };
    void rebuild_regions();

    uint32_t band_height_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::vector<Span> rows_;
    std::vector<Rect> regions_;
    uint64_t revision_ = 0;
};

} // namespace navicore_osmand::ui
