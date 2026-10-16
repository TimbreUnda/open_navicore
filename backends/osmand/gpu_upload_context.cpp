#include "gpu_upload_context.h"

namespace navicore_osmand {
namespace {
bool choose_pbuffer_config(EGLDisplay display, EGLContext shared_with,
                           EGLConfig& config)
{
    // Prefer the render context's own config so sharing is unambiguous.
    EGLint config_id = 0;
    EGLint count = 0;
    if (eglQueryContext(display, shared_with, EGL_CONFIG_ID, &config_id) ==
        EGL_TRUE) {
        const EGLint by_id[] = {EGL_CONFIG_ID, config_id, EGL_NONE};
        EGLint surface_type = 0;
        if (eglChooseConfig(display, by_id, &config, 1, &count) == EGL_TRUE &&
            count == 1 &&
            eglGetConfigAttrib(display, config, EGL_SURFACE_TYPE,
                               &surface_type) == EGL_TRUE &&
            (surface_type & EGL_PBUFFER_BIT) != 0)
            return true;
    }
    const EGLint any_pbuffer[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE
    };
    count = 0;
    return eglChooseConfig(display, any_pbuffer, &config, 1, &count) ==
               EGL_TRUE && count == 1;
}
} // namespace

bool GpuUploadContext::create(EGLDisplay display, EGLContext shared_with)
{
    destroy();
    error_ = EGL_SUCCESS;
    if (display == EGL_NO_DISPLAY || shared_with == EGL_NO_CONTEXT) {
        error_ = EGL_BAD_PARAMETER;
        return false;
    }
    EGLConfig config = nullptr;
    if (!choose_pbuffer_config(display, shared_with, config)) {
        error_ = eglGetError();
        return false;
    }
    static const EGLint surface_attributes[] = {
        EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE
    };
    static const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
    };
    display_ = display;
    surface_ = eglCreatePbufferSurface(display, config, surface_attributes);
    if (surface_ != EGL_NO_SURFACE)
        context_ = eglCreateContext(display, config, shared_with,
                                    context_attributes);
    if (context_ == EGL_NO_CONTEXT) {
        error_ = eglGetError();
        destroy();
        return false;
    }
    // Prove the context binds here, where a failure can still fall back to
    // render-thread uploads, then restore the provider binding.
    const EGLSurface draw = eglGetCurrentSurface(EGL_DRAW);
    const EGLSurface read = eglGetCurrentSurface(EGL_READ);
    const EGLContext current = eglGetCurrentContext();
    const bool bound = eglMakeCurrent(display, surface_, surface_, context_) ==
                       EGL_TRUE;
    if (!bound)
        error_ = eglGetError();
    if (eglMakeCurrent(display, draw, read, current) != EGL_TRUE) {
        error_ = eglGetError();
        destroy();
        return false;
    }
    if (!bound) {
        destroy();
        return false;
    }
    return true;
}

bool GpuUploadContext::make_current() noexcept
{
    return valid() &&
           eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE;
}

void GpuUploadContext::release_current() noexcept
{
    if (display_ != EGL_NO_DISPLAY)
        (void)eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                             EGL_NO_CONTEXT);
}

void GpuUploadContext::destroy() noexcept
{
    if (display_ != EGL_NO_DISPLAY) {
        if (context_ != EGL_NO_CONTEXT)
            (void)eglDestroyContext(display_, context_);
        if (surface_ != EGL_NO_SURFACE)
            (void)eglDestroySurface(display_, surface_);
    }
    display_ = EGL_NO_DISPLAY;
    surface_ = EGL_NO_SURFACE;
    context_ = EGL_NO_CONTEXT;
}

} // namespace navicore_osmand
