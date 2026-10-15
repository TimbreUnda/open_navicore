#define _POSIX_C_SOURCE 200112L

#include "open_navi_renderer_platform.h"
#include "open_navi_screen_input.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <screen/screen.h>

#include <errno.h>
#include <pthread.h>
#include <sys/neutrino.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define OPEN_NAVI_PRESENT_PACING_NANOSECONDS UINT64_C(0)
#define OPEN_NAVI_WINDOW_BUFFER_COUNT 3
/* Bench experiment: "2" or "3" render buffers for NAV_main. A Screen sweep
 * showed eglSwapBuffers blocking ~37 ms with 3 buffers under the HMI but
 * ~4 ms with 2; whether more frames reach the display is what this tests. */
#define OPEN_NAVI_BUFFERS_MARKER "/datastore/navicore-backend/nav-buffers"
/* Frame cap in fps (10..120). Swaps never wait (swap interval 0), so a cheap
 * renderer would otherwise spin far above Screen's ~30 Hz composition. */
#define OPEN_NAVI_PACE_MARKER "/datastore/navicore-backend/nav-pace-fps"
/* Render thread priority (10..40). nv_navigation runs on one core with its
 * workers at the same default priority, round-robin; a higher render
 * priority keeps worker timeslices out of a frame. */
/* Input by notification: Screen sends a pulse when an event is queued and
 * the render loop calls screen_get_event() only then (plus a safety poll
 * every 500 ms), instead of an IPC to Screen on every loop, which waits
 * whenever Screen is busy compositing. */
#define OPEN_NAVI_INPUT_NOTIFY_MARKER \
    "/datastore/navicore-backend/nav-input-notify"
#define OPEN_NAVI_INPUT_SAFETY_POLL_NANOSECONDS UINT64_C(500000000)
#define OPEN_NAVI_NOTIFY_PULSE_EVENT (_PULSE_CODE_MINAVAIL + 1)
#define OPEN_NAVI_NOTIFY_PULSE_STOP (_PULSE_CODE_MINAVAIL + 2)
#define OPEN_NAVI_RENDER_PRIORITY_MARKER \
    "/datastore/navicore-backend/nav-render-priority"
#define OPEN_NAVI_PRESENT_TRACE_PATH "/tmp/open-navicore-present.log"
#define OPEN_NAVI_CONTENT_X 0
#define OPEN_NAVI_CONTENT_Y 72
#define OPEN_NAVI_CONTENT_WIDTH 1024
#define OPEN_NAVI_CONTENT_HEIGHT 574
/* NAV_main sits below the full-screen HMI_main window on the composited
 * framebuffer pipeline, so every map frame is recomposited by Screen. An
 * opaque window lets Screen copy instead of blending it over the background;
 * the marker restores the class default (source-over) for A/B runs. */
#define OPEN_NAVI_DISABLE_OPAQUE_MARKER \
    "/datastore/navicore-backend/disable-opaque-nav-window"
/* Bench experiment: request a non-composited (overlay) layer. An optional
 * positive integer in the marker selects an explicit pipeline id. */
#define OPEN_NAVI_OVERLAY_MARKER "/datastore/navicore-backend/enable-nav-overlay"
/* Diagnostic: glFinish() before eglSwapBuffers() splits the swap into our own
 * GPU work and the remaining Screen/vsync wait. It serializes CPU and GPU, so
 * it is never part of a throughput run. */
#define OPEN_NAVI_GPU_FINISH_MARKER \
    "/datastore/navicore-backend/enable-gpu-finish-probe"
/* 2 ms bins, last bin collects >= 62 ms; emitted every 5 seconds. */
#define OPEN_NAVI_HIST_BINS 32u
#define OPEN_NAVI_HIST_BIN_NS UINT64_C(2000000)
#define OPEN_NAVI_HIST_WINDOW_NS UINT64_C(5000000000)

typedef struct {
    screen_context_t screen_context;
    screen_window_t window;
    screen_event_t event;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    OpenNaviScreenInputState input;
    uint64_t next_present_nanoseconds;
    uint64_t trace_window_start_nanoseconds;
    uint64_t trace_pace_nanoseconds;
    uint64_t pace_nanoseconds;
    int render_priority;
    int render_priority_checked;
    /* input notification */
    int notify_chid;
    int notify_coid;
    int notify_active;
    pthread_t notify_thread;
    volatile int input_pending;
    uint64_t last_drain_nanoseconds;
    /* screen_get_event() cost, per present-trace window */
    uint32_t poll_calls;
    uint64_t poll_total_nanoseconds;
    uint64_t poll_max_nanoseconds;
    uint64_t trace_swap_nanoseconds;
    uint64_t trace_swap_max_nanoseconds;
    uint32_t trace_presented;
    FILE *trace_file;
    int gpu_finish_probe;
    uint64_t hist_window_start_nanoseconds;
    uint64_t last_present_nanoseconds;
    uint64_t finish_total_nanoseconds;
    uint32_t hist_frames;
    uint32_t finish_hist[OPEN_NAVI_HIST_BINS];
    uint32_t swap_hist[OPEN_NAVI_HIST_BINS];
    uint32_t interval_hist[OPEN_NAVI_HIST_BINS];
    int asynchronous_swap;
    uint32_t ignored_events;
    uint32_t event_errors;
    uint32_t window_lost;
} QnxRenderer;

static QnxRenderer g_qnx;

/* Diagnostic: with the marker below, every raw Screen input event and the
 * decoder's verdict go to INPUT_TRACE_PATH (bounded), to see what the
 * provider receives during a touch drag. */
#define INPUT_TRACE_MARKER "/datastore/navicore-backend/enable-input-trace"
#define INPUT_TRACE_PATH "/tmp/open-navicore-input.log"
#define INPUT_TRACE_LINES 4000u
static FILE *g_input_trace;
static unsigned int g_input_trace_lines;

static void input_trace_open(void)
{
    if (g_input_trace != NULL || access(INPUT_TRACE_MARKER, F_OK) != 0) return;
    g_input_trace = fopen(INPUT_TRACE_PATH, "w");
    g_input_trace_lines = 0u;
}

static void input_trace(const char *format, ...)
{
    struct timespec now;
    va_list args;
    if (g_input_trace == NULL || g_input_trace_lines >= INPUT_TRACE_LINES) return;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    (void)fprintf(g_input_trace, "%lld.%03ld ", (long long)now.tv_sec,
                  now.tv_nsec / 1000000L);
    va_start(args, format);
    (void)vfprintf(g_input_trace, format, args);
    va_end(args);
    (void)fputc('\n', g_input_trace);
    (void)fflush(g_input_trace);
    ++g_input_trace_lines;
}

static uint64_t monotonic_nanoseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0u;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
           (uint64_t)now.tv_nsec;
}

static void pace_presentation(void)
{
    uint64_t now = monotonic_nanoseconds();
    if (g_qnx.pace_nanoseconds == 0u) return;
    if (now == 0u) return;
    if (g_qnx.next_present_nanoseconds == 0u ||
        now > g_qnx.next_present_nanoseconds +
              g_qnx.pace_nanoseconds) {
        g_qnx.next_present_nanoseconds = now;
    }
    while (now < g_qnx.next_present_nanoseconds) {
        const uint64_t remaining = g_qnx.next_present_nanoseconds - now;
        struct timespec delay;
        delay.tv_sec = (time_t)(remaining / UINT64_C(1000000000));
        delay.tv_nsec = (long)(remaining % UINT64_C(1000000000));
        if (nanosleep(&delay, NULL) == 0 || errno != EINTR) break;
        now = monotonic_nanoseconds();
        if (now == 0u) return;
    }
    g_qnx.next_present_nanoseconds += g_qnx.pace_nanoseconds;
}

static void hist_add(uint32_t *hist, uint64_t nanoseconds)
{
    uint64_t bin = nanoseconds / OPEN_NAVI_HIST_BIN_NS;
    if (bin >= OPEN_NAVI_HIST_BINS) bin = OPEN_NAVI_HIST_BINS - 1u;
    ++hist[bin];
}

/* Writes "lower_ms:count,..." for non-empty bins. */
static void hist_print(FILE *file, const char *name, const uint32_t *hist)
{
    uint32_t bin;
    int first = 1;
    (void)fprintf(file, " %s=", name);
    for (bin = 0; bin < OPEN_NAVI_HIST_BINS; ++bin) {
        if (hist[bin] == 0u) continue;
        (void)fprintf(file, "%s%u:%u", first ? "" : ",",
                      bin * (unsigned int)(OPEN_NAVI_HIST_BIN_NS / 1000000u),
                      hist[bin]);
        first = 0;
    }
    if (first) (void)fputc('-', file);
}

static void record_present_histograms(uint64_t finish_ns, uint64_t swap_ns,
                                      uint64_t end_ns)
{
    if (g_qnx.hist_window_start_nanoseconds == 0u)
        g_qnx.hist_window_start_nanoseconds = end_ns;
    if (g_qnx.gpu_finish_probe) {
        hist_add(g_qnx.finish_hist, finish_ns);
        g_qnx.finish_total_nanoseconds += finish_ns;
    }
    hist_add(g_qnx.swap_hist, swap_ns);
    if (g_qnx.last_present_nanoseconds != 0u &&
        end_ns - g_qnx.last_present_nanoseconds < UINT64_C(1000000000))
        hist_add(g_qnx.interval_hist, end_ns - g_qnx.last_present_nanoseconds);
    g_qnx.last_present_nanoseconds = end_ns;
    ++g_qnx.hist_frames;
    if (end_ns - g_qnx.hist_window_start_nanoseconds < OPEN_NAVI_HIST_WINDOW_NS ||
        g_qnx.trace_file == NULL)
        return;
    (void)fprintf(g_qnx.trace_file, "presenthist window_ms=%llu frames=%u "
                  "gpu_finish_probe=%d finish_avg_us=%llu",
                  (unsigned long long)((end_ns -
                      g_qnx.hist_window_start_nanoseconds) / 1000000u),
                  g_qnx.hist_frames, g_qnx.gpu_finish_probe,
                  (unsigned long long)(g_qnx.finish_total_nanoseconds /
                                       g_qnx.hist_frames / 1000u));
    if (g_qnx.gpu_finish_probe)
        hist_print(g_qnx.trace_file, "finish", g_qnx.finish_hist);
    hist_print(g_qnx.trace_file, "swap", g_qnx.swap_hist);
    hist_print(g_qnx.trace_file, "interval", g_qnx.interval_hist);
    (void)fputc('\n', g_qnx.trace_file);
    (void)fflush(g_qnx.trace_file);
    memset(g_qnx.finish_hist, 0, sizeof(g_qnx.finish_hist));
    memset(g_qnx.swap_hist, 0, sizeof(g_qnx.swap_hist));
    memset(g_qnx.interval_hist, 0, sizeof(g_qnx.interval_hist));
    g_qnx.finish_total_nanoseconds = 0u;
    g_qnx.hist_frames = 0u;
    g_qnx.hist_window_start_nanoseconds = end_ns;
}

static int marker_present(const char *path)
{
    return access(path, F_OK) == 0;
}

static int marker_integer(const char *path)
{
    FILE *marker = fopen(path, "r");
    int value = 0;
    if (marker == NULL) return 0;
    if (fscanf(marker, "%d", &value) != 1 || value < 0) value = 0;
    (void)fclose(marker);
    return value;
}

static void *notify_main(void *unused)
{
    (void)unused;
    for (;;) {
        struct _pulse pulse;
        if (MsgReceivePulse(g_qnx.notify_chid, &pulse, sizeof(pulse), NULL) == -1) {
            if (errno == EINTR) continue;
            break;
        }
        if (pulse.code == OPEN_NAVI_NOTIFY_PULSE_STOP) break;
        if (pulse.code == OPEN_NAVI_NOTIFY_PULSE_EVENT)
            __atomic_store_n(&g_qnx.input_pending, 1, __ATOMIC_RELEASE);
    }
    return NULL;
}

/* After the window exists. Any failure leaves polling on every loop. */
static void start_input_notify(void)
{
    struct sigevent event;
    if (access(OPEN_NAVI_INPUT_NOTIFY_MARKER, F_OK) != 0) return;
    g_qnx.notify_chid = ChannelCreate(_NTO_CHF_PRIVATE);
    if (g_qnx.notify_chid == -1) return;
    g_qnx.notify_coid = ConnectAttach(0, 0, g_qnx.notify_chid, _NTO_SIDE_CHANNEL, 0);
    if (g_qnx.notify_coid != -1) {
        SIGEV_PULSE_INIT(&event, g_qnx.notify_coid, SIGEV_PULSE_PRIO_INHERIT,
                         OPEN_NAVI_NOTIFY_PULSE_EVENT, 0);
        g_qnx.input_pending = 1;
        if (pthread_create(&g_qnx.notify_thread, NULL, notify_main, NULL) == 0) {
            if (screen_notify(g_qnx.screen_context, SCREEN_NOTIFY_EVENT, NULL,
                              &event) == 0) {
                g_qnx.notify_active = 1;
            } else {
                (void)MsgSendPulse(g_qnx.notify_coid, -1,
                                   OPEN_NAVI_NOTIFY_PULSE_STOP, 0);
                (void)pthread_join(g_qnx.notify_thread, NULL);
            }
        }
    }
    if (!g_qnx.notify_active) {
        if (g_qnx.notify_coid != -1) (void)ConnectDetach(g_qnx.notify_coid);
        (void)ChannelDestroy(g_qnx.notify_chid);
        g_qnx.notify_chid = g_qnx.notify_coid = 0;
    }
}

static void stop_input_notify(void)
{
    if (!g_qnx.notify_active) return;
    (void)screen_notify(g_qnx.screen_context, SCREEN_NOTIFY_EVENT, NULL, NULL);
    (void)MsgSendPulse(g_qnx.notify_coid, -1, OPEN_NAVI_NOTIFY_PULSE_STOP, 0);
    (void)pthread_join(g_qnx.notify_thread, NULL);
    (void)ConnectDetach(g_qnx.notify_coid);
    (void)ChannelDestroy(g_qnx.notify_chid);
    g_qnx.notify_active = 0;
}

int open_navi_renderer_platform_input_pending(void)
{
    uint64_t now;
    if (!g_qnx.notify_active) return 1;
    now = monotonic_nanoseconds();
    if (__atomic_exchange_n(&g_qnx.input_pending, 0, __ATOMIC_ACQ_REL) != 0 ||
        now - g_qnx.last_drain_nanoseconds >= OPEN_NAVI_INPUT_SAFETY_POLL_NANOSECONDS) {
        g_qnx.last_drain_nanoseconds = now;
        return 1;
    }
    return 0;
}

/* Runs on the render thread at its first frame, after the backend started:
 * threads the backend created during its start keep the inherited
 * priority. The result goes to the present trace. */
static void apply_render_priority(void)
{
    const int priority = marker_integer(OPEN_NAVI_RENDER_PRIORITY_MARKER);
    struct sched_param parameters;
    int policy = 0;
    int applied = 0;
    if (pthread_getschedparam(pthread_self(), &policy, &parameters) != 0)
        return;
    g_qnx.render_priority = parameters.sched_priority;
    if (priority >= 10 && priority <= 40) {
        parameters.sched_priority = priority;
        applied = pthread_setschedparam(pthread_self(), policy, &parameters) == 0;
        if (applied) g_qnx.render_priority = priority;
    }
    if (g_qnx.trace_file != NULL) {
        (void)fprintf(g_qnx.trace_file, "render_priority=%d marker=%d applied=%d\n",
                      g_qnx.render_priority, priority, applied);
        (void)fflush(g_qnx.trace_file);
    }
}

static void set_diagnostic(char *output, size_t capacity,
                           const char *format, ...)
{
    va_list arguments;
    if (output == NULL || capacity == 0u) return;
    va_start(arguments, format);
    (void)vsnprintf(output, capacity, format, arguments);
    va_end(arguments);
    output[capacity - 1u] = '\0';
}

static int screen_error(char *output, size_t capacity, const char *operation)
{
    int saved_errno = errno;
    set_diagnostic(output, capacity, "%s failed: errno=%d (%s)",
                   operation, saved_errno, strerror(saved_errno));
    return -1;
}

static int egl_error(char *output, size_t capacity, const char *operation)
{
    EGLint error = eglGetError();
    set_diagnostic(output, capacity, "%s failed: EGL error 0x%04x",
                   operation, (unsigned int)error);
    return -1;
}

static int config_has_required_bits(EGLDisplay display, EGLConfig config)
{
    EGLint surface_type = 0;
    EGLint renderable_type = 0;
    return eglGetConfigAttrib(display, config, EGL_SURFACE_TYPE,
                              &surface_type) == EGL_TRUE &&
           eglGetConfigAttrib(display, config, EGL_RENDERABLE_TYPE,
                              &renderable_type) == EGL_TRUE &&
           (surface_type & EGL_WINDOW_BIT) != 0 &&
           (renderable_type & EGL_OPENGL_ES2_BIT) != 0;
}

static int choose_config(EGLDisplay display, EGLConfig *config_out,
                         EGLint *id_out, char *diagnostic, size_t capacity)
{
    static const EGLint preferred[] = {
        EGL_CONFIG_ID, 0x16, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE
    };
    static const EGLint fallback[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE
    };
    EGLConfig config = NULL;
    EGLint count = 0;
    EGLint id = 0;
    if (eglChooseConfig(display, preferred, &config, 1, &count) != EGL_TRUE)
        return egl_error(diagnostic, capacity, "eglChooseConfig(config 0x16)");
    if (count < 1 || !config_has_required_bits(display, config)) {
        count = 0;
        config = NULL;
        if (eglChooseConfig(display, fallback, &config, 1, &count) != EGL_TRUE)
            return egl_error(diagnostic, capacity, "eglChooseConfig(fallback)");
    }
    if (count < 1 || config == NULL ||
        !config_has_required_bits(display, config)) {
        set_diagnostic(diagnostic, capacity,
                       "no EGL config supports WINDOW+GLES2");
        return -1;
    }
    if (eglGetConfigAttrib(display, config, EGL_CONFIG_ID, &id) != EGL_TRUE)
        return egl_error(diagnostic, capacity,
                         "eglGetConfigAttrib(EGL_CONFIG_ID)");
    *config_out = config;
    *id_out = id;
    return 0;
}

void open_navi_renderer_platform_destroy(void)
{
    if (g_qnx.display != EGL_NO_DISPLAY) {
        (void)eglMakeCurrent(g_qnx.display, EGL_NO_SURFACE,
                             EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_qnx.surface != EGL_NO_SURFACE)
            (void)eglDestroySurface(g_qnx.display, g_qnx.surface);
        if (g_qnx.context != EGL_NO_CONTEXT)
            (void)eglDestroyContext(g_qnx.display, g_qnx.context);
    }
    stop_input_notify();
    if (g_qnx.event != NULL) (void)screen_destroy_event(g_qnx.event);
    if (g_qnx.window != NULL) (void)screen_destroy_window(g_qnx.window);
    if (g_qnx.screen_context != NULL)
        (void)screen_destroy_context(g_qnx.screen_context);
    if (g_qnx.trace_file != NULL) (void)fclose(g_qnx.trace_file);
    /* EGLDisplay is process-global on this stack; do not eglTerminate it. */
    memset(&g_qnx, 0, sizeof(g_qnx));
    g_qnx.display = EGL_NO_DISPLAY;
    g_qnx.surface = EGL_NO_SURFACE;
    g_qnx.context = EGL_NO_CONTEXT;
}

int open_navi_renderer_platform_create(OpenNaviBackendRenderTargetV1 *target,
                                  char *diagnostic, size_t capacity)
{
    static const char window_class[8] = {'N','A','V','_','m','a','i','n'};
    static const EGLint surface_attributes[] = {
        EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE
    };
    static const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
    };
    EGLConfig config = NULL;
    EGLint config_id = 0;
    EGLint min_swap_interval = -1;
    EGLint max_swap_interval = -1;
    int size[2] = { 0, 0 };
    int window_position[2] = { OPEN_NAVI_CONTENT_X, OPEN_NAVI_CONTENT_Y };
    int window_size[2] = { OPEN_NAVI_CONTENT_WIDTH, OPEN_NAVI_CONTENT_HEIGHT };
    int source_position[2] = { 0, 0 };
    int source_size[2] = { OPEN_NAVI_CONTENT_WIDTH, OPEN_NAVI_CONTENT_HEIGHT };
    int buffer_size[2] = { OPEN_NAVI_CONTENT_WIDTH, OPEN_NAVI_CONTENT_HEIGHT };
    int rotation = 0;
    int sensitivity = SCREEN_SENSITIVITY_TEST;
    int screen_swap_interval = -1;
    int buffer_count = -1;
    int render_buffer_count = -1;
    int pipeline = -1;
    int class_usage = -1;
    int class_pipeline = -1;
    int overlay_requested = marker_present(OPEN_NAVI_OVERLAY_MARKER);
    int overlay_pipeline = overlay_requested ?
        marker_integer(OPEN_NAVI_OVERLAY_MARKER) : 0;
    int overlay_applied = 0;
    int opaque_requested = !marker_present(OPEN_NAVI_DISABLE_OPAQUE_MARKER);
    int opaque_applied = 0;
    int transparency = -1;
    int usage = -1;
    int requested_buffers = OPEN_NAVI_WINDOW_BUFFER_COUNT;
    int identity_coordinates;

    memset(&g_qnx, 0, sizeof(g_qnx));
    g_qnx.display = EGL_NO_DISPLAY;
    g_qnx.surface = EGL_NO_SURFACE;
    g_qnx.context = EGL_NO_CONTEXT;
    if (screen_create_context(&g_qnx.screen_context,
                              SCREEN_APPLICATION_CONTEXT) != 0)
        return screen_error(diagnostic, capacity, "screen_create_context");
    g_qnx.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_qnx.display == EGL_NO_DISPLAY ||
        eglInitialize(g_qnx.display, NULL, NULL) != EGL_TRUE ||
        eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        (void)egl_error(diagnostic, capacity, "EGL display initialization");
        goto fail;
    }
    if (screen_create_window(&g_qnx.window, g_qnx.screen_context) != 0 ||
        screen_set_window_property_cv(g_qnx.window, SCREEN_PROPERTY_CLASS,
                                      (int)sizeof(window_class),
                                      window_class) != 0 ||
        screen_set_window_property_iv(g_qnx.window,
                                      SCREEN_PROPERTY_BUFFER_SIZE,
                                      buffer_size) != 0 ||
        screen_set_window_property_iv(g_qnx.window, SCREEN_PROPERTY_POSITION,
                                      window_position) != 0 ||
        screen_set_window_property_iv(g_qnx.window, SCREEN_PROPERTY_SIZE,
                                      window_size) != 0 ||
        screen_set_window_property_iv(g_qnx.window,
                                      SCREEN_PROPERTY_SOURCE_POSITION,
                                      source_position) != 0 ||
        screen_set_window_property_iv(g_qnx.window,
                                      SCREEN_PROPERTY_SOURCE_SIZE,
                                      source_size) != 0) {
        (void)screen_error(diagnostic, capacity, "NAV_main window creation");
        goto fail;
    }
    /* Usage and pipeline must be chosen before buffers exist. */
    (void)screen_get_window_property_iv(g_qnx.window, SCREEN_PROPERTY_USAGE,
                                        &class_usage);
    (void)screen_get_window_property_iv(g_qnx.window, SCREEN_PROPERTY_PIPELINE,
                                        &class_pipeline);
    if (overlay_requested) {
        int overlay_usage = SCREEN_USAGE_OPENGL_ES2 | SCREEN_USAGE_OVERLAY;
        overlay_applied =
            screen_set_window_property_iv(g_qnx.window, SCREEN_PROPERTY_USAGE,
                                          &overlay_usage) == 0 &&
            (overlay_pipeline <= 0 ||
             screen_set_window_property_iv(g_qnx.window,
                                           SCREEN_PROPERTY_PIPELINE,
                                           &overlay_pipeline) == 0);
        if (!overlay_applied) {
            /* Never leave a half-applied overlay request behind. */
            int fallback_usage = class_usage > 0 ? class_usage :
                                                   SCREEN_USAGE_OPENGL_ES2;
            (void)screen_set_window_property_iv(g_qnx.window,
                                                SCREEN_PROPERTY_USAGE,
                                                &fallback_usage);
            if (class_pipeline >= 0)
                (void)screen_set_window_property_iv(g_qnx.window,
                                                    SCREEN_PROPERTY_PIPELINE,
                                                    &class_pipeline);
        }
    }
    {
        const int pace_fps = marker_integer(OPEN_NAVI_PACE_MARKER);
        g_qnx.pace_nanoseconds = (pace_fps >= 10 && pace_fps <= 120)
            ? UINT64_C(1000000000) / (uint64_t)pace_fps
            : OPEN_NAVI_PRESENT_PACING_NANOSECONDS;
        const int marker_buffers = marker_integer(OPEN_NAVI_BUFFERS_MARKER);
        if (marker_buffers == 2 || marker_buffers == 3)
            requested_buffers = marker_buffers;
    }
    if (screen_create_window_buffers(g_qnx.window, requested_buffers) != 0) {
        /* A rejected overlay request falls back to the class composition. */
        int fallback_usage = class_usage > 0 ? class_usage :
                                               SCREEN_USAGE_OPENGL_ES2;
        if (!overlay_requested ||
            screen_set_window_property_iv(g_qnx.window, SCREEN_PROPERTY_USAGE,
                                          &fallback_usage) != 0 ||
            (class_pipeline >= 0 &&
             screen_set_window_property_iv(g_qnx.window,
                                           SCREEN_PROPERTY_PIPELINE,
                                           &class_pipeline) != 0) ||
            screen_create_window_buffers(g_qnx.window,
                                         requested_buffers) != 0) {
            (void)screen_error(diagnostic, capacity, "NAV_main window buffers");
            goto fail;
        }
        overlay_applied = 0;
    }
    if (screen_create_event(&g_qnx.event) != 0) {
        (void)screen_error(diagnostic, capacity, "NAV_main window event");
        goto fail;
    }
    if (opaque_requested) {
        int opaque = SCREEN_TRANSPARENCY_NONE;
        opaque_applied = screen_set_window_property_iv(
            g_qnx.window, SCREEN_PROPERTY_TRANSPARENCY, &opaque) == 0;
    }
    if (choose_config(g_qnx.display, &config, &config_id,
                      diagnostic, capacity) != 0) goto fail;
    g_qnx.surface = eglCreateWindowSurface(
        g_qnx.display, config, (EGLNativeWindowType)g_qnx.window,
        surface_attributes);
    g_qnx.context = eglCreateContext(g_qnx.display, config, EGL_NO_CONTEXT,
                                     context_attributes);
    if (g_qnx.surface == EGL_NO_SURFACE || g_qnx.context == EGL_NO_CONTEXT) {
        (void)egl_error(diagnostic, capacity, "EGL surface/context creation");
        goto fail;
    }
    (void)eglGetConfigAttrib(g_qnx.display, config, EGL_MIN_SWAP_INTERVAL,
                             &min_swap_interval);
    (void)eglGetConfigAttrib(g_qnx.display, config, EGL_MAX_SWAP_INTERVAL,
                             &max_swap_interval);
    if (eglMakeCurrent(g_qnx.display, g_qnx.surface, g_qnx.surface,
                       g_qnx.context) == EGL_TRUE) {
        g_qnx.asynchronous_swap =
            eglSwapInterval(g_qnx.display, 0) == EGL_TRUE;
    } else {
        (void)egl_error(diagnostic, capacity,
                        "eglMakeCurrent(configure swap interval)");
        goto fail;
    }
    if (screen_get_window_property_iv(g_qnx.window,
                                      SCREEN_PROPERTY_BUFFER_SIZE, size) != 0) {
        (void)screen_error(diagnostic, capacity,
                           "screen_get_window_property_iv(BUFFER_SIZE)");
        goto fail;
    }
    identity_coordinates =
        screen_get_window_property_iv(g_qnx.window, SCREEN_PROPERTY_POSITION,
                                      window_position) == 0 &&
        screen_get_window_property_iv(g_qnx.window, SCREEN_PROPERTY_SIZE,
                                      window_size) == 0 &&
        screen_get_window_property_iv(g_qnx.window,
                                      SCREEN_PROPERTY_SOURCE_POSITION,
                                      source_position) == 0 &&
        screen_get_window_property_iv(g_qnx.window, SCREEN_PROPERTY_SOURCE_SIZE,
                                      source_size) == 0 &&
        screen_get_window_property_iv(g_qnx.window, SCREEN_PROPERTY_ROTATION,
                                      &rotation) == 0 &&
        window_position[0] == 0 && window_position[1] == 0 &&
        source_position[0] == 0 && source_position[1] == 0 &&
        rotation == 0 && window_size[0] == size[0] &&
        window_size[1] == size[1] && source_size[0] == size[0] &&
        source_size[1] == size[1];
    (void)screen_get_window_property_iv(g_qnx.window,
                                        SCREEN_PROPERTY_SENSITIVITY,
                                        &sensitivity);
    (void)screen_get_window_property_iv(g_qnx.window,
                                        SCREEN_PROPERTY_SWAP_INTERVAL,
                                        &screen_swap_interval);
    (void)screen_get_window_property_iv(g_qnx.window,
                                        SCREEN_PROPERTY_BUFFER_COUNT,
                                        &buffer_count);
    (void)screen_get_window_property_iv(g_qnx.window,
                                        SCREEN_PROPERTY_RENDER_BUFFER_COUNT,
                                        &render_buffer_count);
    (void)screen_get_window_property_iv(g_qnx.window,
                                        SCREEN_PROPERTY_PIPELINE,
                                        &pipeline);
    (void)screen_get_window_property_iv(g_qnx.window,
                                        SCREEN_PROPERTY_TRANSPARENCY,
                                        &transparency);
    (void)screen_get_window_property_iv(g_qnx.window, SCREEN_PROPERTY_USAGE,
                                        &usage);
    memset(target, 0, sizeof(*target));
    target->abi_version = OPEN_NAVI_BACKEND_ABI_V1;
    target->struct_size = sizeof(*target);
    target->width = size[0] > 0 ? (uint32_t)size[0] : 800u;
    target->height = size[1] > 0 ? (uint32_t)size[1] : 480u;
    open_navi_screen_input_reset(&g_qnx.input, (uintptr_t)g_qnx.window,
                            identity_coordinates, target->width,
                            target->height);
    input_trace_open();
    input_trace("input_trace start window=%p size=%ux%u identity=%d",
                (void *)g_qnx.window, target->width, target->height,
                identity_coordinates);
    target->native_pointer_bytes = sizeof(void *);
    target->native_screen_window = (uintptr_t)g_qnx.window;
    target->native_egl_display = (uintptr_t)g_qnx.display;
    target->native_egl_surface = (uintptr_t)g_qnx.surface;
    target->native_egl_context = (uintptr_t)g_qnx.context;
    g_qnx.trace_file = fopen(OPEN_NAVI_PRESENT_TRACE_PATH, "a");
    g_qnx.gpu_finish_probe = marker_present(OPEN_NAVI_GPU_FINISH_MARKER);
    start_input_notify();
    if (g_qnx.trace_file != NULL) {
        /* The provider diagnostic is capped at 256 bytes; keep composition
         * policy on its own trace line. */
        (void)fprintf(g_qnx.trace_file,
                      "composition class_usage=0x%x class_pipeline=%d "
                      "usage=0x%x pipeline=%d overlay_requested=%d "
                      "overlay_pipeline=%d overlay_applied=%d "
                      "opaque_requested=%d opaque_applied=%d "
                      "transparency=%d buffers=%d pace_us=%llu\n",
                      (unsigned int)class_usage, class_pipeline,
                      (unsigned int)usage, pipeline, overlay_requested,
                      overlay_pipeline, overlay_applied, opaque_requested,
                      opaque_applied, transparency, requested_buffers,
                      (unsigned long long)(g_qnx.pace_nanoseconds / 1000u));
        (void)fflush(g_qnx.trace_file);
    }
    set_diagnostic(diagnostic, capacity,
                   "backend renderer live: class=NAV_main config=0x%x "
                   "requested_buffers=%d geometry=%ux%u input_identity=%d "
                   "sensitivity=%d "
                   "swap_range=%d..%d swap0=%d screen_swap=%d "
                   "buffer_count=%d render_buffers=%d pipeline=%d "
                   "pace_interval_us=0 acceptance_floor_fps=30 uncapped=1 "
                   "persistent_context=1",
                   (unsigned int)config_id, requested_buffers,
                   target->width, target->height,
                   identity_coordinates, sensitivity, min_swap_interval,
                   max_swap_interval, g_qnx.asynchronous_swap,
                   screen_swap_interval, buffer_count, render_buffer_count,
                   pipeline);
    return 0;
fail:
    open_navi_renderer_platform_destroy();
    return -1;
}

int open_navi_renderer_platform_begin_frame(char *diagnostic, size_t capacity)
{
    if (!g_qnx.render_priority_checked) {
        g_qnx.render_priority_checked = 1;
        apply_render_priority();
    }
    if (eglGetCurrentContext() == g_qnx.context &&
        eglGetCurrentSurface(EGL_DRAW) == g_qnx.surface &&
        eglGetCurrentSurface(EGL_READ) == g_qnx.surface)
        return 0;
    if (eglMakeCurrent(g_qnx.display, g_qnx.surface, g_qnx.surface,
                       g_qnx.context) != EGL_TRUE)
        return egl_error(diagnostic, capacity, "eglMakeCurrent");
    return 0;
}

int open_navi_renderer_platform_end_frame(int present, char *diagnostic,
                                     size_t capacity)
{
    int result = 0;
    if (present != 0) {
        const uint64_t begin_nanoseconds = monotonic_nanoseconds();
        uint64_t swap_begin_nanoseconds;
        uint64_t end_nanoseconds;
        uint64_t finish_nanoseconds = 0u;
        pace_presentation();
        if (g_qnx.gpu_finish_probe) {
            const uint64_t finish_begin = monotonic_nanoseconds();
            glFinish();
            finish_nanoseconds = monotonic_nanoseconds() - finish_begin;
        }
        swap_begin_nanoseconds = monotonic_nanoseconds();
        if (eglSwapBuffers(g_qnx.display, g_qnx.surface) != EGL_TRUE)
            result = egl_error(diagnostic, capacity, "eglSwapBuffers");
        end_nanoseconds = monotonic_nanoseconds();
        if (begin_nanoseconds != 0u && swap_begin_nanoseconds != 0u &&
            end_nanoseconds != 0u) {
            const uint64_t pace_nanoseconds =
                swap_begin_nanoseconds - begin_nanoseconds;
            const uint64_t swap_nanoseconds =
                end_nanoseconds - swap_begin_nanoseconds;
            if (g_qnx.trace_window_start_nanoseconds == 0u)
                g_qnx.trace_window_start_nanoseconds = begin_nanoseconds;
            g_qnx.trace_pace_nanoseconds += pace_nanoseconds;
            g_qnx.trace_swap_nanoseconds += swap_nanoseconds;
            if (swap_nanoseconds > g_qnx.trace_swap_max_nanoseconds)
                g_qnx.trace_swap_max_nanoseconds = swap_nanoseconds;
            ++g_qnx.trace_presented;
            record_present_histograms(finish_nanoseconds, swap_nanoseconds,
                                      end_nanoseconds);
            if (end_nanoseconds - g_qnx.trace_window_start_nanoseconds >=
                    UINT64_C(1000000000) &&
                g_qnx.trace_file != NULL) {
                const uint64_t elapsed_nanoseconds =
                    end_nanoseconds - g_qnx.trace_window_start_nanoseconds;
                (void)fprintf(
                    g_qnx.trace_file,
                    "present window_ms=%llu presented=%u fps_x10=%llu "
                    "pace_avg_us=%llu swap_avg_us=%llu swap_max_us=%llu "
                    "poll_calls=%u poll_total_us=%llu poll_max_us=%llu notify=%d\n",
                    (unsigned long long)(elapsed_nanoseconds /
                                         UINT64_C(1000000)),
                    g_qnx.trace_presented,
                    (unsigned long long)(g_qnx.trace_presented *
                                         UINT64_C(10000000000) /
                                         elapsed_nanoseconds),
                    (unsigned long long)(g_qnx.trace_pace_nanoseconds /
                                         g_qnx.trace_presented /
                                         UINT64_C(1000)),
                    (unsigned long long)(g_qnx.trace_swap_nanoseconds /
                                         g_qnx.trace_presented /
                                         UINT64_C(1000)),
                    (unsigned long long)(g_qnx.trace_swap_max_nanoseconds /
                                         UINT64_C(1000)),
                    g_qnx.poll_calls,
                    (unsigned long long)(g_qnx.poll_total_nanoseconds / 1000u),
                    (unsigned long long)(g_qnx.poll_max_nanoseconds / 1000u),
                    g_qnx.notify_active);
                g_qnx.poll_calls = 0u;
                g_qnx.poll_total_nanoseconds = 0u;
                g_qnx.poll_max_nanoseconds = 0u;
                (void)fflush(g_qnx.trace_file);
                g_qnx.trace_window_start_nanoseconds = end_nanoseconds;
                g_qnx.trace_pace_nanoseconds = 0u;
                g_qnx.trace_swap_nanoseconds = 0u;
                g_qnx.trace_swap_max_nanoseconds = 0u;
                g_qnx.trace_presented = 0u;
            }
        }
    }
    return result;
}

static void increment_counter(uint32_t *counter)
{
    if (*counter != UINT32_MAX) ++*counter;
}

int open_navi_renderer_platform_poll_pointer(OpenNaviBackendPointerV2 *pointer_out,
                                        int contacts,
                                        char *diagnostic, size_t capacity)
{
    OpenNaviScreenRawEvent raw;
    screen_window_t event_window = NULL;
    int event_type = SCREEN_EVENT_NONE;
    int position[2];
    int source_position[2];
    int value;

    if (pointer_out == NULL || g_qnx.screen_context == NULL ||
        g_qnx.event == NULL) return -1;
    for (;;) {
        const uint64_t poll_begin = monotonic_nanoseconds();
        const int got = screen_get_event(g_qnx.screen_context, g_qnx.event, 0u);
        const uint64_t poll_nanoseconds = monotonic_nanoseconds() - poll_begin;
        ++g_qnx.poll_calls;
        g_qnx.poll_total_nanoseconds += poll_nanoseconds;
        if (poll_nanoseconds > g_qnx.poll_max_nanoseconds)
            g_qnx.poll_max_nanoseconds = poll_nanoseconds;
        if (got == 0) break;
        if (errno == EINTR) continue;
        increment_counter(&g_qnx.event_errors);
        (void)screen_error(diagnostic, capacity, "screen_get_event(0)");
        return 3;
    }
    if (screen_get_event_property_iv(g_qnx.event, SCREEN_PROPERTY_TYPE,
                                     &event_type) != 0) {
        increment_counter(&g_qnx.event_errors);
        (void)screen_error(diagnostic, capacity,
                           "screen_get_event_property_iv(TYPE)");
        return 3;
    }
    if (event_type == SCREEN_EVENT_NONE) return 0;

    if (event_type == SCREEN_EVENT_CLOSE ||
        event_type == SCREEN_EVENT_UNREALIZE) {
        if (screen_get_event_property_pv(g_qnx.event, SCREEN_PROPERTY_WINDOW,
                                         (void **)&event_window) != 0) {
            increment_counter(&g_qnx.event_errors);
            (void)screen_error(diagnostic, capacity,
                               "screen_get_event_property_pv(WINDOW)");
            return 3;
        }
        if (event_window == g_qnx.window) {
            increment_counter(&g_qnx.window_lost);
            set_diagnostic(diagnostic, capacity,
                           "NAV_main Screen window lost event=%d", event_type);
            return -2;
        }
        increment_counter(&g_qnx.ignored_events);
        return 2;
    }

    memset(&raw, 0, sizeof(raw));
    if (event_type == SCREEN_EVENT_POINTER)
        raw.type = OPEN_NAVI_SCREEN_RAW_POINTER;
    else if (event_type == SCREEN_EVENT_MTOUCH_TOUCH)
        raw.type = OPEN_NAVI_SCREEN_RAW_TOUCH;
    else if (event_type == SCREEN_EVENT_MTOUCH_MOVE)
        raw.type = OPEN_NAVI_SCREEN_RAW_MOVE;
    else if (event_type == SCREEN_EVENT_MTOUCH_RELEASE)
        raw.type = OPEN_NAVI_SCREEN_RAW_RELEASE;
    else {
        increment_counter(&g_qnx.ignored_events);
        input_trace("type=%d ignored_type", event_type);
        return 2;
    }

    if (screen_get_event_property_pv(g_qnx.event, SCREEN_PROPERTY_WINDOW,
                                     (void **)&event_window) != 0) {
        increment_counter(&g_qnx.event_errors);
        (void)screen_error(diagnostic, capacity,
                           "screen_get_event_property_pv(WINDOW)");
        return 3;
    }
    raw.window = (uintptr_t)event_window;
    if (screen_get_event_property_iv(g_qnx.event, SCREEN_PROPERTY_POSITION,
                                     position) == 0) {
        raw.position_x = position[0];
        raw.position_y = position[1];
        raw.position_valid = 1u;
    }
    if (screen_get_event_property_iv(g_qnx.event,
                                     SCREEN_PROPERTY_SOURCE_POSITION,
                                     source_position) == 0) {
        raw.source_x = source_position[0];
        raw.source_y = source_position[1];
        raw.source_valid = 1u;
    }
    if (screen_get_event_property_iv(g_qnx.event, SCREEN_PROPERTY_BUTTONS,
                                     &value) == 0) {
        raw.buttons = (uint32_t)value;
        raw.buttons_valid = 1u;
    }
    if (raw.type != OPEN_NAVI_SCREEN_RAW_POINTER &&
        screen_get_event_property_iv(g_qnx.event, SCREEN_PROPERTY_TOUCH_ID,
                                     &value) == 0) {
        raw.touch_id = (uint32_t)value;
        raw.touch_id_valid = 1u;
    }
    {
        int decoded;
        g_qnx.input.multi_contact = contacts != 0 ? 1u : 0u;
        decoded = open_navi_screen_input_decode(&g_qnx.input, &raw, pointer_out);
        input_trace("type=%d own=%d pos=%d,%d src=%d,%d btn=%u id=%u decoded=%d "
                    "action=%u out=%d,%d acc=%u foreign=%u dup=%u sec=%u drop=%u oob=%u ign=%u",
                    event_type, raw.window == (uintptr_t)g_qnx.window,
                    raw.position_x, raw.position_y, raw.source_x, raw.source_y,
                    raw.buttons, raw.touch_id, decoded,
                    decoded == 1 ? pointer_out->action : 0u,
                    decoded == 1 ? pointer_out->x : -1, decoded == 1 ? pointer_out->y : -1,
                    g_qnx.input.accepted, g_qnx.input.foreign_window,
                    g_qnx.input.duplicate_class, g_qnx.input.secondary_touch,
                    g_qnx.input.coordinate_drops, g_qnx.input.out_of_bounds,
                    g_qnx.input.ignored);
        return open_navi_screen_input_platform_result(decoded);
    }
}
