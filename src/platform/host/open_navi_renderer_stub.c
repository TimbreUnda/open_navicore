#include "open_navi_renderer.h"
#include "open_navi_renderer_platform.h"
#include "open_navi_screen_input.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

enum { OPEN_NAVI_STUB_INPUT_CAPACITY = 128 };

static unsigned int g_create_count;
static unsigned int g_destroy_count;
static int g_live;
static int g_create_result;
static int g_begin_result;
static int g_context_current;
static pthread_mutex_t g_input_lock = PTHREAD_MUTEX_INITIALIZER;
static OpenNaviScreenRawEvent g_input_queue[OPEN_NAVI_STUB_INPUT_CAPACITY];
static unsigned int g_input_head;
static unsigned int g_input_count;
static int g_identity_coordinates = 1;
static int g_forced_poll_result;
static int g_repeating_poll_result;
static OpenNaviScreenInputState g_input_state;
static const uintptr_t g_input_window = (uintptr_t)0x4e41564du;

#if defined(__GNUC__)
__attribute__((weak))
#endif
void open_navi_renderer_test_observe(enum open_navi_renderer_test_event event)
{
    (void)event;
}

int open_navi_renderer_platform_create(OpenNaviBackendRenderTargetV1 *target,
                                  char *diagnostic,
                                  size_t diagnostic_capacity)
{
    ++g_create_count;
    open_navi_renderer_test_observe(OPEN_NAVI_RENDERER_TEST_CREATE);
    if (g_create_result != 0) {
        if (diagnostic != NULL && diagnostic_capacity != 0u)
            (void)snprintf(diagnostic, diagnostic_capacity,
                           "host stub forced failure=%d", g_create_result);
        return g_create_result;
    }
    memset(target, 0, sizeof(*target));
    target->abi_version = OPEN_NAVI_BACKEND_ABI_V1;
    target->struct_size = sizeof(*target);
    target->width = 800u;
    target->height = 480u;
    target->native_pointer_bytes = sizeof(void *);
    target->native_screen_window = g_input_window;
    (void)pthread_mutex_lock(&g_input_lock);
    open_navi_screen_input_reset(&g_input_state, g_input_window,
                            g_identity_coordinates, 800u, 480u);
    g_input_head = 0u;
    g_input_count = 0u;
    (void)pthread_mutex_unlock(&g_input_lock);
    g_live = 1;
    if (diagnostic != NULL && diagnostic_capacity != 0u)
        (void)snprintf(diagnostic, diagnostic_capacity,
                       "host stub: backend renderer created");
    return 0;
}

void open_navi_renderer_platform_destroy(void)
{
    g_context_current = 0;
    if (g_live != 0) {
        g_live = 0;
        ++g_destroy_count;
        open_navi_renderer_test_observe(OPEN_NAVI_RENDERER_TEST_DESTROY);
    }
}

int open_navi_renderer_platform_begin_frame(char *diagnostic,
                                       size_t diagnostic_capacity)
{
    (void)diagnostic;
    (void)diagnostic_capacity;
    if (g_live == 0 || g_begin_result != 0) return -1;
    g_context_current = 1;
    return 0;
}

int open_navi_renderer_platform_end_frame(int present, char *diagnostic,
                                     size_t diagnostic_capacity)
{
    (void)present;
    (void)diagnostic;
    (void)diagnostic_capacity;
    if (g_live == 0) return -1;
    g_context_current = 0;
    return 0;
}

int open_navi_renderer_platform_poll_pointer(OpenNaviBackendPointerV2 *pointer_out,
                                        int contacts,
                                        char *diagnostic,
                                        size_t diagnostic_capacity)
{
    OpenNaviScreenRawEvent raw;
    int result;
    (void)diagnostic;
    (void)diagnostic_capacity;
    (void)pthread_mutex_lock(&g_input_lock);
    if (g_forced_poll_result != 0) {
        result = g_forced_poll_result;
        g_forced_poll_result = 0;
        (void)pthread_mutex_unlock(&g_input_lock);
        return result;
    }
    if (g_repeating_poll_result != 0) {
        result = g_repeating_poll_result;
        (void)pthread_mutex_unlock(&g_input_lock);
        return result;
    }
    if (g_input_count == 0u) {
        (void)pthread_mutex_unlock(&g_input_lock);
        return 0;
    }
    raw = g_input_queue[g_input_head];
    g_input_head = (g_input_head + 1u) % OPEN_NAVI_STUB_INPUT_CAPACITY;
    --g_input_count;
    g_input_state.multi_contact = contacts != 0 ? 1u : 0u;
    result = open_navi_screen_input_platform_result(
        open_navi_screen_input_decode(&g_input_state, &raw, pointer_out));
    (void)pthread_mutex_unlock(&g_input_lock);
    return result;
}

unsigned int open_navi_renderer_stub_create_count(void) { return g_create_count; }
unsigned int open_navi_renderer_stub_destroy_count(void) { return g_destroy_count; }

void open_navi_renderer_stub_reset(void)
{
    g_create_count = 0u;
    g_destroy_count = 0u;
    g_live = 0;
    g_create_result = 0;
    g_begin_result = 0;
    g_context_current = 0;
    (void)pthread_mutex_lock(&g_input_lock);
    g_input_head = 0u;
    g_input_count = 0u;
    g_identity_coordinates = 1;
    g_forced_poll_result = 0;
    g_repeating_poll_result = 0;
    memset(&g_input_state, 0, sizeof(g_input_state));
    (void)pthread_mutex_unlock(&g_input_lock);
    (void)open_navi_backend_install_v1(NULL);
}

void open_navi_renderer_stub_set_create_result(int result)
{
    g_create_result = result;
}

void open_navi_renderer_stub_set_begin_result(int result)
{
    g_begin_result = result;
}

int open_navi_renderer_stub_context_current(void)
{
    return g_context_current;
}

int open_navi_renderer_stub_queue_raw_input(const OpenNaviRendererStubRawEvent *event)
{
    unsigned int tail;
    OpenNaviScreenRawEvent *raw;
    if (event == NULL) return -2;
    (void)pthread_mutex_lock(&g_input_lock);
    if (g_input_count == OPEN_NAVI_STUB_INPUT_CAPACITY) {
        (void)pthread_mutex_unlock(&g_input_lock);
        return -1;
    }
    tail = (g_input_head + g_input_count) % OPEN_NAVI_STUB_INPUT_CAPACITY;
    raw = &g_input_queue[tail];
    memset(raw, 0, sizeof(*raw));
    raw->type = event->type;
    raw->window = event->window;
    raw->position_x = event->position_x;
    raw->position_y = event->position_y;
    raw->source_x = event->source_x;
    raw->source_y = event->source_y;
    raw->buttons = event->buttons;
    raw->touch_id = event->touch_id;
    raw->position_valid = event->position_valid;
    raw->source_valid = event->source_valid;
    raw->buttons_valid = event->buttons_valid;
    raw->touch_id_valid = event->touch_id_valid;
    ++g_input_count;
    (void)pthread_mutex_unlock(&g_input_lock);
    return 0;
}

uintptr_t open_navi_renderer_stub_input_window(void)
{
    return g_input_window;
}

void open_navi_renderer_stub_set_identity_coordinates(int enabled)
{
    (void)pthread_mutex_lock(&g_input_lock);
    g_identity_coordinates = enabled != 0;
    g_input_state.identity_coordinates = enabled != 0;
    (void)pthread_mutex_unlock(&g_input_lock);
}

void open_navi_renderer_stub_set_poll_result(int result)
{
    (void)pthread_mutex_lock(&g_input_lock);
    g_forced_poll_result = result;
    (void)pthread_mutex_unlock(&g_input_lock);
}

void open_navi_renderer_stub_set_repeating_poll_result(int result)
{
    (void)pthread_mutex_lock(&g_input_lock);
    g_repeating_poll_result = result;
    (void)pthread_mutex_unlock(&g_input_lock);
}

void open_navi_renderer_stub_input_stats(OpenNaviRendererStubInputStats *stats_out)
{
    if (stats_out == NULL) return;
    (void)pthread_mutex_lock(&g_input_lock);
    stats_out->accepted = g_input_state.accepted;
    stats_out->foreign_window = g_input_state.foreign_window;
    stats_out->secondary_touch = g_input_state.secondary_touch;
    stats_out->property_errors = g_input_state.property_errors;
    stats_out->coordinate_drops = g_input_state.coordinate_drops;
    stats_out->out_of_bounds = g_input_state.out_of_bounds;
    stats_out->ignored = g_input_state.ignored;
    stats_out->duplicate_class = g_input_state.duplicate_class;
    (void)pthread_mutex_unlock(&g_input_lock);
}
