#pragma once

#include "overlay_occupancy.h"

#include <cstdint>
#include <vector>

namespace navicore_osmand::ui {

// One dirty canvas rectangle to upload.
struct UploadRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// GLES2 compositor for a little-endian LVGL ARGB8888 software canvas. Calls
// are valid only with the provider's expected context current.
class GlOverlayCompositor {
public:
    GlOverlayCompositor() = default;
    ~GlOverlayCompositor() = default;
    GlOverlayCompositor(const GlOverlayCompositor&) = delete;
    GlOverlayCompositor& operator=(const GlOverlayCompositor&) = delete;

    // Blends only `occupancy.regions()`; transparent canvas areas cost no
    // fragments, and an empty canvas issues no GL calls at all.
    // Uploads each dirty rectangle separately, then blends the occupied
    // regions. An empty dirty list re-blends without uploading.
    bool present(const uint32_t* pixels, uint32_t width, uint32_t height,
                 const std::vector<UploadRect>& dirty,
                 const OverlayOccupancy& occupancy);
    void release(bool context_lost) noexcept;

private:
    bool initialize();
    void upload_rect(const uint32_t* pixels, uint32_t width, UploadRect rect);
    void upload_regions(const OverlayOccupancy& occupancy, uint32_t width,
                        uint32_t height);
    unsigned int program_ = 0;
    unsigned int texture_ = 0;
    unsigned int vertex_buffer_ = 0;
    int texture_uniform_ = -1;
    uint32_t texture_width_ = 0;
    uint32_t texture_height_ = 0;
    uint64_t regions_revision_ = 0;
    uint32_t regions_width_ = 0;
    uint32_t regions_height_ = 0;
    int region_vertices_ = 0;
    std::vector<uint32_t> upload_pixels_;
    std::vector<float> region_geometry_;
};

} // namespace navicore_osmand::ui
