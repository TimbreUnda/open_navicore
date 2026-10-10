#include "mock_default_backend.h"

#include <assert.h>
#include <GLES2/gl2.h>
#include <stdint.h>
#include <string.h>

static unsigned int g_stale_errors;
static GLenum g_error;
static unsigned int g_bind_calls;
static unsigned int g_disable_calls;
static unsigned int g_mask_calls;
static unsigned int g_viewport_calls;
static unsigned int g_clear_color_calls;
static unsigned int g_clear_calls;
static unsigned int g_finish_calls;
static int g_inject_bind_error;

GLenum glGetError(void)
{
    GLenum result;
    if (g_stale_errors != 0u) {
        --g_stale_errors;
        return GL_INVALID_OPERATION;
    }
    result = g_error;
    g_error = GL_NO_ERROR;
    return result;
}

void glBindFramebuffer(GLenum target, GLuint framebuffer)
{
    assert(target == GL_FRAMEBUFFER && framebuffer == 0u);
    ++g_bind_calls;
    if (g_inject_bind_error != 0) g_error = GL_INVALID_OPERATION;
}

void glDisable(GLenum capability)
{
    assert(capability == GL_SCISSOR_TEST);
    ++g_disable_calls;
}

void glColorMask(GLboolean red, GLboolean green,
                 GLboolean blue, GLboolean alpha)
{
    assert(red == GL_TRUE && green == GL_TRUE &&
           blue == GL_TRUE && alpha == GL_TRUE);
    ++g_mask_calls;
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    assert(x == 0 && y == 0 && width == 800 && height == 480);
    ++g_viewport_calls;
}

void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    assert(red == 1.0f && green == 0.0f && blue == 0.75f && alpha == 1.0f);
    ++g_clear_color_calls;
}

void glClear(GLbitfield mask)
{
    assert(mask == GL_COLOR_BUFFER_BIT);
    ++g_clear_calls;
}

void glFinish(void)
{
    ++g_finish_calls;
}

static void reset(void)
{
    g_stale_errors = 0u;
    g_error = GL_NO_ERROR;
    g_bind_calls = 0u;
    g_disable_calls = 0u;
    g_mask_calls = 0u;
    g_viewport_calls = 0u;
    g_clear_color_calls = 0u;
    g_clear_calls = 0u;
    g_finish_calls = 0u;
    g_inject_bind_error = 0;
}

int main(void)
{
    const MockNavBackendV1 *backend = mock_default_backend_v1();
    MockNavBackendRenderTargetV1 target;
    MockNavBackendFrameV1 frame;
    memset(&target, 0, sizeof(target));
    memset(&frame, 0, sizeof(frame));
    target.width = 800u;
    target.height = 480u;

    reset();
    g_stale_errors = 3u;
    assert(backend->render_frame(backend->backend_context,
                                 &target, &frame) == 0);
    assert(g_stale_errors == 0u);
    assert(g_bind_calls == 1u && g_disable_calls == 1u &&
           g_mask_calls == 1u && g_viewport_calls == 1u &&
           g_clear_color_calls == 1u && g_clear_calls == 1u &&
           g_finish_calls == 1u);

    reset();
    g_inject_bind_error = 1;
    assert(backend->render_frame(backend->backend_context,
                                 &target, &frame) != 0);

    reset();
    target.width = 0u;
    assert(backend->render_frame(backend->backend_context,
                                 &target, &frame) != 0);
    assert(g_bind_calls == 0u);
    return 0;
}
