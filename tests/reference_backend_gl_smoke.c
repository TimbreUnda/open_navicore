#include <open_navicore/backend_plugin.h>

#include <assert.h>
#include <GLES2/gl2.h>
#include <stdint.h>
#include <string.h>

static GLenum g_error;
static unsigned int g_stale_errors;
static unsigned int g_sequence;
static unsigned int g_bind_at;
static unsigned int g_disable_at;
static unsigned int g_mask_at;
static unsigned int g_viewport_at;
static unsigned int g_color_at;
static unsigned int g_clear_at;
static unsigned int g_finish_at;
static int g_inject_clear_error;

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
    g_bind_at = ++g_sequence;
}

void glDisable(GLenum capability)
{
    assert(capability == GL_SCISSOR_TEST);
    g_disable_at = ++g_sequence;
}

void glColorMask(GLboolean red, GLboolean green,
                 GLboolean blue, GLboolean alpha)
{
    assert(red == GL_TRUE && green == GL_TRUE &&
           blue == GL_TRUE && alpha == GL_TRUE);
    g_mask_at = ++g_sequence;
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    assert(x == 0 && y == 0 && width == 800 && height == 480);
    g_viewport_at = ++g_sequence;
}

void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    assert(red == 0.0f && green == 0.85f && blue == 1.0f && alpha == 1.0f);
    g_color_at = ++g_sequence;
}

void glClear(GLbitfield mask)
{
    assert(mask == GL_COLOR_BUFFER_BIT);
    g_clear_at = ++g_sequence;
    if (g_inject_clear_error != 0) g_error = GL_INVALID_OPERATION;
}

void glFinish(void)
{
    g_finish_at = ++g_sequence;
}

static void reset(void)
{
    g_error = GL_NO_ERROR;
    g_stale_errors = 0u;
    g_sequence = 0u;
    g_bind_at = g_disable_at = g_mask_at = g_viewport_at = 0u;
    g_color_at = g_clear_at = g_finish_at = 0u;
    g_inject_clear_error = 0;
}

static OpenNaviBackendV1 query_backend(void)
{
    OpenNaviBackendPluginV1 plugin;
    memset(&plugin, 0, sizeof(plugin));
    assert(open_navi_backend_query_v1(OPEN_NAVI_PLUGIN_QUERY_ABI_V1,
                                     sizeof(plugin), &plugin) == 0);
    return plugin.backend.v1;
}

int main(void)
{
    OpenNaviBackendV1 backend = query_backend();
    OpenNaviBackendRenderTargetV1 target;
    OpenNaviBackendFrameV1 frame;
    memset(&target, 0, sizeof(target));
    memset(&frame, 0, sizeof(frame));
    target.width = 800u;
    target.height = 480u;

    reset();
    g_stale_errors = 3u;
    assert(backend.render_frame(backend.backend_context, &target, &frame) == 0);
    assert(g_stale_errors == 0u);
    assert(g_bind_at == 1u && g_disable_at == 2u && g_mask_at == 3u &&
           g_viewport_at == 4u && g_color_at == 5u && g_clear_at == 6u &&
           g_finish_at == 7u);

    reset();
    g_inject_clear_error = 1;
    assert(backend.render_frame(backend.backend_context, &target, &frame) != 0);
    assert(g_finish_at == 7u);

    reset();
    target.width = 0u;
    assert(backend.render_frame(backend.backend_context, &target, &frame) != 0);
    assert(g_sequence == 0u);
    return 0;
}
