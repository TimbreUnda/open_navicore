#pragma once

#include <cstdint>
#include <vector>

namespace navicore_osmand::ui {

// GLES2 compositor for a little-endian LVGL ARGB8888 software canvas. Calls
// are valid only with the provider's expected context current.
class GlOverlayCompositor {
public:
    GlOverlayCompositor() = default;
    ~GlOverlayCompositor() = default;
    GlOverlayCompositor(const GlOverlayCompositor&) = delete;
    GlOverlayCompositor& operator=(const GlOverlayCompositor&) = delete;

    bool present(const uint32_t* pixels, uint32_t width, uint32_t height,
                 uint32_t dirty_x, uint32_t dirty_y,
                 uint32_t dirty_width, uint32_t dirty_height);
    void release(bool context_lost) noexcept;

private:
    bool initialize();
    unsigned int program_ = 0;
    unsigned int texture_ = 0;
    unsigned int vertex_buffer_ = 0;
    int texture_uniform_ = -1;
    uint32_t texture_width_ = 0;
    uint32_t texture_height_ = 0;
    std::vector<uint32_t> upload_pixels_;
};

} // namespace navicore_osmand::ui
