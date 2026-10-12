/*
 * screen_bench: isolate QNX Screen composition/presentation cost on the radio.
 *
 * Creates ONE test window with explicit Screen properties, presents frames for
 * a bounded time, and reports what Screen actually applied plus 2 ms
 * histograms of present time and present-to-present interval. It never
 * touches nv_navigation, so it does not consume System Manager relaunches.
 *
 * Modes:
 *   gl   EGL/GLES2 window, glClear per frame, eglSwapBuffers (like NAV_main).
 *        --finish adds glFinish() before the swap and reports it separately,
 *        splitting GPU time from Screen/compositor time.
 *   sw   no GL: CPU-fill native buffers and screen_post_window(). Measures
 *        Screen's composition/post cost alone. --dirty small posts a 64x64
 *        damage rectangle instead of the full window, to test whether cost
 *        scales with the damaged area.
 *
 * Examples (radio, as root):
 *   screen_bench --mode gl --seconds 10                       # NAV-like baseline
 *   screen_bench --mode gl --finish                           # GPU vs compositor
 *   screen_bench --mode sw --dirty full                       # Screen only
 *   screen_bench --mode sw --dirty small                      # damage scaling
 *   screen_bench --mode gl --overlay --pipeline 1             # video1 layer
 *   screen_bench --mode gl --transparency none --format rgbx8888
 *   screen_bench --mode gl --size 512x287                     # area scaling
 */
#define _POSIX_C_SOURCE 200112L

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <screen/screen.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BINS 32u                 /* 2 ms bins, last >= 62 ms */
#define BIN_NS 2000000ull
#define MAX_SECONDS 120

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

typedef struct {
    const char *mode;            /* gl | sw */
    const char *klass;           /* SCREEN_PROPERTY_CLASS, NULL = none */
    int pipeline;                /* 0 = leave to Screen */
    int overlay;                 /* add SCREEN_USAGE_OVERLAY */
    int format;                  /* SCREEN_FORMAT_* */
    int transparency;            /* -1 = leave */
    int width, height, x, y;
    int zorder;                  /* INT32_MIN = leave */
    int buffers;
    int interval;
    int seconds;
    int alpha;                   /* -1 = leave */
    int finish;                  /* gl: glFinish before swap */
    int wait_idle;               /* sw: post with SCREEN_WAIT_IDLE */
    int small_dirty;             /* sw: 64x64 damage */
} Options;

static uint64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static void hist_add(uint32_t *h, uint64_t ns)
{
    uint64_t bin = ns / BIN_NS;
    if (bin >= BINS) bin = BINS - 1u;
    ++h[bin];
}

static void hist_print(const char *name, const uint32_t *h, uint64_t total_ns,
                       uint32_t count)
{
    uint32_t i;
    printf("%-9s avg=%.2fms bins(ms:count)=", name,
           count ? (double)total_ns / count / 1e6 : 0.0);
    for (i = 0; i < BINS; ++i)
        if (h[i]) printf(" %u:%u", (unsigned)(i * 2u), h[i]);
    printf("\n");
}

static int parse_format(const char *s)
{
    if (!strcmp(s, "rgba8888")) return SCREEN_FORMAT_RGBA8888;
    if (!strcmp(s, "rgbx8888")) return SCREEN_FORMAT_RGBX8888;
    if (!strcmp(s, "rgb565")) return SCREEN_FORMAT_RGB565;
    fprintf(stderr, "unknown format %s\n", s);
    exit(2);
}

static void usage(void)
{
    puts("screen_bench [--mode gl|sw] [--class NAME] [--pipeline N] [--overlay]\n"
         "  [--format rgba8888|rgbx8888|rgb565] [--transparency none|over]\n"
         "  [--size WxH] [--pos X,Y] [--zorder Z] [--buffers N] [--interval N]\n"
         "  [--alpha 0-255] [--finish] [--dirty full|small] [--wait-idle]\n"
         "  [--seconds S<=120]\n"
         "defaults: --mode gl --size 1024x574 --pos 0,72 --zorder 999\n"
         "          --buffers 3 --interval 0 --format rgba8888 --seconds 10");
    exit(2);
}

static void parse(int argc, char **argv, Options *o)
{
    int i;
    memset(o, 0, sizeof(*o));
    o->mode = "gl";
    o->format = SCREEN_FORMAT_RGBA8888;
    o->transparency = -1;
    o->width = 1024; o->height = 574; o->x = 0; o->y = 72;
    o->zorder = 999;
    o->buffers = 3;
    o->seconds = 10;
    o->alpha = 255;
    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define NEED() do { if (!v) usage(); ++i; } while (0)
        if (!strcmp(a, "--mode")) { NEED(); o->mode = v; }
        else if (!strcmp(a, "--class")) { NEED(); o->klass = v; }
        else if (!strcmp(a, "--pipeline")) { NEED(); o->pipeline = atoi(v); }
        else if (!strcmp(a, "--overlay")) o->overlay = 1;
        else if (!strcmp(a, "--format")) { NEED(); o->format = parse_format(v); }
        else if (!strcmp(a, "--transparency")) {
            NEED();
            o->transparency = !strcmp(v, "none") ? SCREEN_TRANSPARENCY_NONE
                                                 : SCREEN_TRANSPARENCY_SOURCE_OVER;
        }
        else if (!strcmp(a, "--size")) {
            NEED(); if (sscanf(v, "%dx%d", &o->width, &o->height) != 2) usage();
        }
        else if (!strcmp(a, "--pos")) {
            NEED(); if (sscanf(v, "%d,%d", &o->x, &o->y) != 2) usage();
        }
        else if (!strcmp(a, "--zorder")) { NEED(); o->zorder = atoi(v); }
        else if (!strcmp(a, "--buffers")) { NEED(); o->buffers = atoi(v); }
        else if (!strcmp(a, "--interval")) { NEED(); o->interval = atoi(v); }
        else if (!strcmp(a, "--alpha")) { NEED(); o->alpha = atoi(v); }
        else if (!strcmp(a, "--finish")) o->finish = 1;
        else if (!strcmp(a, "--wait-idle")) o->wait_idle = 1;
        else if (!strcmp(a, "--dirty")) { NEED(); o->small_dirty = !strcmp(v, "small"); }
        else if (!strcmp(a, "--seconds")) { NEED(); o->seconds = atoi(v); }
        else usage();
#undef NEED
    }
    if (strcmp(o->mode, "gl") && strcmp(o->mode, "sw")) usage();
    if (o->seconds < 1) o->seconds = 1;
    if (o->seconds > MAX_SECONDS) o->seconds = MAX_SECONDS;
    if (o->buffers < 1 || o->buffers > 4) usage();
    if (o->width < 16 || o->height < 16 || o->width > 4096 || o->height > 4096)
        usage();
}

static int set_iv(screen_window_t w, int p, const int *v, const char *name)
{
    if (screen_set_window_property_iv(w, p, v) != 0) {
        printf("set %-14s FAILED errno=%d (%s)\n", name, errno, strerror(errno));
        return -1;
    }
    return 0;
}

static void report_applied(screen_window_t w)
{
    int pipeline = -1, usage = -1, format = -1, transparency = -1, zorder = -1;
    int buffers = -1, render = -1, interval = -1, visible = -1, alpha = -1;
    int size[2] = {-1, -1}, pos[2] = {-1, -1};
    char klass[64] = "";
    screen_get_window_property_iv(w, SCREEN_PROPERTY_PIPELINE, &pipeline);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_USAGE, &usage);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_FORMAT, &format);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_TRANSPARENCY, &transparency);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_ZORDER, &zorder);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_BUFFER_COUNT, &buffers);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_RENDER_BUFFER_COUNT, &render);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_SWAP_INTERVAL, &interval);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_VISIBLE, &visible);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_GLOBAL_ALPHA, &alpha);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_SIZE, size);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_POSITION, pos);
    screen_get_window_property_cv(w, SCREEN_PROPERTY_CLASS, sizeof(klass) - 1, klass);
    printf("applied class=%s pipeline=%d usage=0x%x format=%d transparency=%d "
           "zorder=%d buffers=%d render_buffers=%d interval=%d visible=%d "
           "alpha=%d size=%dx%d pos=%d,%d\n",
           klass[0] ? klass : "-", pipeline, (unsigned)usage, format,
           transparency, zorder, buffers, render, interval, visible, alpha,
           size[0], size[1], pos[0], pos[1]);
}

/* Exact colour sizes for the requested format first, then the provider's
 * config 0x16, then any window+GLES2 config (first smoke run: the strict
 * RGBA8888 request matched nothing on this driver). */
static int choose_config(EGLDisplay d, int format, EGLConfig *out)
{
    const int rgb565 = format == SCREEN_FORMAT_RGB565;
    const EGLint exact[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, rgb565 ? 5 : 8, EGL_GREEN_SIZE, rgb565 ? 6 : 8,
        EGL_BLUE_SIZE, rgb565 ? 5 : 8, EGL_NONE
    };
    const EGLint provider[] = {EGL_CONFIG_ID, 0x16, EGL_NONE};
    const EGLint any[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    const EGLint *tries[3] = {exact, provider, any};
    int i;
    for (i = 0; i < 3; ++i) {
        EGLint n = 0, id = 0;
        if (eglChooseConfig(d, tries[i], out, 1, &n) == EGL_TRUE && n == 1) {
            eglGetConfigAttrib(d, *out, EGL_CONFIG_ID, &id);
            printf("egl config=0x%x (%s)\n", (unsigned)id,
                   i == 0 ? "exact" : i == 1 ? "provider 0x16" : "any");
            return 0;
        }
    }
    return -1;
}

int main(int argc, char **argv)
{
    Options o;
    screen_context_t ctx = NULL;
    screen_window_t win = NULL;
    EGLDisplay egl = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext glctx = EGL_NO_CONTEXT;
    int usage, size[2], pos[2], rc = 1;
    uint32_t present_h[BINS] = {0}, interval_h[BINS] = {0}, finish_h[BINS] = {0};
    uint64_t present_total = 0, interval_total = 0, finish_total = 0;
    uint32_t frames = 0, intervals = 0;
    uint64_t start, deadline, last = 0;

    parse(argc, argv, &o);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    printf("request mode=%s class=%s pipeline=%d overlay=%d format=%d "
           "transparency=%d size=%dx%d pos=%d,%d zorder=%d buffers=%d "
           "interval=%d finish=%d dirty=%s wait_idle=%d seconds=%d\n",
           o.mode, o.klass ? o.klass : "-", o.pipeline, o.overlay, o.format,
           o.transparency, o.width, o.height, o.x, o.y, o.zorder, o.buffers,
           o.interval, o.finish, o.small_dirty ? "small" : "full", o.wait_idle,
           o.seconds);

    if (screen_create_context(&ctx, SCREEN_APPLICATION_CONTEXT) != 0) {
        perror("screen_create_context"); return 1;
    }
    if (screen_create_window(&win, ctx) != 0) {
        perror("screen_create_window"); goto out;
    }
    if (o.klass)
        screen_set_window_property_cv(win, SCREEN_PROPERTY_CLASS,
                                      (int)strlen(o.klass), o.klass);
    usage = !strcmp(o.mode, "gl") ? SCREEN_USAGE_OPENGL_ES2
                                  : (SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE);
    if (o.overlay) usage |= SCREEN_USAGE_OVERLAY;
    size[0] = o.width; size[1] = o.height; pos[0] = o.x; pos[1] = o.y;
    set_iv(win, SCREEN_PROPERTY_USAGE, &usage, "usage");
    set_iv(win, SCREEN_PROPERTY_FORMAT, &o.format, "format");
    if (o.pipeline > 0) set_iv(win, SCREEN_PROPERTY_PIPELINE, &o.pipeline, "pipeline");
    set_iv(win, SCREEN_PROPERTY_BUFFER_SIZE, size, "buffer_size");
    set_iv(win, SCREEN_PROPERTY_SIZE, size, "size");
    set_iv(win, SCREEN_PROPERTY_POSITION, pos, "position");
    set_iv(win, SCREEN_PROPERTY_ZORDER, &o.zorder, "zorder");
    set_iv(win, SCREEN_PROPERTY_SWAP_INTERVAL, &o.interval, "swap_interval");
    if (o.transparency >= 0)
        set_iv(win, SCREEN_PROPERTY_TRANSPARENCY, &o.transparency, "transparency");
    if (screen_create_window_buffers(win, o.buffers) != 0) {
        printf("screen_create_window_buffers FAILED errno=%d (%s)\n", errno,
               strerror(errno));
        goto out;
    }
    if (o.alpha >= 0)
        set_iv(win, SCREEN_PROPERTY_GLOBAL_ALPHA, &o.alpha, "global_alpha");
    /* Smoke run read z-order back as 0 when set only before buffers. */
    set_iv(win, SCREEN_PROPERTY_ZORDER, &o.zorder, "zorder");

    if (!strcmp(o.mode, "gl")) {
        static const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        EGLConfig config;
        /* EGL loads the driver named by graphics.conf via GRAPHICS_ROOT;
         * a plain SSH shell lacks it (see README). */
        if (!getenv("GRAPHICS_ROOT"))
            setenv("GRAPHICS_ROOT", "/proc/boot/usr/lib/graphics/jacinto6", 0);
        egl = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (egl == EGL_NO_DISPLAY) {
            printf("eglGetDisplay FAILED 0x%x (GRAPHICS_ROOT=%s)\n", eglGetError(),
                   getenv("GRAPHICS_ROOT") ? getenv("GRAPHICS_ROOT") : "unset");
            goto out;
        }
        if (eglInitialize(egl, NULL, NULL) != EGL_TRUE) {
            printf("eglInitialize FAILED 0x%x (GRAPHICS_ROOT=%s)\n", eglGetError(),
                   getenv("GRAPHICS_ROOT") ? getenv("GRAPHICS_ROOT") : "unset");
            goto out;
        }
        if (choose_config(egl, o.format, &config) != 0) {
            printf("no EGL config 0x%x\n", eglGetError()); goto out;
        }
        surface = eglCreateWindowSurface(egl, config, (EGLNativeWindowType)win, NULL);
        glctx = eglCreateContext(egl, config, EGL_NO_CONTEXT, ctx_attr);
        if (surface == EGL_NO_SURFACE || glctx == EGL_NO_CONTEXT ||
            eglMakeCurrent(egl, surface, surface, glctx) != EGL_TRUE) {
            printf("EGL surface/context FAILED 0x%x\n", eglGetError()); goto out;
        }
        eglSwapInterval(egl, o.interval);
    }
    report_applied(win);

    start = now_ns();
    deadline = start + (uint64_t)o.seconds * 1000000000ull;
    while (!g_stop && now_ns() < deadline) {
        uint64_t t0, t1, t2;
        const float shade = (float)(frames % 64u) / 63.0f;
        if (!strcmp(o.mode, "gl")) {
            glClearColor(0.1f, shade, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            t0 = now_ns();
            if (o.finish) glFinish();
            t1 = now_ns();
            if (eglSwapBuffers(egl, surface) != EGL_TRUE) {
                printf("eglSwapBuffers FAILED 0x%x\n", eglGetError()); break;
            }
            t2 = now_ns();
            if (o.finish) {
                hist_add(finish_h, t1 - t0);
                finish_total += t1 - t0;
            }
        } else {
            screen_buffer_t buffers[4];
            void *pointer = NULL;
            int stride = 0;
            int rect[4];
            if (screen_get_window_property_pv(win, SCREEN_PROPERTY_RENDER_BUFFERS,
                                              (void **)buffers) != 0 ||
                screen_get_buffer_property_pv(buffers[0], SCREEN_PROPERTY_POINTER,
                                              &pointer) != 0 ||
                screen_get_buffer_property_iv(buffers[0], SCREEN_PROPERTY_STRIDE,
                                              &stride) != 0 || !pointer) {
                printf("render buffer access FAILED errno=%d\n", errno); break;
            }
            /* Touch only the damaged area so CPU cost stays small. */
            rect[0] = 0; rect[1] = 0;
            rect[2] = o.small_dirty ? 64 : o.width;
            rect[3] = o.small_dirty ? 64 : o.height;
            {
                int row;
                const int bpp = o.format == SCREEN_FORMAT_RGB565 ? 2 : 4;
                for (row = 0; row < rect[3]; ++row)
                    memset((char *)pointer + (size_t)row * (size_t)stride,
                           (int)(frames * 7u), (size_t)rect[2] * (size_t)bpp);
            }
            t1 = now_ns();
            /* Posts are asynchronous, even with SCREEN_WAIT_IDLE (smoke run:
             * 0.02-0.05 ms each). --wait-idle flushes the context and waits
             * for Screen to finish, timing the composition itself. */
            if (screen_post_window(win, buffers[0], 1, rect, 0) != 0) {
                printf("screen_post_window FAILED errno=%d\n", errno); break;
            }
            if (o.wait_idle && screen_flush_context(ctx, SCREEN_WAIT_IDLE) != 0) {
                printf("screen_flush_context FAILED errno=%d\n", errno); break;
            }
            t2 = now_ns();
        }
        hist_add(present_h, t2 - t1);
        present_total += t2 - t1;
        if (last) {
            hist_add(interval_h, t2 - last);
            interval_total += t2 - last;
            ++intervals;
        }
        last = t2;
        ++frames;
    }
    {
        const double seconds = (double)(now_ns() - start) / 1e9;
        printf("result frames=%u seconds=%.2f fps=%.2f\n", frames, seconds,
               seconds > 0 ? frames / seconds : 0.0);
        hist_print(!strcmp(o.mode, "gl") ? "swap" : "post", present_h,
                   present_total, frames);
        if (o.finish) hist_print("glFinish", finish_h, finish_total, frames);
        hist_print("interval", interval_h, interval_total, intervals);
    }
    rc = 0;
out:
    if (egl != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(egl, surface);
        if (glctx != EGL_NO_CONTEXT) eglDestroyContext(egl, glctx);
    }
    if (win) screen_destroy_window(win);
    if (ctx) screen_destroy_context(ctx);
    return rc;
}
