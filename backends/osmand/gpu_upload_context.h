#pragma once

#include <EGL/egl.h>

namespace navicore_osmand {

// A pbuffer-backed EGL context sharing objects with the provider's render
// context, so OsmAnd's GPU worker thread can upload tile textures off the
// render thread instead of one resource per presented frame.
//
// create() and destroy() run on the render thread with the provider context
// current; make_current()/release_current() run on the GPU worker thread.
class GpuUploadContext {
public:
    GpuUploadContext() = default;
    ~GpuUploadContext() { destroy(); }
    GpuUploadContext(const GpuUploadContext&) = delete;
    GpuUploadContext& operator=(const GpuUploadContext&) = delete;

    // Creates the shared context and proves it can be made current, then
    // restores the caller's binding. Returns false with nothing allocated.
    bool create(EGLDisplay display, EGLContext shared_with);
    bool make_current() noexcept;
    void release_current() noexcept;
    void destroy() noexcept;
    bool valid() const noexcept { return context_ != EGL_NO_CONTEXT; }
    EGLint error() const noexcept { return error_; }

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLint error_ = EGL_SUCCESS;
};

} // namespace navicore_osmand
