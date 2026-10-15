#include "open_navi_default_backend.h"

#if !defined(OPEN_NAVI_RENDERER_STUB)
#include <GLES2/gl2.h>
#include <limits.h>
#endif

static int default_start(void *context,
                         const OpenNaviBackendServicesV1 *services)
{
    (void)context;
    (void)services;
    return 0;
}

static int default_surface(void *context,
                           const OpenNaviBackendRenderTargetV1 *target)
{
    (void)context;
    return target != NULL && target->width != 0u && target->height != 0u
               ? 0 : -1;
}

static int default_visibility(void *context,
                              const OpenNaviBackendVisibilityV1 *event)
{
    (void)context;
    (void)event;
    return 0;
}

static int default_screen(void *context, const OpenNaviBackendScreenV1 *event)
{
    (void)context;
    (void)event;
    return 0;
}

static int default_gps(void *context, const OpenNaviBackendGpsV1 *event)
{
    (void)context;
    (void)event;
    return 0;
}

static int default_pointer(void *context,
                           const OpenNaviBackendPointerV1 *event)
{
    (void)context;
    (void)event;
    return 0;
}

static int default_render(void *context,
                          const OpenNaviBackendRenderTargetV1 *target,
                          const OpenNaviBackendFrameV1 *frame)
{
    (void)context;
    (void)target;
    (void)frame;
#if !defined(OPEN_NAVI_RENDERER_STUB)
    unsigned int drain;
    if (target == NULL || target->width == 0u || target->height == 0u ||
        target->width > (uint32_t)INT_MAX ||
        target->height > (uint32_t)INT_MAX)
        return -1;
    /* A failed third-party renderer owns the same context and is not required
     * to restore GL state. Drain its sticky errors and establish the minimum
     * state required for an unmistakable full default-surface clear. */
    for (drain = 0u; drain < 16u && glGetError() != GL_NO_ERROR; ++drain) {}
    glBindFramebuffer(GL_FRAMEBUFFER, 0u);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, (GLsizei)target->width, (GLsizei)target->height);
    /* Bright magenta cannot be confused with stock map or dark UI chrome. */
    glClearColor(1.0f, 0.0f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    return glGetError() == GL_NO_ERROR ? 0 : -1;
#else
    return 0;
#endif
}

static const OpenNaviBackendV1 g_default_backend = {
    OPEN_NAVI_BACKEND_ABI_V1,
    sizeof(OpenNaviBackendV1),
    NULL,
    default_start,
    NULL,
    default_surface,
    NULL,
    default_visibility,
    default_screen,
    default_gps,
    default_pointer,
    default_render
};

const OpenNaviBackendV1 *open_navi_default_backend_v1(void)
{
    return &g_default_backend;
}
