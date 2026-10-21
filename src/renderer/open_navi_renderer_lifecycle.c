#define _POSIX_C_SOURCE 200112L

/* Renderer lifecycle: the render thread, backend start and stop, and the
 * create/destroy entry points. */

#include "open_navi_renderer_internal.h"

#include "open_navi_default_backend.h"
#include "open_navi_renderer_platform.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static pthread_once_t g_condition_once = PTHREAD_ONCE_INIT;
static int g_condition_init_result = -1;

#if defined(__GNUC__)
extern uint32_t open_navi_service_publish_cluster(
    const OpenNaviBackendClusterV1 *cluster) __attribute__((weak));
extern uint32_t open_navi_service_publish_audio_availability(
    const OpenNaviBackendAudioAvailabilityV1 *audio) __attribute__((weak));
extern uint32_t open_navi_service_publish_thread_status(
    const OpenNaviBackendThreadStatusV1 *thread_status) __attribute__((weak));
extern uint32_t open_navi_service_publish_guidance_state(
    const OpenNaviBackendGuidanceStateV1 *guidance) __attribute__((weak));
#else
extern uint32_t open_navi_service_publish_cluster(
    const OpenNaviBackendClusterV1 *cluster);
extern uint32_t open_navi_service_publish_audio_availability(
    const OpenNaviBackendAudioAvailabilityV1 *audio);
extern uint32_t open_navi_service_publish_thread_status(
    const OpenNaviBackendThreadStatusV1 *thread_status);
extern uint32_t open_navi_service_publish_guidance_state(
    const OpenNaviBackendGuidanceStateV1 *guidance);
#endif

static void initialize_condition(void)
{
    pthread_condattr_t attributes;
    int attributes_initialized = 0;
    int result = pthread_condattr_init(&attributes);
    if (result == 0) {
        attributes_initialized = 1;
        result = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    }
    if (result == 0)
        result = pthread_cond_init(&g_runtime.condition, &attributes);
    if (attributes_initialized != 0)
        (void)pthread_condattr_destroy(&attributes);
    g_condition_init_result = result;
}

static int ensure_condition(void)
{
    int result = pthread_once(&g_condition_once, initialize_condition);
    return result == 0 ? g_condition_init_result : result;
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

static uintptr_t next_service_token_locked(void)
{
    if (g_runtime.next_service_token == UINTPTR_MAX)
        return (uintptr_t)0u;
    ++g_runtime.next_service_token;
    return g_runtime.next_service_token;
}

static int outbound_service_current(void *runtime_context)
{
    uintptr_t token = (uintptr_t)runtime_context;
    int current;

    (void)pthread_mutex_lock(&g_runtime.lock);
    current = token != (uintptr_t)0u &&
              token == g_runtime.active_service_token &&
              g_runtime.outbound_services_active != 0 &&
              (g_runtime.state == RENDERER_STARTING ||
               g_runtime.state == RENDERER_RUNNING) &&
              g_runtime.stop == 0;
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return current;
}

static uint32_t service_publish_cluster(
    void *runtime_context, const OpenNaviBackendClusterV1 *cluster)
{
    if (!outbound_service_current(runtime_context))
        return OPEN_NAVI_BACKEND_SERVICE_STALE;
    if (open_navi_service_publish_cluster == NULL)
        return OPEN_NAVI_BACKEND_SERVICE_UNAVAILABLE;
    return open_navi_service_publish_cluster(cluster);
}

static uint32_t service_publish_audio_availability(
    void *runtime_context, const OpenNaviBackendAudioAvailabilityV1 *audio)
{
    if (!outbound_service_current(runtime_context))
        return OPEN_NAVI_BACKEND_SERVICE_STALE;
    if (open_navi_service_publish_audio_availability == NULL)
        return OPEN_NAVI_BACKEND_SERVICE_UNAVAILABLE;
    return open_navi_service_publish_audio_availability(audio);
}

static uint32_t service_publish_thread_status(
    void *runtime_context, const OpenNaviBackendThreadStatusV1 *thread_status)
{
    if (!outbound_service_current(runtime_context))
        return OPEN_NAVI_BACKEND_SERVICE_STALE;
    if (open_navi_service_publish_thread_status == NULL)
        return OPEN_NAVI_BACKEND_SERVICE_UNAVAILABLE;
    return open_navi_service_publish_thread_status(thread_status);
}

static uint32_t service_publish_guidance_state(
    void *runtime_context, const OpenNaviBackendGuidanceStateV1 *guidance)
{
    if (!outbound_service_current(runtime_context))
        return OPEN_NAVI_BACKEND_SERVICE_STALE;
    if (open_navi_service_publish_guidance_state == NULL)
        return OPEN_NAVI_BACKEND_SERVICE_UNAVAILABLE;
    return open_navi_service_publish_guidance_state(guidance);
}

static int stop_requested(void)
{
    int stop;
    (void)pthread_mutex_lock(&g_runtime.lock);
    stop = g_runtime.stop;
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return stop;
}

static void renderer_cleanup(int started, int surfaced)
{
    OpenNaviBackendV1 backend = g_runtime.backend.active;
    if (surfaced != 0 && backend.surface_destroyed != NULL) {
        char diagnostic[257];
        diagnostic[0] = '\0';
        int context_current = open_navi_renderer_platform_begin_frame(
            diagnostic, sizeof(diagnostic)) == 0;
        backend.surface_destroyed(backend.backend_context);
        if (context_current != 0) {
            (void)open_navi_renderer_platform_end_frame(
                0, diagnostic, sizeof(diagnostic));
        }
    }
    if (started != 0 && backend.stop != NULL)
        backend.stop(backend.backend_context);
    open_navi_renderer_platform_destroy();
}

static void clear_pending_notifications_locked(void)
{
    unsigned int display;
    for (display = 0u; display < OPEN_NAVI_RENDERER_DISPLAY_COUNT; ++display) {
        g_runtime.visibility[display].pending = 0;
        g_runtime.screen[display].pending = 0;
    }
    g_runtime.gps.pending = 0;
    memset(&g_runtime.vehicle, 0, sizeof(g_runtime.vehicle));
    memset(&g_runtime.gps_interval, 0, sizeof(g_runtime.gps_interval));
    memset(&g_runtime.audio, 0, sizeof(g_runtime.audio));
    memset(&g_runtime.internationalization, 0,
           sizeof(g_runtime.internationalization));
    memset(&g_runtime.cluster_display, 0,
           sizeof(g_runtime.cluster_display));
    memset(&g_runtime.driver_distraction, 0,
           sizeof(g_runtime.driver_distraction));
    memset(&g_runtime.guidance_stop, 0,
           sizeof(g_runtime.guidance_stop));
    g_runtime.pointer.head = 0u;
    g_runtime.pointer.count = 0u;
}

/* Outbound services (V3 and later) are offered to the backend from start. */
static int active_abi_has_outbound_services(void)
{
    return active_abi_at_least(OPEN_NAVI_BACKEND_ABI_V3);
}

/* Creates the platform target and starts the backend with the services
 * table matching its ABI. *started is set once the backend is running. */
static int start_backend(uintptr_t service_token, int *started)
{
    OpenNaviBackendServicesV1 services;
    OpenNaviBackendServicesV2 services_v2;
    OpenNaviBackendServicesV3 services_v3;
    int status;

    memset(&services, 0, sizeof(services));
    services.abi_version = OPEN_NAVI_BACKEND_ABI_V1;
    services.struct_size = sizeof(services);
    services.runtime_context = (void *)service_token;
    services.request_frame = open_navi_renderer_request_frame;
    memset(&services_v2, 0, sizeof(services_v2));
    services_v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V2;
    services_v2.v1.struct_size = sizeof(services_v2);
    services_v2.v1.runtime_context = (void *)service_token;
    services_v2.v1.request_frame = open_navi_renderer_request_frame;
    services_v2.publish_cluster = service_publish_cluster;
    services_v2.publish_audio_availability =
        service_publish_audio_availability;
    services_v2.publish_thread_status = service_publish_thread_status;

    services_v3.v2 = services_v2;
    services_v3.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V3;
    services_v3.v2.v1.struct_size = sizeof(services_v3);
    services_v3.publish_guidance_state = service_publish_guidance_state;

    status = open_navi_renderer_platform_create(&g_runtime.target,
                                           g_runtime.diagnostic,
                                           sizeof(g_runtime.diagnostic));
    if (status == 0 &&
        g_runtime.backend.active_version == OPEN_NAVI_BACKEND_ABI_V6) {
        status = g_runtime.backend.active_v6.start_v3(
            g_runtime.backend.active.backend_context, &services_v3);
        if (status == 0) *started = 1;
    } else if (status == 0 &&
        active_abi_at_least(OPEN_NAVI_BACKEND_ABI_V3)) {
        status = g_runtime.backend.active_v3.start_v2(
            g_runtime.backend.active.backend_context, &services_v2);
        if (status == 0) *started = 1;
    } else if (status == 0 && g_runtime.backend.active.start != NULL) {
        status = g_runtime.backend.active.start(g_runtime.backend.active.backend_context,
                                        &services);
        if (status == 0) *started = 1;
    } else if (status == 0) {
        *started = 1;
    }
    return status;
}

static void enable_outbound_services_while_starting(void)
{
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state == RENDERER_STARTING && g_runtime.stop == 0)
        g_runtime.outbound_services_active = 1;
    (void)pthread_mutex_unlock(&g_runtime.lock);
}

/* Gives the backend its drawing surface inside a platform frame. *surfaced
 * is set once the backend holds the surface. */
static int create_backend_surface(int *surfaced)
{
    int status = 0;

    if (g_runtime.backend.active.surface_created != NULL) {
        char diagnostic[257];
        diagnostic[0] = '\0';
        if (open_navi_renderer_platform_begin_frame(
                diagnostic, sizeof(diagnostic)) != 0) {
            status = -1;
        } else {
            status = g_runtime.backend.active.surface_created(
                g_runtime.backend.active.backend_context, &g_runtime.target);
            (void)open_navi_renderer_platform_end_frame(
                0, diagnostic, sizeof(diagnostic));
            if (status == 0) *surfaced = 1;
        }
    } else {
        *surfaced = 1;
    }
    return status;
}

/* Moves to RUNNING, or to stopping when startup failed or was cancelled,
 * then wakes whoever waits for the start result. Returns the final status. */
static int publish_start_result(int status)
{
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (status == 0 && g_runtime.stop == 0 &&
        g_runtime.state == RENDERER_STARTING) {
        g_runtime.state = RENDERER_RUNNING;
        g_runtime.outbound_services_active =
            active_abi_has_outbound_services();
    } else {
        if (status == 0) status = OPEN_NAVI_RENDERER_CANCELLED;
        g_runtime.stop = 1;
        g_runtime.active_service_token = (uintptr_t)0u;
        g_runtime.outbound_services_active = 0;
        if (g_runtime.state != RENDERER_COLD)
            g_runtime.state = RENDERER_STOPPING;
    }
    g_runtime.start_result = status;
    g_runtime.startup_done = 1;
    (void)pthread_cond_broadcast(&g_runtime.condition);
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return status;
}

/* Runs until stop is requested (0) or the queue or platform fails (-1). */
static int run_event_loop(uintptr_t service_token)
{
    RenderEvent event;

    for (;;) {
        int drain_result;
        int take_result;
        drain_result = open_navi_renderer_platform_input_pending()
                           ? open_navi_renderer_drain_platform_pointer() : 0;
        if (drain_result < 0) return -1;
        /* A full bounded drain can be followed by another full batch forever.
         * Observe teardown between every batch rather than relying on a
         * condition wait that a perpetual unrelated-event flood bypasses. */
        if (stop_requested()) return 0;
        if (drain_result > 0) {
            int available;
            (void)pthread_mutex_lock(&g_runtime.lock);
            available = open_navi_renderer_event_available_locked();
            (void)pthread_mutex_unlock(&g_runtime.lock);
            if (!available) continue;
        }
        take_result = open_navi_renderer_take_event(&event);
        if (take_result == 0) return 0;
        if (take_result < 0) return -1;
        if (take_result == 1) open_navi_renderer_dispatch_event(&event, service_token);
    }
}

/* After a failure or cancel: stop the renderer, stop accepting input, drop
 * the service token and everything still queued. */
static void abort_running_state(void)
{
    (void)pthread_mutex_lock(&g_runtime.lock);
    g_runtime.accepting_state = 0;
    g_runtime.stop = 1;
    g_runtime.active_service_token = (uintptr_t)0u;
    g_runtime.outbound_services_active = 0;
    clear_pending_notifications_locked();
    g_runtime.frame_pending = 0;
    if (g_runtime.state != RENDERER_COLD)
        g_runtime.state = RENDERER_STOPPING;
    (void)pthread_mutex_unlock(&g_runtime.lock);
}

static void return_to_cold_state(void)
{
    (void)pthread_mutex_lock(&g_runtime.lock);
    g_runtime.active_service_token = (uintptr_t)0u;
    g_runtime.outbound_services_active = 0;
    g_runtime.state = RENDERER_COLD;
    if (g_runtime.thread_detached != 0) {
        g_runtime.thread_created = 0;
        g_runtime.join_claimed = 0;
        g_runtime.thread_detached = 0;
    }
    g_runtime.stop = 0;
    memset(&g_runtime.target, 0, sizeof(g_runtime.target));
    (void)pthread_cond_broadcast(&g_runtime.condition);
    (void)pthread_mutex_unlock(&g_runtime.lock);
}

static void *renderer_thread(void *unused)
{
    uintptr_t service_token;
    int status;
    int started = 0;
    int surfaced = 0;
    (void)unused;

    (void)pthread_mutex_lock(&g_runtime.lock);
    service_token = g_runtime.active_service_token;
    (void)pthread_mutex_unlock(&g_runtime.lock);

    status = start_backend(service_token, &started);
    if (status == 0 && active_abi_has_outbound_services())
        enable_outbound_services_while_starting();
    if (status == 0 && stop_requested()) status = OPEN_NAVI_RENDERER_CANCELLED;

    if (status == 0) status = create_backend_surface(&surfaced);
    if (status == 0 && stop_requested()) status = OPEN_NAVI_RENDERER_CANCELLED;

    status = publish_start_result(status);
    if (status == 0) {
        (void)open_navi_renderer_request_frame((void *)service_token);
        status = run_event_loop(service_token);
    }
    if (status != 0) abort_running_state();
    renderer_cleanup(started, surfaced);
    return_to_cold_state();
    return NULL;
}

int open_navi_renderer_set_window_area(int enabled)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state != RENDERER_COLD)
        result = -1;
    else
        g_runtime.window_area = enabled != 0;
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_renderer_set_pointer_contacts(int enabled)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state != RENDERER_COLD)
        result = -1;
    else
        g_runtime.pointer_contacts = enabled != 0;
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

void open_navi_renderer_prepare_lifecycle(void)
{
    if (ensure_condition() != 0) return;
    (void)pthread_mutex_lock(&g_runtime.lock);
    g_runtime.accepting_state = 1;
    clear_pending_notifications_locked();
    g_runtime.pointer.accepted = 0u;
    g_runtime.pointer.dropped = 0u;
    g_runtime.pointer.move_coalesced = 0u;
    g_runtime.pointer.move_dropped = 0u;
    g_runtime.pointer.edge_dropped = 0u;
    g_runtime.pointer.move_evicted = 0u;
    g_runtime.pointer.gesture_pairs_evicted = 0u;
    g_runtime.counters.platform_ignored = 0u;
    g_runtime.counters.platform_errors = 0u;
    g_runtime.counters.platform_window_lost = 0u;
    g_runtime.counters.visibility_unsupported = 0u;
    g_runtime.counters.screen_unsupported = 0u;
    g_runtime.frame_pending = 0;
    (void)pthread_mutex_unlock(&g_runtime.lock);
}

void open_navi_renderer_end_lifecycle(void)
{
    (void)pthread_mutex_lock(&g_runtime.lock);
    g_runtime.accepting_state = 0;
    clear_pending_notifications_locked();
    g_runtime.frame_pending = 0;
    (void)pthread_mutex_unlock(&g_runtime.lock);
}

int open_navi_renderer_create(char *diagnostic, size_t diagnostic_capacity)
{
    int create_result;
    int start_result;
    if (ensure_condition() != 0) {
        set_diagnostic(diagnostic, diagnostic_capacity,
                       "renderer condition initialization failed");
        return -1;
    }
    (void)pthread_mutex_lock(&g_runtime.lock);
    while (g_runtime.state == RENDERER_STOPPING ||
           (g_runtime.state == RENDERER_COLD &&
            g_runtime.thread_created != 0))
        (void)pthread_cond_wait(&g_runtime.condition, &g_runtime.lock);
    if (g_runtime.state == RENDERER_RUNNING) {
        set_diagnostic(diagnostic, diagnostic_capacity,
                       "backend renderer already live");
        (void)pthread_mutex_unlock(&g_runtime.lock);
        return 0;
    }
    if (g_runtime.state != RENDERER_COLD) {
        (void)pthread_mutex_unlock(&g_runtime.lock);
        return -1;
    }
    if (g_runtime.accepting_state == 0) {
        g_runtime.accepting_state = 1;
        clear_pending_notifications_locked();
    }
    g_runtime.state = RENDERER_STARTING;
    g_runtime.stop = 0;
    g_runtime.startup_done = 0;
    g_runtime.join_claimed = 0;
    g_runtime.thread_detached = 0;
    g_runtime.start_result = -1;
    g_runtime.frame_sequence = 0u;
    g_runtime.frame_pending = 0;
    g_runtime.diagnostic[0] = '\0';
    memset(&g_runtime.backend.active_v2, 0, sizeof(g_runtime.backend.active_v2));
    memset(&g_runtime.backend.active_v3, 0, sizeof(g_runtime.backend.active_v3));
    memset(&g_runtime.backend.active_v4, 0, sizeof(g_runtime.backend.active_v4));
    memset(&g_runtime.backend.active_v5, 0, sizeof(g_runtime.backend.active_v5));
    memset(&g_runtime.backend.active_v6, 0, sizeof(g_runtime.backend.active_v6));
    g_runtime.outbound_services_active = 0;
    open_navi_renderer_activate_installed_locked();
    g_runtime.active_service_token = next_service_token_locked();
    if (g_runtime.active_service_token == (uintptr_t)0u) {
        g_runtime.state = RENDERER_COLD;
        set_diagnostic(diagnostic, diagnostic_capacity,
                       "renderer service generation exhausted");
        (void)pthread_mutex_unlock(&g_runtime.lock);
        return -1;
    }
    create_result = pthread_create(&g_runtime.thread, NULL,
                                   renderer_thread, NULL);
    if (create_result != 0) {
        g_runtime.active_service_token = (uintptr_t)0u;
        g_runtime.outbound_services_active = 0;
        g_runtime.state = RENDERER_COLD;
        set_diagnostic(diagnostic, diagnostic_capacity,
                       "renderer thread create failed=%d", create_result);
        (void)pthread_mutex_unlock(&g_runtime.lock);
        return -1;
    }
    g_runtime.thread_created = 1;
    while (g_runtime.startup_done == 0)
        (void)pthread_cond_wait(&g_runtime.condition, &g_runtime.lock);
    start_result = g_runtime.start_result;
    set_diagnostic(diagnostic, diagnostic_capacity, "%s",
                   g_runtime.diagnostic[0] != '\0'
                       ? g_runtime.diagnostic : "backend renderer live");
    (void)pthread_mutex_unlock(&g_runtime.lock);
    if (start_result != 0) {
        open_navi_renderer_destroy();
        return start_result;
    }
    return 0;
}

void open_navi_renderer_destroy(void)
{
    pthread_t thread;
    int join = 0;
    int self;

    if (ensure_condition() != 0) return;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state == RENDERER_COLD &&
        g_runtime.thread_created == 0) {
        (void)pthread_mutex_unlock(&g_runtime.lock);
        return;
    }
    thread = g_runtime.thread;
    self = g_runtime.thread_created != 0 &&
           pthread_equal(pthread_self(), thread);
    if (g_runtime.state != RENDERER_COLD) {
        g_runtime.stop = 1;
        g_runtime.active_service_token = (uintptr_t)0u;
        g_runtime.outbound_services_active = 0;
        g_runtime.state = RENDERER_STOPPING;
        g_runtime.pointer.head = 0u;
        g_runtime.pointer.count = 0u;
        g_runtime.frame_pending = 0;
    }
    (void)pthread_cond_broadcast(&g_runtime.condition);

    /* This check is deliberately before join ownership/waiting. Cleanup
     * callbacks may reenter after an external thread already claimed join. */
    if (self != 0) {
        if (g_runtime.join_claimed == 0) {
            g_runtime.join_claimed = 1;
            g_runtime.thread_detached = 1;
            (void)pthread_detach(thread);
        }
        (void)pthread_mutex_unlock(&g_runtime.lock);
        return;
    }
    if (g_runtime.thread_created != 0 && g_runtime.join_claimed == 0) {
        g_runtime.join_claimed = 1;
        join = 1;
    } else {
        while (g_runtime.state != RENDERER_COLD)
            (void)pthread_cond_wait(&g_runtime.condition, &g_runtime.lock);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);

    if (join != 0) {
        (void)pthread_join(thread, NULL);
        (void)pthread_mutex_lock(&g_runtime.lock);
        g_runtime.thread_created = 0;
        g_runtime.join_claimed = 0;
        (void)pthread_cond_broadcast(&g_runtime.condition);
        (void)pthread_mutex_unlock(&g_runtime.lock);
    }
}
