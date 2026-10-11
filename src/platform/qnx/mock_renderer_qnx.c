#define _POSIX_C_SOURCE 200112L

#include "mock_renderer_platform.h"
#include "mock_screen_input.h"

#include <EGL/egl.h>
#include <screen/screen.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MOCK_PRESENT_PACING_NANOSECONDS UINT64_C(33333333)
#define MOCK_WINDOW_BUFFER_COUNT 3

typedef struct {
    screen_context_t screen_context;
    screen_window_t window;
    screen_event_t event;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    MockScreenInputState input;
    uint64_t next_present_nanoseconds;
    int asynchronous_swap;
    uint32_t ignored_events;
    uint32_t event_errors;
    uint32_t window_lost;
} QnxRenderer;

static QnxRenderer g_qnx;

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
    if (now == 0u) return;
    if (g_qnx.next_present_nanoseconds == 0u ||
        now > g_qnx.next_present_nanoseconds +
              MOCK_PRESENT_PACING_NANOSECONDS) {
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
    g_qnx.next_present_nanoseconds += MOCK_PRESENT_PACING_NANOSECONDS;
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

void mock_renderer_platform_destroy(void)
{
    if (g_qnx.display != EGL_NO_DISPLAY) {
        (void)eglMakeCurrent(g_qnx.display, EGL_NO_SURFACE,
                             EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_qnx.surface != EGL_NO_SURFACE)
            (void)eglDestroySurface(g_qnx.display, g_qnx.surface);
        if (g_qnx.context != EGL_NO_CONTEXT)
            (void)eglDestroyContext(g_qnx.display, g_qnx.context);
    }
    if (g_qnx.event != NULL) (void)screen_destroy_event(g_qnx.event);
    if (g_qnx.window != NULL) (void)screen_destroy_window(g_qnx.window);
    if (g_qnx.screen_context != NULL)
        (void)screen_destroy_context(g_qnx.screen_context);
    /* EGLDisplay is process-global on this stack; do not eglTerminate it. */
    memset(&g_qnx, 0, sizeof(g_qnx));
    g_qnx.display = EGL_NO_DISPLAY;
    g_qnx.surface = EGL_NO_SURFACE;
    g_qnx.context = EGL_NO_CONTEXT;
}

int mock_renderer_platform_create(MockNavBackendRenderTargetV1 *target,
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
    int window_position[2] = { 0, 0 };
    int window_size[2] = { 0, 0 };
    int source_position[2] = { 0, 0 };
    int source_size[2] = { 0, 0 };
    int rotation = 0;
    int sensitivity = SCREEN_SENSITIVITY_TEST;
    int screen_swap_interval = -1;
    int buffer_count = -1;
    int render_buffer_count = -1;
    int pipeline = -1;
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
        screen_create_window_buffers(g_qnx.window,
                                     MOCK_WINDOW_BUFFER_COUNT) != 0 ||
        screen_create_event(&g_qnx.event) != 0) {
        (void)screen_error(diagnostic, capacity, "NAV_main window creation");
        goto fail;
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
    memset(target, 0, sizeof(*target));
    target->abi_version = MOCK_NAV_BACKEND_ABI_V1;
    target->struct_size = sizeof(*target);
    target->width = size[0] > 0 ? (uint32_t)size[0] : 800u;
    target->height = size[1] > 0 ? (uint32_t)size[1] : 480u;
    mock_screen_input_reset(&g_qnx.input, (uintptr_t)g_qnx.window,
                            identity_coordinates, target->width,
                            target->height);
    target->native_pointer_bytes = sizeof(void *);
    target->native_screen_window = (uintptr_t)g_qnx.window;
    target->native_egl_display = (uintptr_t)g_qnx.display;
    target->native_egl_surface = (uintptr_t)g_qnx.surface;
    target->native_egl_context = (uintptr_t)g_qnx.context;
    set_diagnostic(diagnostic, capacity,
                   "backend renderer live: class=NAV_main config=0x%x "
                   "requested_buffers=%d geometry=%ux%u input_identity=%d "
                   "sensitivity=%d "
                   "swap_range=%d..%d swap0=%d screen_swap=%d "
                   "buffer_count=%d render_buffers=%d pipeline=%d "
                   "pace_interval_us=33333 display_goal_fps=30 "
                   "persistent_context=1",
                   (unsigned int)config_id, MOCK_WINDOW_BUFFER_COUNT,
                   target->width, target->height,
                   identity_coordinates, sensitivity, min_swap_interval,
                   max_swap_interval, g_qnx.asynchronous_swap,
                   screen_swap_interval, buffer_count, render_buffer_count,
                   pipeline);
    return 0;
fail:
    mock_renderer_platform_destroy();
    return -1;
}

int mock_renderer_platform_begin_frame(char *diagnostic, size_t capacity)
{
    if (eglGetCurrentContext() == g_qnx.context &&
        eglGetCurrentSurface(EGL_DRAW) == g_qnx.surface &&
        eglGetCurrentSurface(EGL_READ) == g_qnx.surface)
        return 0;
    if (eglMakeCurrent(g_qnx.display, g_qnx.surface, g_qnx.surface,
                       g_qnx.context) != EGL_TRUE)
        return egl_error(diagnostic, capacity, "eglMakeCurrent");
    return 0;
}

int mock_renderer_platform_end_frame(int present, char *diagnostic,
                                     size_t capacity)
{
    int result = 0;
    if (present != 0) {
        pace_presentation();
        if (eglSwapBuffers(g_qnx.display, g_qnx.surface) != EGL_TRUE)
            result = egl_error(diagnostic, capacity, "eglSwapBuffers");
    }
    return result;
}

static void increment_counter(uint32_t *counter)
{
    if (*counter != UINT32_MAX) ++*counter;
}

int mock_renderer_platform_poll_pointer(MockNavBackendPointerV1 *pointer_out,
                                        char *diagnostic, size_t capacity)
{
    MockScreenRawEvent raw;
    screen_window_t event_window = NULL;
    int event_type = SCREEN_EVENT_NONE;
    int position[2];
    int source_position[2];
    int value;

    if (pointer_out == NULL || g_qnx.screen_context == NULL ||
        g_qnx.event == NULL) return -1;
    while (screen_get_event(g_qnx.screen_context, g_qnx.event, 0u) != 0) {
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
        raw.type = MOCK_SCREEN_RAW_POINTER;
    else if (event_type == SCREEN_EVENT_MTOUCH_TOUCH)
        raw.type = MOCK_SCREEN_RAW_TOUCH;
    else if (event_type == SCREEN_EVENT_MTOUCH_MOVE)
        raw.type = MOCK_SCREEN_RAW_MOVE;
    else if (event_type == SCREEN_EVENT_MTOUCH_RELEASE)
        raw.type = MOCK_SCREEN_RAW_RELEASE;
    else {
        increment_counter(&g_qnx.ignored_events);
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
    if (raw.type != MOCK_SCREEN_RAW_POINTER &&
        screen_get_event_property_iv(g_qnx.event, SCREEN_PROPERTY_TOUCH_ID,
                                     &value) == 0) {
        raw.touch_id = (uint32_t)value;
        raw.touch_id_valid = 1u;
    }
    return mock_screen_input_platform_result(
        mock_screen_input_decode(&g_qnx.input, &raw, pointer_out));
}
