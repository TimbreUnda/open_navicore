/*
 * gpu_bench: what the SGX540 (GLES2) on the radio can do, per operation.
 *
 * Renders off-screen into a 1024x574 RGBA framebuffer object, so Screen
 * composition never enters the timings; each test is bracketed by
 * glFinish(). A tiny hidden window only provides the EGL context.
 * Output: one line per test with ms per iteration and derived throughput.
 *
 *   gpu_bench [--iterations N] [--width W] [--height H]
 */
#define _POSIX_C_SOURCE 200112L

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <screen/screen.h>

#include <inttypes.h>
#include <stdint.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int W = 1024, H = 574, ITER = 30;
static GLuint quad_vbo;

/* clock_gettime ticks at ~1 ms on this QNX build (first run printed 0.000
 * for most GPU tests); ClockCycles() is the CPU cycle counter. */
static double now_ms(void)
{
    static double cycles_per_ms = 0.0;
    if (cycles_per_ms == 0.0)
        cycles_per_ms = (double)SYSPAGE_ENTRY(qtime)->cycles_per_sec / 1e3;
    return (double)ClockCycles() / cycles_per_ms;
}

/* PowerVR defers FBO work nobody reads; a 1-pixel readback forces the tiles
 * to be rendered. Its own cost is measured once and subtracted. */
static double g_resolve_ms = 0.0;
static void resolve(void)
{
    uint8_t pixel[4];
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
}

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        printf("shader compile failed: %s\n", log);
        return 0;
    }
    return s;
}

static GLuint program(const char *vs, const char *fs)
{
    GLuint p = glCreateProgram(), v = compile(GL_VERTEX_SHADER, vs),
           f = compile(GL_FRAGMENT_SHADER, fs);
    GLint ok = 0;
    glAttachShader(p, v);
    glAttachShader(p, f);
    glBindAttribLocation(p, 0, "pos");
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    return ok ? p : 0;
}

static const char *VS =
    "attribute vec2 pos; varying vec2 uv;"
    "void main(){ uv = pos*0.5+0.5; gl_Position = vec4(pos,0.0,1.0); }";

static void draw_quad(void)
{
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* Marginal cost per iteration: (T(ITER runs + resolve) - T(1 run + resolve))
 * / (ITER - 1). A forced resolve is a full GPU round trip that waits behind
 * Screen's own composition on the shared SGX (~13-22 ms on the radio), so
 * per-iteration resolves would hide the work itself. */
static void timed(const char *name, void (*fn)(void *), void *arg, double pixels)
{
    double t0, one, many, ms;
    int i;
    fn(arg);           /* warm-up: shader compile, first upload */
    resolve();
    t0 = now_ms();
    fn(arg);
    resolve();
    one = now_ms() - t0;
    t0 = now_ms();
    for (i = 0; i < ITER; ++i) fn(arg);
    resolve();
    many = now_ms() - t0;
    ms = ITER > 1 ? (many - one) / (ITER - 1) : many;
    if (ms < 0) ms = 0;
    if (pixels > 0)
        printf("%-34s %8.3f ms  %8.1f Mpix/s\n", name, ms,
               ms > 0 ? pixels / ms / 1e3 : 0.0);
    else
        printf("%-34s %8.3f ms\n", name, ms);
    if (glGetError() != GL_NO_ERROR) printf("  (GL error during %s)\n", name);
}

/* ---- tests ---- */
static void t_clear(void *a) { (void)a; glClear(GL_COLOR_BUFFER_BIT); }
static void t_quad(void *a) { glUseProgram(*(GLuint *)a); draw_quad(); }
static void t_quad4(void *a)
{
    int i;
    glUseProgram(*(GLuint *)a);
    for (i = 0; i < 4; ++i) draw_quad();
}

typedef struct { GLenum format, type; int w, h; void *pixels; GLuint tex; } Upload;
static void t_teximage(void *a)
{
    Upload *u = a;
    glBindTexture(GL_TEXTURE_2D, u->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, u->format, u->w, u->h, 0, u->format, u->type,
                 u->pixels);
}
static void t_texsub(void *a)
{
    Upload *u = a;
    glBindTexture(GL_TEXTURE_2D, u->tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, u->w, u->h, u->format, u->type,
                    u->pixels);
}

typedef struct { GLuint prog; int count; GLuint vbo; float *verts; } Draws;
static void t_many_draws(void *a)
{
    Draws *d = a;
    int i;
    glUseProgram(d->prog);
    glBindBuffer(GL_ARRAY_BUFFER, d->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    for (i = 0; i < d->count; ++i) glDrawArrays(GL_TRIANGLES, i * 6, 6);
}
static void t_one_draw(void *a)
{
    Draws *d = a;
    glUseProgram(d->prog);
    glBindBuffer(GL_ARRAY_BUFFER, d->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, d->count * 6);
}
static void t_stream_data(void *a)
{
    Draws *d = a;
    glBindBuffer(GL_ARRAY_BUFFER, d->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)d->count * 6 * 2 * sizeof(float),
                 d->verts, GL_STREAM_DRAW);
    t_one_draw(a);
}
static void t_stream_sub(void *a)
{
    Draws *d = a;
    glBindBuffer(GL_ARRAY_BUFFER, d->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)d->count * 6 * 2 * sizeof(float),
                    d->verts);
    t_one_draw(a);
}
static void t_client_arrays(void *a)
{
    Draws *d = a;
    glUseProgram(d->prog);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, d->verts);
    glDrawArrays(GL_TRIANGLES, 0, d->count * 6);
}
static void t_readpixels(void *a)
{
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, a);
}

typedef struct { GLuint prog, tex; } Textured;
static void t_textured(void *a)
{
    Textured *t = a;
    glUseProgram(t->prog);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    draw_quad();
}

static GLuint make_texture(GLenum format, GLenum type, int w, int h, GLenum filter,
                           int mip)
{
    GLuint tex;
    size_t bytes = (size_t)w * h * (type == GL_UNSIGNED_BYTE ? 4 : 2);
    void *pixels = malloc(bytes);
    memset(pixels, 0x7f, bytes);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, type, pixels);
    if (mip) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mip ? GL_LINEAR_MIPMAP_NEAREST : filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    free(pixels);
    return tex;
}

int main(int argc, char **argv)
{
    static const EGLint config_attr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    static const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    static const float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    screen_context_t sctx;
    screen_window_t win;
    EGLDisplay egl;
    EGLConfig config;
    EGLSurface surface;
    EGLContext ctx;
    EGLint n = 0;
    GLuint fbo, target, flat, tex_prog, alu_m, alu_h, alu_light;
    int i, usage = SCREEN_USAGE_OPENGL_ES2, size[2] = {16, 16}, z = -1000;
    double pixels;

    for (i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--iterations")) ITER = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--width")) W = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--height")) H = atoi(argv[i + 1]);
    }
    if (ITER < 1) ITER = 1;
    pixels = (double)W * H;
    if (!getenv("GRAPHICS_ROOT"))
        setenv("GRAPHICS_ROOT", "/proc/boot/usr/lib/graphics/jacinto6", 0);

    /* A 16x16 window far below everything just to own a context. */
    if (screen_create_context(&sctx, SCREEN_APPLICATION_CONTEXT) != 0 ||
        screen_create_window(&win, sctx) != 0) {
        perror("screen"); return 1;
    }
    screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, &usage);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_BUFFER_SIZE, size);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_ZORDER, &z);
    screen_create_window_buffers(win, 2);
    egl = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl == EGL_NO_DISPLAY || !eglInitialize(egl, NULL, NULL) ||
        !eglChooseConfig(egl, config_attr, &config, 1, &n) || n != 1) {
        printf("EGL init failed 0x%x\n", eglGetError()); return 1;
    }
    surface = eglCreateWindowSurface(egl, config, (EGLNativeWindowType)win, NULL);
    ctx = eglCreateContext(egl, config, EGL_NO_CONTEXT, ctx_attr);
    if (!eglMakeCurrent(egl, surface, surface, ctx)) {
        printf("eglMakeCurrent failed 0x%x\n", eglGetError()); return 1;
    }
    printf("GL_RENDERER=%s\nGL_VERSION=%s\n", glGetString(GL_RENDERER),
           glGetString(GL_VERSION));
    printf("GL_EXTENSIONS=%s\n", glGetString(GL_EXTENSIONS));
    printf("target %dx%d RGBA FBO, %d iterations per test\n\n", W, H, ITER);

    glGenFramebuffers(1, &fbo);
    target = make_texture(GL_RGBA, GL_UNSIGNED_BYTE, W, H, GL_NEAREST, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           target, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        printf("FBO incomplete\n"); return 1;
    }
    glViewport(0, 0, W, H);
    glGenBuffers(1, &quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    flat = program(VS, "precision mediump float;"
                       "void main(){ gl_FragColor = vec4(0.2,0.4,0.6,1.0); }");
    tex_prog = program(VS, "precision mediump float; uniform sampler2D t;"
                           "varying vec2 uv;"
                           "void main(){ gl_FragColor = texture2D(t, uv); }");
    alu_light = program(VS, "precision mediump float; varying vec2 uv;"
                            "void main(){ vec4 c=vec4(uv,0.5,1.0);"
                            "for(int i=0;i<4;++i) c=c*1.0001+0.0001;"
                            "gl_FragColor=c; }");
    alu_m = program(VS, "precision mediump float; varying vec2 uv;"
                        "void main(){ vec4 c=vec4(uv,0.5,1.0);"
                        "for(int i=0;i<32;++i) c=c*1.0001+0.0001;"
                        "gl_FragColor=c; }");
    alu_h = program(VS, "precision highp float; varying vec2 uv;"
                        "void main(){ vec4 c=vec4(uv,0.5,1.0);"
                        "for(int i=0;i<32;++i) c=c*1.0001+0.0001;"
                        "gl_FragColor=c; }");

    {
        /* Baseline: the readback alone, with nothing new to render. */
        double t0;
        resolve();
        t0 = now_ms();
        for (i = 0; i < 200; ++i) resolve();
        g_resolve_ms = (now_ms() - t0) / 200;
        printf("GPU round trip (1-pixel readback, nothing to draw) %.3f ms\n"
               "results below are marginal cost per iteration\n\n",
               g_resolve_ms);
    }
    puts("-- fill rate --");
    glDisable(GL_BLEND);
    timed("clear", t_clear, NULL, pixels);
    timed("flat quad, no blend", t_quad, &flat, pixels);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    timed("flat quad, alpha blend", t_quad, &flat, pixels);
    timed("4x overdraw, alpha blend", t_quad4, &flat, pixels * 4);
    glDisable(GL_BLEND);
    timed("4x overdraw, no blend", t_quad4, &flat, pixels * 4);

    puts("-- shader math (fullscreen) --");
    timed("4 mad mediump", t_quad, &alu_light, pixels);
    timed("32 mad mediump", t_quad, &alu_m, pixels);
    timed("32 mad highp", t_quad, &alu_h, pixels);

    puts("-- texture sampling (fullscreen, 1024x512 texture) --");
    {
        Textured t = {tex_prog, 0};
        t.tex = make_texture(GL_RGBA, GL_UNSIGNED_BYTE, 1024, 512, GL_NEAREST, 0);
        timed("RGBA8888 nearest", t_textured, &t, pixels);
        t.tex = make_texture(GL_RGBA, GL_UNSIGNED_BYTE, 1024, 512, GL_LINEAR, 0);
        timed("RGBA8888 linear", t_textured, &t, pixels);
        t.tex = make_texture(GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 1024, 512, GL_LINEAR, 0);
        timed("RGB565 linear", t_textured, &t, pixels);
        t.tex = make_texture(GL_RGBA, GL_UNSIGNED_BYTE, 1024, 512, GL_LINEAR, 1);
        timed("RGBA8888 linear + mipmaps", t_textured, &t, pixels);
    }

    puts("-- texture upload (256x256 tile / full canvas) --");
    {
        static uint8_t buf[1024 * 1024 * 4];
        Upload u = {GL_RGBA, GL_UNSIGNED_BYTE, 256, 256, buf, 0};
        glGenTextures(1, &u.tex);
        timed("glTexImage2D 256x256 RGBA", t_teximage, &u, 0);
        timed("glTexSubImage2D 256x256 RGBA", t_texsub, &u, 0);
        u.format = GL_RGB; u.type = GL_UNSIGNED_SHORT_5_6_5;
        timed("glTexImage2D 256x256 RGB565", t_teximage, &u, 0);
        timed("glTexSubImage2D 256x256 RGB565", t_texsub, &u, 0);
        u.format = GL_RGBA; u.type = GL_UNSIGNED_BYTE; u.w = 1024; u.h = 574;
        timed("glTexImage2D 1024x574 RGBA", t_teximage, &u, 0);
        timed("glTexSubImage2D 1024x574 RGBA", t_texsub, &u, 0);
    }

    puts("-- draw calls and vertex streaming (1000 small quads) --");
    {
        Draws d = {flat, 1000, 0, NULL};
        d.verts = malloc((size_t)d.count * 6 * 2 * sizeof(float));
        for (i = 0; i < d.count; ++i) {
            const float x = -1.0f + (i % 40) * 0.05f, y = -1.0f + (i / 40) * 0.08f;
            const float q[12] = {x, y, x + .04f, y, x, y + .06f,
                                 x, y + .06f, x + .04f, y, x + .04f, y + .06f};
            memcpy(d.verts + i * 12, q, sizeof(q));
        }
        glGenBuffers(1, &d.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, d.vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)d.count * 12 * sizeof(float),
                     d.verts, GL_STATIC_DRAW);
        timed("1000 draw calls (static VBO)", t_many_draws, &d, 0);
        timed("1 batched draw (static VBO)", t_one_draw, &d, 0);
        timed("batched + glBufferData/frame", t_stream_data, &d, 0);
        timed("batched + glBufferSubData/frame", t_stream_sub, &d, 0);
        timed("batched client-side arrays", t_client_arrays, &d, 0);
        free(d.verts);
    }

    puts("-- readback --");
    {
        void *buf = malloc((size_t)W * H * 4);
        timed("glReadPixels full RGBA", t_readpixels, buf, pixels);
        free(buf);
    }
    eglMakeCurrent(egl, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl, surface);
    eglDestroyContext(egl, ctx);
    screen_destroy_window(win);
    screen_destroy_context(sctx);
    return 0;
}
