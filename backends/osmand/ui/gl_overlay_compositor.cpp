#include "gl_overlay_compositor.h"

#include <GLES2/gl2.h>

namespace navicore_osmand::ui {
namespace {
GLuint compile(GLenum kind, const char* source)
{
    const GLuint shader = glCreateShader(kind);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

struct AttributeState {
    GLint enabled;
    GLint size;
    GLint type;
    GLint normalized;
    GLint stride;
    GLint buffer;
    void* pointer;
};

struct State {
    GLint program;
    GLint active_texture;
    GLint texture;
    GLint array_buffer;
    GLint viewport[4];
    GLint blend_src_rgb;
    GLint blend_dst_rgb;
    GLint blend_src_alpha;
    GLint blend_dst_alpha;
    GLboolean blend;
    GLboolean depth;
    GLboolean cull;
    GLboolean scissor;
    AttributeState attributes[2];
};

AttributeState save_attribute(GLuint index)
{
    AttributeState attribute{};
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attribute.enabled);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attribute.size);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &attribute.type);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,
                        &attribute.normalized);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &attribute.stride);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,
                        &attribute.buffer);
    glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER,
                              &attribute.pointer);
    return attribute;
}

State save_state()
{
    State state{};
    glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.texture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.array_buffer);
    glGetIntegerv(GL_VIEWPORT, state.viewport);
    glGetIntegerv(GL_BLEND_SRC_RGB, &state.blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &state.blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &state.blend_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &state.blend_dst_alpha);
    state.blend = glIsEnabled(GL_BLEND);
    state.depth = glIsEnabled(GL_DEPTH_TEST);
    state.cull = glIsEnabled(GL_CULL_FACE);
    state.scissor = glIsEnabled(GL_SCISSOR_TEST);
    state.attributes[0] = save_attribute(0);
    state.attributes[1] = save_attribute(1);
    return state;
}

void enable(GLenum capability, GLboolean value)
{
    if (value) glEnable(capability); else glDisable(capability);
}

void restore_state(const State& state)
{
    for (GLuint index = 0; index < 2; ++index) {
        const AttributeState& attribute = state.attributes[index];
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(attribute.buffer));
        glVertexAttribPointer(index, attribute.size,
                              static_cast<GLenum>(attribute.type),
                              static_cast<GLboolean>(attribute.normalized),
                              attribute.stride, attribute.pointer);
        if (attribute.enabled) glEnableVertexAttribArray(index);
        else glDisableVertexAttribArray(index);
    }
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(state.array_buffer));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.texture));
    glActiveTexture(static_cast<GLenum>(state.active_texture));
    glUseProgram(static_cast<GLuint>(state.program));
    glViewport(state.viewport[0], state.viewport[1], state.viewport[2],
               state.viewport[3]);
    glBlendFuncSeparate(static_cast<GLenum>(state.blend_src_rgb),
                        static_cast<GLenum>(state.blend_dst_rgb),
                        static_cast<GLenum>(state.blend_src_alpha),
                        static_cast<GLenum>(state.blend_dst_alpha));
    enable(GL_BLEND, state.blend);
    enable(GL_DEPTH_TEST, state.depth);
    enable(GL_CULL_FACE, state.cull);
    enable(GL_SCISSOR_TEST, state.scissor);
}

} // namespace

bool GlOverlayCompositor::initialize()
{
    static const char* vertex_source =
        "attribute vec2 position; attribute vec2 uv; varying vec2 vuv;"
        "void main(){gl_Position=vec4(position,0.0,1.0);vuv=uv;}";
    // LVGL ARGB8888 is BGRA byte order on this little-endian target. Uploading
    // as RGBA and swapping red/blue in the GLES2 shader avoids requiring the
    // unqualified GL_EXT_texture_format_BGRA8888 extension.
    static const char* fragment_source =
        "precision mediump float; uniform sampler2D image; varying vec2 vuv;"
        "void main(){vec4 p=texture2D(image,vuv);gl_FragColor=p.bgra;}";
    const GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glBindAttribLocation(program_, 0, "position");
    glBindAttribLocation(program_, 1, "uv");
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        release(false);
        return false;
    }
    texture_uniform_ = glGetUniformLocation(program_, "image");
    static const GLfloat vertices[] = {
        -1.f, -1.f, 0.f, 1.f,  1.f, -1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 0.f,  1.f,  1.f, 1.f, 0.f
    };
    glGenBuffers(1, &vertex_buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return glGetError() == GL_NO_ERROR;
}

bool GlOverlayCompositor::present(const uint32_t* pixels, uint32_t width,
                                  uint32_t height)
{
    if (!pixels || width == 0 || height == 0)
        return false;
    const State state = save_state();
    // A map renderer error belongs to its frame, not to this overlay. Drain it
    // before attributing subsequent GLES failures to the compositor.
    while (glGetError() != GL_NO_ERROR) {}
    if (!program_ && !initialize()) {
        restore_state(state);
        return false;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(width),
                 static_cast<GLsizei>(height), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels);
    glUseProgram(program_);
    glUniform1i(texture_uniform_, 0);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), nullptr);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    const bool ok = glGetError() == GL_NO_ERROR;
    restore_state(state);
    return ok;
}

void GlOverlayCompositor::release(bool context_lost) noexcept
{
    if (!context_lost) {
        if (texture_) glDeleteTextures(1, &texture_);
        if (vertex_buffer_) glDeleteBuffers(1, &vertex_buffer_);
        if (program_) glDeleteProgram(program_);
    }
    program_ = 0;
    texture_ = 0;
    vertex_buffer_ = 0;
    texture_uniform_ = -1;
}

} // namespace navicore_osmand::ui
