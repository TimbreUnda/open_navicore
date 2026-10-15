#define _POSIX_C_SOURCE 200809L

#include "open_navi_gps_backend.h"
#include <open_navicore/backend.h>
#include "open_navi_renderer.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

enum CallbackPhase {
    PHASE_NONE = 0,
    PHASE_START = 1,
    PHASE_SURFACE_CREATED = 2,
    PHASE_RENDER = 3,
    PHASE_VISIBILITY = 4,
    PHASE_POINTER = 5,
    PHASE_VEHICLE = 6,
    PHASE_GPS_INTERVAL = 7,
    PHASE_AUDIO = 8,
    PHASE_SURFACE_DESTROYED = 9,
    PHASE_STOP = 10,
    PHASE_COUNT = 11
};

/* CI and instrumented runs can pause one renderer thread for several seconds
 * while the waiter remains runnable.  Keep this a hang detector rather than
 * a scheduler-latency assertion. */
enum { STRESS_DEADLINE_SECONDS = 30 };

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    enum CallbackPhase destroy_phase;
    enum CallbackPhase block_phase;
    int block_entered;
    int block_release;
    int expect_surface_destroyed_current;
    uint32_t calls[PHASE_COUNT];
    uint32_t screen_calls;
    uint32_t gps_calls;
    uint32_t pointer_x[64];
    uint32_t pointer_actions[64];
    uint32_t pointer_visibility_seen[64];
    uint32_t pointer_count;
    uint32_t vehicle_calls;
    uint32_t gps_interval_calls;
    uint32_t audio_calls;
    OpenNaviBackendVisibilityV1 visibility[OPEN_NAVI_RENDERER_DISPLAY_COUNT];
    OpenNaviBackendScreenV1 screen[OPEN_NAVI_RENDERER_DISPLAY_COUNT];
    uint32_t visibility_per_display[OPEN_NAVI_RENDERER_DISPLAY_COUNT];
    uint32_t screen_per_display[OPEN_NAVI_RENDERER_DISPLAY_COUNT];
    uint32_t state_order[32];
    uint32_t state_order_count;
    OpenNaviBackendGpsV1 gps;
    OpenNaviBackendVehicleV2 vehicle;
    OpenNaviBackendGpsIntervalV2 gps_interval;
    OpenNaviBackendAudioV2 audio;
    OpenNaviBackendServicesV1 services;
} StressBackend;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    int create;
    int done;
    int result;
} TimedOperation;

static void deadline_after(struct timespec *deadline, unsigned int seconds)
{
    assert(clock_gettime(CLOCK_REALTIME, deadline) == 0);
    deadline->tv_sec += (time_t)seconds;
}

static void backend_init(StressBackend *backend)
{
    memset(backend, 0, sizeof(*backend));
    assert(pthread_mutex_init(&backend->lock, NULL) == 0);
    assert(pthread_cond_init(&backend->condition, NULL) == 0);
    backend->expect_surface_destroyed_current = 1;
}

static void callback_enter(StressBackend *backend, enum CallbackPhase phase)
{
    int destroy;
    (void)pthread_mutex_lock(&backend->lock);
    ++backend->calls[phase];
    (void)pthread_cond_broadcast(&backend->condition);
    if (backend->block_phase == phase) {
        backend->block_entered = 1;
        (void)pthread_cond_broadcast(&backend->condition);
        while (backend->block_release == 0)
            (void)pthread_cond_wait(&backend->condition, &backend->lock);
    }
    destroy = backend->destroy_phase == phase;
    (void)pthread_mutex_unlock(&backend->lock);
    if (destroy != 0) open_navi_renderer_destroy();
}

static int backend_start(void *context,
                         const OpenNaviBackendServicesV1 *services)
{
    StressBackend *backend = (StressBackend *)context;
    backend->services = *services;
    assert(services->runtime_context != NULL);
    assert(open_navi_renderer_stub_context_current() == 0);
    assert(services->request_frame(services->runtime_context) == 0);
    callback_enter(backend, PHASE_START);
    return 0;
}

static void backend_stop(void *context)
{
    assert(open_navi_renderer_stub_context_current() == 0);
    callback_enter((StressBackend *)context, PHASE_STOP);
}

static int backend_surface_created(
    void *context, const OpenNaviBackendRenderTargetV1 *target)
{
    assert(target->width == 800u && target->height == 480u);
    assert(open_navi_renderer_stub_context_current() != 0);
    callback_enter((StressBackend *)context, PHASE_SURFACE_CREATED);
    return 0;
}

static void backend_surface_destroyed(void *context)
{
    StressBackend *backend = (StressBackend *)context;
    assert((open_navi_renderer_stub_context_current() != 0) ==
           backend->expect_surface_destroyed_current);
    callback_enter(backend, PHASE_SURFACE_DESTROYED);
}

static int backend_visibility(
    void *context, const OpenNaviBackendVisibilityV1 *visibility)
{
    StressBackend *backend = (StressBackend *)context;
    assert(visibility->display < OPEN_NAVI_RENDERER_DISPLAY_COUNT);
    (void)pthread_mutex_lock(&backend->lock);
    backend->visibility[visibility->display] = *visibility;
    ++backend->visibility_per_display[visibility->display];
    assert(backend->state_order_count < 16u);
    backend->state_order[backend->state_order_count++] =
        10u + visibility->display;
    (void)pthread_mutex_unlock(&backend->lock);
    callback_enter(backend, PHASE_VISIBILITY);
    return 0;
}

static int backend_screen(void *context,
                          const OpenNaviBackendScreenV1 *screen)
{
    StressBackend *backend = (StressBackend *)context;
    (void)pthread_mutex_lock(&backend->lock);
    assert(screen->display < OPEN_NAVI_RENDERER_DISPLAY_COUNT);
    backend->screen[screen->display] = *screen;
    ++backend->screen_per_display[screen->display];
    ++backend->screen_calls;
    assert(backend->state_order_count < 16u);
    backend->state_order[backend->state_order_count++] = 20u + screen->display;
    (void)pthread_cond_broadcast(&backend->condition);
    (void)pthread_mutex_unlock(&backend->lock);
    return 0;
}

static int backend_gps(void *context, const OpenNaviBackendGpsV1 *gps)
{
    StressBackend *backend = (StressBackend *)context;
    (void)pthread_mutex_lock(&backend->lock);
    backend->gps = *gps;
    ++backend->gps_calls;
    assert(backend->state_order_count < 16u);
    backend->state_order[backend->state_order_count++] = 30u;
    (void)pthread_cond_broadcast(&backend->condition);
    (void)pthread_mutex_unlock(&backend->lock);
    return 0;
}

static int backend_pointer(void *context,
                           const OpenNaviBackendPointerV1 *pointer)
{
    StressBackend *backend = (StressBackend *)context;
    (void)pthread_mutex_lock(&backend->lock);
    assert(backend->pointer_count < 64u);
    backend->pointer_x[backend->pointer_count] = (uint32_t)pointer->x;
    backend->pointer_actions[backend->pointer_count] = pointer->action;
    backend->pointer_visibility_seen[backend->pointer_count] =
        backend->visibility_per_display[0];
    ++backend->pointer_count;
    (void)pthread_cond_broadcast(&backend->condition);
    (void)pthread_mutex_unlock(&backend->lock);
    callback_enter(backend, PHASE_POINTER);
    return 0;
}

static int backend_vehicle(void *context,
                           const OpenNaviBackendVehicleV2 *vehicle)
{
    StressBackend *backend = (StressBackend *)context;
    (void)pthread_mutex_lock(&backend->lock);
    backend->vehicle = *vehicle;
    ++backend->vehicle_calls;
    assert(backend->state_order_count < 32u);
    backend->state_order[backend->state_order_count++] = 40u;
    (void)pthread_cond_broadcast(&backend->condition);
    (void)pthread_mutex_unlock(&backend->lock);
    callback_enter(backend, PHASE_VEHICLE);
    return 0;
}

static int backend_gps_interval(
    void *context, const OpenNaviBackendGpsIntervalV2 *gps_interval)
{
    StressBackend *backend = (StressBackend *)context;
    (void)pthread_mutex_lock(&backend->lock);
    backend->gps_interval = *gps_interval;
    ++backend->gps_interval_calls;
    assert(backend->state_order_count < 32u);
    backend->state_order[backend->state_order_count++] = 50u;
    (void)pthread_cond_broadcast(&backend->condition);
    (void)pthread_mutex_unlock(&backend->lock);
    callback_enter(backend, PHASE_GPS_INTERVAL);
    return 0;
}

static int backend_audio(void *context, const OpenNaviBackendAudioV2 *audio)
{
    StressBackend *backend = (StressBackend *)context;
    (void)pthread_mutex_lock(&backend->lock);
    backend->audio = *audio;
    ++backend->audio_calls;
    assert(backend->state_order_count < 32u);
    backend->state_order[backend->state_order_count++] = 60u;
    (void)pthread_cond_broadcast(&backend->condition);
    (void)pthread_mutex_unlock(&backend->lock);
    callback_enter(backend, PHASE_AUDIO);
    return 0;
}

static int backend_render(void *context,
                          const OpenNaviBackendRenderTargetV1 *target,
                          const OpenNaviBackendFrameV1 *frame)
{
    (void)target;
    assert(frame->sequence != 0u);
    assert(open_navi_renderer_stub_context_current() != 0);
    callback_enter((StressBackend *)context, PHASE_RENDER);
    return 0;
}

static OpenNaviBackendV1 make_backend(StressBackend *context)
{
    OpenNaviBackendV1 backend;
    memset(&backend, 0, sizeof(backend));
    backend.abi_version = OPEN_NAVI_BACKEND_ABI_V1;
    backend.struct_size = sizeof(backend);
    backend.backend_context = context;
    backend.start = backend_start;
    backend.stop = backend_stop;
    backend.surface_created = backend_surface_created;
    backend.surface_destroyed = backend_surface_destroyed;
    backend.visibility_changed = backend_visibility;
    backend.screen_changed = backend_screen;
    backend.gps_changed = backend_gps;
    backend.pointer_changed = backend_pointer;
    backend.render_frame = backend_render;
    return backend;
}

static OpenNaviBackendV2 make_backend_v2(StressBackend *context)
{
    OpenNaviBackendV2 backend;
    memset(&backend, 0, sizeof(backend));
    backend.v1 = make_backend(context);
    backend.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V2;
    backend.v1.struct_size = sizeof(backend);
    backend.vehicle_changed = backend_vehicle;
    backend.gps_interval_changed = backend_gps_interval;
    backend.audio_changed = backend_audio;
    return backend;
}

static void wait_phase_at(StressBackend *backend, enum CallbackPhase phase,
                          uint32_t expected, unsigned int source_line)
{
    struct timespec deadline;
    int status = 0;
    deadline_after(&deadline, STRESS_DEADLINE_SECONDS);
    (void)pthread_mutex_lock(&backend->lock);
    while (backend->calls[phase] < expected && status == 0)
        status = pthread_cond_timedwait(&backend->condition, &backend->lock,
                                        &deadline);
    if (status != 0 || backend->calls[phase] < expected)
    {
        if (phase == PHASE_POINTER) {
            OpenNaviRendererPointerQueueStats queue_stats;
            OpenNaviRendererStubInputStats input_stats;
            memset(&queue_stats, 0, sizeof(queue_stats));
            memset(&input_stats, 0, sizeof(input_stats));
            open_navi_renderer_test_pointer_stats(&queue_stats);
            open_navi_renderer_stub_input_stats(&input_stats);
            (void)fprintf(stderr,
                          "pointer stats accepted=%u dropped=%u platform_error=%u window_lost=%u raw_accepted=%u ignored=%u foreign=%u secondary=%u\n",
                          queue_stats.accepted, queue_stats.dropped,
                          queue_stats.platform_errors,
                          queue_stats.platform_window_lost,
                          input_stats.accepted, input_stats.ignored,
                          input_stats.foreign_window,
                          input_stats.secondary_touch);
        }
        (void)fprintf(stderr,
                      "wait_phase timeout line=%u phase=%u actual=%u expected=%u status=%d\n",
                      source_line,
                      (unsigned int)phase,
                      (unsigned int)backend->calls[phase],
                      (unsigned int)expected, status);
    }
    assert(status == 0 && backend->calls[phase] >= expected);
    (void)pthread_mutex_unlock(&backend->lock);
}

#define wait_phase(backend, phase, expected)                                  \
    wait_phase_at((backend), (phase), (expected), __LINE__)

static void wait_state_counts(StressBackend *backend, uint32_t screens,
                              uint32_t gps, uint32_t pointers)
{
    struct timespec deadline;
    int status = 0;
    deadline_after(&deadline, STRESS_DEADLINE_SECONDS);
    (void)pthread_mutex_lock(&backend->lock);
    while ((backend->screen_calls < screens || backend->gps_calls < gps ||
            backend->pointer_count < pointers) && status == 0) {
        status = pthread_cond_timedwait(&backend->condition, &backend->lock,
                                        &deadline);
    }
    assert(status == 0);
    (void)pthread_mutex_unlock(&backend->lock);
}

static void wait_semantic_counts(StressBackend *backend,
                                 uint32_t vehicle,
                                 uint32_t gps_interval,
                                 uint32_t audio)
{
    struct timespec deadline;
    int status = 0;
    deadline_after(&deadline, STRESS_DEADLINE_SECONDS);
    (void)pthread_mutex_lock(&backend->lock);
    while ((backend->vehicle_calls < vehicle ||
            backend->gps_interval_calls < gps_interval ||
            backend->audio_calls < audio) && status == 0) {
        status = pthread_cond_timedwait(&backend->condition, &backend->lock,
                                        &deadline);
    }
    assert(status == 0);
    (void)pthread_mutex_unlock(&backend->lock);
}

static void *operation_thread(void *opaque)
{
    TimedOperation *operation = (TimedOperation *)opaque;
    char diagnostic[257];
    int result = 0;
    if (operation->create != 0)
        result = open_navi_renderer_create(diagnostic, sizeof(diagnostic));
    else
        open_navi_renderer_destroy();
    (void)pthread_mutex_lock(&operation->lock);
    operation->result = result;
    operation->done = 1;
    (void)pthread_cond_broadcast(&operation->condition);
    (void)pthread_mutex_unlock(&operation->lock);
    return NULL;
}

static int timed_operation(int create)
{
    TimedOperation operation;
    struct timespec deadline;
    pthread_t thread;
    int status = 0;
    memset(&operation, 0, sizeof(operation));
    assert(pthread_mutex_init(&operation.lock, NULL) == 0);
    assert(pthread_cond_init(&operation.condition, NULL) == 0);
    operation.create = create;
    assert(pthread_create(&thread, NULL, operation_thread, &operation) == 0);
    deadline_after(&deadline, STRESS_DEADLINE_SECONDS);
    (void)pthread_mutex_lock(&operation.lock);
    while (operation.done == 0 && status == 0)
        status = pthread_cond_timedwait(&operation.condition, &operation.lock,
                                        &deadline);
    assert(status == 0 && operation.done != 0);
    (void)pthread_mutex_unlock(&operation.lock);
    assert(pthread_join(thread, NULL) == 0);
    return operation.result;
}

static void finish_lifecycle(void)
{
    assert(timed_operation(0) == 0);
    open_navi_renderer_end_lifecycle();
    assert(open_navi_backend_install_v1(NULL) == 0);
}

static void test_reentrant_phase(enum CallbackPhase phase)
{
    StressBackend context;
    OpenNaviBackendV1 backend;
    OpenNaviBackendV2 backend_v2;
    int create_status;
    backend_init(&context);
    context.destroy_phase = phase;
    open_navi_renderer_prepare_lifecycle();
    if (phase == PHASE_VEHICLE || phase == PHASE_GPS_INTERVAL ||
        phase == PHASE_AUDIO) {
        backend_v2 = make_backend_v2(&context);
        assert(open_navi_backend_install_v2(&backend_v2) == 0);
    } else {
        backend = make_backend(&context);
        assert(open_navi_backend_install_v1(&backend) == 0);
    }
    create_status = timed_operation(1);
    if (phase == PHASE_START || phase == PHASE_SURFACE_CREATED)
        assert(create_status != 0);
    else
        assert(create_status == 0);

    if (phase == PHASE_VISIBILITY) {
        assert(open_navi_renderer_notify_visibility(0u, 1u, 1u) == 0);
        wait_phase(&context, PHASE_VISIBILITY, 1u);
    } else if (phase == PHASE_POINTER) {
        assert(open_navi_renderer_test_submit_pointer(
                   OPEN_NAVI_BACKEND_POINTER_PRESS, 1, 2, 1u) == 0);
        wait_phase(&context, PHASE_POINTER, 1u);
    } else if (phase == PHASE_VEHICLE) {
        assert(open_navi_renderer_notify_vehicle_status(4u, 0u, 1u) == 0);
        wait_phase(&context, PHASE_VEHICLE, 1u);
    } else if (phase == PHASE_GPS_INTERVAL) {
        assert(open_navi_renderer_notify_gps_interval(6u, 1000u) == 0);
        wait_phase(&context, PHASE_GPS_INTERVAL, 1u);
    } else if (phase == PHASE_AUDIO) {
        assert(open_navi_renderer_notify_audio(19u, 0u, 1u, 15u) == 0);
        wait_phase(&context, PHASE_AUDIO, 1u);
    } else if (phase == PHASE_RENDER) {
        wait_phase(&context, PHASE_RENDER, 1u);
    }

    /* For cleanup phases this external caller owns join before the callback
     * reenters destroy; for all other phases it also waits detached cleanup. */
    assert(timed_operation(0) == 0);
    wait_phase(&context, PHASE_STOP, 1u);
    if (phase != PHASE_START)
        wait_phase(&context, PHASE_SURFACE_DESTROYED, 1u);
    open_navi_renderer_end_lifecycle();
    assert(open_navi_backend_install_v2(NULL) == 0);
}

static void test_stale_service(void)
{
    StressBackend first;
    StressBackend second;
    OpenNaviBackendV1 backend;
    OpenNaviBackendServicesV1 stale;
    uint32_t render_before;

    backend_init(&first);
    backend_init(&second);
    open_navi_renderer_prepare_lifecycle();
    backend = make_backend(&first);
    assert(open_navi_backend_install_v1(&backend) == 0);
    assert(timed_operation(1) == 0);
    wait_phase(&first, PHASE_RENDER, 1u);
    stale = first.services;
    assert(stale.runtime_context != NULL);
    finish_lifecycle();
    assert(stale.request_frame(stale.runtime_context) == -1);

    open_navi_renderer_prepare_lifecycle();
    backend = make_backend(&second);
    assert(open_navi_backend_install_v1(&backend) == 0);
    assert(timed_operation(1) == 0);
    wait_phase(&second, PHASE_RENDER, 1u);
    assert(second.services.runtime_context != stale.runtime_context);
    assert(stale.request_frame(stale.runtime_context) == -1);
    (void)pthread_mutex_lock(&second.lock);
    render_before = second.calls[PHASE_RENDER];
    (void)pthread_mutex_unlock(&second.lock);
    assert(second.services.request_frame(second.services.runtime_context) == 0);
    wait_phase(&second, PHASE_RENDER, render_before + 1u);
    finish_lifecycle();
}

static void test_coalescing_and_pointer_overflow(void)
{
    StressBackend context;
    OpenNaviBackendV1 backend;
    OpenNaviGpsSnapshot gps;
    uint32_t accepted = 0u;
    uint32_t dropped = 0u;
    uint32_t index;
    uint32_t display;
    OpenNaviRendererPointerQueueStats queue_stats;

    backend_init(&context);
    context.block_phase = PHASE_VISIBILITY;
    backend = make_backend(&context);
    open_navi_renderer_prepare_lifecycle();
    assert(open_navi_backend_install_v1(&backend) == 0);
    assert(timed_operation(1) == 0);
    wait_phase(&context, PHASE_RENDER, 1u);
    assert(open_navi_renderer_notify_visibility(0u, 1u, 1u) == 0);

    (void)pthread_mutex_lock(&context.lock);
    while (context.block_entered == 0)
        (void)pthread_cond_wait(&context.condition, &context.lock);
    (void)pthread_mutex_unlock(&context.lock);

    for (index = 0u; index < 40u; ++index) {
        int status = open_navi_renderer_test_submit_pointer(
            (index & 1u) != 0u ? OPEN_NAVI_BACKEND_POINTER_RELEASE
                               : OPEN_NAVI_BACKEND_POINTER_PRESS,
            (int32_t)index, (int32_t)(100u + index), index & 1u);
        if (status == 0) ++accepted;
        else ++dropped;
    }
    memset(&gps, 0, sizeof(gps));
    for (index = 0u; index < 1000u; ++index) {
        gps.latitude_e7 = (int32_t)index;
        gps.longitude_e7 = -(int32_t)index;
        assert(open_navi_renderer_notify_gps(&gps) == 0);
        for (display = 0u; display < OPEN_NAVI_RENDERER_DISPLAY_COUNT; ++display) {
            assert(open_navi_renderer_notify_visibility(
                       display, index + display * 10000u,
                       index + display * 10000u + 1u) == 0);
            assert(open_navi_renderer_notify_screen(
                       display, index + display * 10000u + 2000u,
                       index + display * 10000u) == 0);
        }
    }
    assert(open_navi_renderer_notify_visibility(3u, 1u, 1u) == -2);
    assert(open_navi_renderer_notify_screen(3u, 1u, 1u) == -2);
    assert(open_navi_renderer_test_visibility_unsupported() == 1u);
    assert(open_navi_renderer_test_screen_unsupported() == 1u);
    assert(accepted == 40u && dropped == 0u);
    assert(open_navi_renderer_test_pointer_accepted() == 40u);
    assert(open_navi_renderer_test_pointer_dropped() == 8u);
    open_navi_renderer_test_pointer_stats(&queue_stats);
    assert(queue_stats.gesture_pairs_evicted == 4u);
    assert(queue_stats.edge_dropped == 0u);

    (void)pthread_mutex_lock(&context.lock);
    context.block_release = 1;
    (void)pthread_cond_broadcast(&context.condition);
    (void)pthread_mutex_unlock(&context.lock);
    wait_state_counts(&context, 3u, 1u, 32u);
    wait_phase(&context, PHASE_VISIBILITY, 4u);

    (void)pthread_mutex_lock(&context.lock);
    for (display = 0u; display < OPEN_NAVI_RENDERER_DISPLAY_COUNT; ++display) {
        assert(context.visibility[display].visibility ==
               999u + display * 10000u);
        assert(context.visibility[display].interactivity ==
               1000u + display * 10000u);
        assert(context.screen[display].screen_type ==
               2999u + display * 10000u);
        assert(context.screen[display].animation ==
               999u + display * 10000u);
        assert(context.screen_per_display[display] == 1u);
        assert(context.visibility_per_display[display] ==
               (display == 0u ? 2u : 1u));
    }
    assert(context.gps.latitude_e7 == 999);
    assert(context.gps.longitude_e7 == -999);
    assert(context.screen_calls == 3u);
    assert(context.gps_calls == 1u);
    assert(context.state_order_count == 8u);
    assert(context.state_order[0] == 10u);
    assert(context.state_order[1] == 30u);
    assert(context.state_order[2] == 10u);
    assert(context.state_order[3] == 20u);
    assert(context.state_order[4] == 11u);
    assert(context.state_order[5] == 21u);
    assert(context.state_order[6] == 12u);
    assert(context.state_order[7] == 22u);
    for (index = 0u; index < 32u; ++index) {
        assert(context.pointer_x[index] == index + 8u);
        assert(context.pointer_actions[index] ==
               (((index + 8u) & 1u) != 0u
                    ? OPEN_NAVI_BACKEND_POINTER_RELEASE
                    : OPEN_NAVI_BACKEND_POINTER_PRESS));
    }
    (void)pthread_mutex_unlock(&context.lock);
    finish_lifecycle();

    memset(&gps, 0, sizeof(gps));
    assert(open_navi_renderer_notify_visibility(0u, 1u, 1u) == -1);
    assert(open_navi_renderer_notify_screen(0u, 1u, 1u) == -1);
    assert(open_navi_renderer_notify_gps(&gps) == -1);
}

static void test_cleanup_context_loss(void)
{
    StressBackend context;
    OpenNaviBackendV1 backend;
    backend_init(&context);
    backend = make_backend(&context);
    open_navi_renderer_prepare_lifecycle();
    assert(open_navi_backend_install_v1(&backend) == 0);
    assert(timed_operation(1) == 0);
    wait_phase(&context, PHASE_RENDER, 1u);
    context.expect_surface_destroyed_current = 0;
    open_navi_renderer_stub_set_begin_result(-1);
    assert(timed_operation(0) == 0);
    wait_phase(&context, PHASE_SURFACE_DESTROYED, 1u);
    wait_phase(&context, PHASE_STOP, 1u);
    open_navi_renderer_stub_set_begin_result(0);
    open_navi_renderer_end_lifecycle();
    assert(open_navi_backend_install_v1(NULL) == 0);
}

static OpenNaviRendererStubRawEvent raw_touch(uint32_t type, uintptr_t window,
                                         uint32_t touch_id,
                                         int32_t x, int32_t y)
{
    OpenNaviRendererStubRawEvent raw;
    memset(&raw, 0, sizeof(raw));
    raw.type = type;
    raw.window = window;
    raw.source_valid = 1u;
    raw.source_x = x;
    raw.source_y = y;
    raw.touch_id_valid = 1u;
    raw.touch_id = touch_id;
    return raw;
}

static void test_platform_input_and_backpressure(void)
{
    StressBackend context;
    StressBackend fresh;
    OpenNaviBackendV1 backend;
    OpenNaviRendererStubRawEvent raw;
    OpenNaviRendererStubInputStats input_stats;
    OpenNaviRendererPointerQueueStats queue_stats;
    uintptr_t window = open_navi_renderer_stub_input_window();
    uint32_t index;

    backend_init(&context);
    backend = make_backend(&context);
    open_navi_renderer_prepare_lifecycle();
    assert(open_navi_backend_install_v1(&backend) == 0);
    assert(timed_operation(1) == 0);
    wait_phase(&context, PHASE_RENDER, 1u);

    /* No provider notification signals the renderer condition here. The
     * bounded idle poll must discover this Screen event on its own. */
    /* More than one platform-drain batch of ignored traffic must not hide
     * the valid event behind it or wait for another provider wakeup. */
    for (index = 0u; index < 20u; ++index) {
        raw = raw_touch(OPEN_NAVI_RENDERER_STUB_RAW_TOUCH, window + 1u,
                        1u, 1, 2);
        assert(open_navi_renderer_stub_queue_raw_input(&raw) == 0);
    }
    raw = raw_touch(OPEN_NAVI_RENDERER_STUB_RAW_TOUCH, window,
                    7u, 800, 20);
    assert(open_navi_renderer_stub_queue_raw_input(&raw) == 0);
    raw = raw_touch(OPEN_NAVI_RENDERER_STUB_RAW_TOUCH, window, 7u, 10, 20);
    raw.touch_id_valid = 0u;
    assert(open_navi_renderer_stub_queue_raw_input(&raw) == 0);
    raw.touch_id_valid = 1u;
    assert(open_navi_renderer_stub_queue_raw_input(&raw) == 0);
    wait_phase(&context, PHASE_POINTER, 1u);
    (void)pthread_mutex_lock(&context.lock);
    assert(context.pointer_actions[0] == OPEN_NAVI_BACKEND_POINTER_PRESS);
    assert(context.pointer_x[0] == 10u);
    assert(context.pointer_visibility_seen[0] == 0u);
    (void)pthread_mutex_unlock(&context.lock);

    assert(open_navi_renderer_notify_visibility(0u, 1u, 1u) == 0);
    raw = raw_touch(OPEN_NAVI_RENDERER_STUB_RAW_TOUCH, window, 8u, 30, 40);
    assert(open_navi_renderer_stub_queue_raw_input(&raw) == 0);
    raw = raw_touch(OPEN_NAVI_RENDERER_STUB_RAW_MOVE, window, 7u, 11, 21);
    assert(open_navi_renderer_stub_queue_raw_input(&raw) == 0);
    raw = raw_touch(OPEN_NAVI_RENDERER_STUB_RAW_RELEASE, window, 7u, 12, 22);
    raw.source_valid = 0u;
    assert(open_navi_renderer_stub_queue_raw_input(&raw) == 0);
    wait_phase(&context, PHASE_POINTER, 3u);
    (void)pthread_mutex_lock(&context.lock);
    assert(context.pointer_actions[1] == OPEN_NAVI_BACKEND_POINTER_MOVE);
    assert(context.pointer_visibility_seen[1] == 1u);
    assert(context.pointer_actions[2] == OPEN_NAVI_BACKEND_POINTER_RELEASE);
    assert(context.pointer_x[2] == 11u);
    (void)pthread_mutex_unlock(&context.lock);
    open_navi_renderer_stub_input_stats(&input_stats);
    assert(input_stats.foreign_window == 20u);
    assert(input_stats.secondary_touch == 1u);
    assert(input_stats.property_errors == 2u);
    assert(input_stats.out_of_bounds == 1u);

    /* Block the callback while filling the queue. Moves are kept away from
     * the final two edge-reserve slots, and a full edge evicts a move. */
    (void)pthread_mutex_lock(&context.lock);
    context.block_phase = PHASE_POINTER;
    context.block_entered = 0;
    (void)pthread_mutex_unlock(&context.lock);
    assert(open_navi_renderer_test_submit_pointer(
               OPEN_NAVI_BACKEND_POINTER_PRESS, 100, 200, 1u) == 0);
    (void)pthread_mutex_lock(&context.lock);
    while (context.block_entered == 0)
        (void)pthread_cond_wait(&context.condition, &context.lock);
    (void)pthread_mutex_unlock(&context.lock);
    for (index = 0u; index < 15u; ++index) {
        assert(open_navi_renderer_test_submit_pointer(
                   OPEN_NAVI_BACKEND_POINTER_MOVE, (int32_t)index, 0, 1u) == 0);
        assert(open_navi_renderer_test_submit_pointer(
                   OPEN_NAVI_BACKEND_POINTER_PRESS, (int32_t)index, 0, 1u) == 0);
    }
    assert(open_navi_renderer_test_submit_pointer(
               OPEN_NAVI_BACKEND_POINTER_MOVE, 500, 0, 1u) == -1);
    assert(open_navi_renderer_test_submit_pointer(
               OPEN_NAVI_BACKEND_POINTER_PRESS, 501, 0, 1u) == 0);
    assert(open_navi_renderer_test_submit_pointer(
               OPEN_NAVI_BACKEND_POINTER_RELEASE, 502, 0, 0u) == 0);
    assert(open_navi_renderer_test_submit_pointer(
               OPEN_NAVI_BACKEND_POINTER_RELEASE, 503, 0, 0u) == 0);
    open_navi_renderer_test_pointer_stats(&queue_stats);
    assert(queue_stats.move_dropped == 1u);
    assert(queue_stats.move_evicted == 1u);
    assert(queue_stats.edge_dropped == 0u);
    (void)pthread_mutex_lock(&context.lock);
    context.block_release = 1;
    (void)pthread_cond_broadcast(&context.condition);
    (void)pthread_mutex_unlock(&context.lock);
    wait_phase(&context, PHASE_POINTER, 36u);
    finish_lifecycle();

    /* Raw events queued after teardown are cleared by platform creation and
     * cannot cross into a new renderer lifecycle. */
    raw = raw_touch(OPEN_NAVI_RENDERER_STUB_RAW_TOUCH, window, 9u, 90, 91);
    assert(open_navi_renderer_stub_queue_raw_input(&raw) == 0);
    backend_init(&fresh);
    backend = make_backend(&fresh);
    open_navi_renderer_prepare_lifecycle();
    assert(open_navi_backend_install_v1(&backend) == 0);
    assert(timed_operation(1) == 0);
    wait_phase(&fresh, PHASE_RENDER, 1u);
    (void)pthread_mutex_lock(&fresh.lock);
    assert(fresh.pointer_count == 0u);
    (void)pthread_mutex_unlock(&fresh.lock);

    /* A one-shot Screen API error is counted and retried, while loss of the
     * owned NAV_main window terminates the generation and rejects providers. */
    open_navi_renderer_stub_set_poll_result(3);
    raw = raw_touch(OPEN_NAVI_RENDERER_STUB_RAW_TOUCH, window, 10u, 100, 101);
    assert(open_navi_renderer_stub_queue_raw_input(&raw) == 0);
    wait_phase(&fresh, PHASE_POINTER, 1u);
    open_navi_renderer_test_pointer_stats(&queue_stats);
    assert(queue_stats.platform_errors == 1u);
    open_navi_renderer_stub_set_poll_result(-2);
    wait_phase(&fresh, PHASE_STOP, 1u);
    assert(open_navi_renderer_notify_visibility(0u, 1u, 1u) == -1);
    assert(timed_operation(0) == 0);
    open_navi_renderer_test_pointer_stats(&queue_stats);
    assert(queue_stats.platform_window_lost == 1u);
    open_navi_renderer_end_lifecycle();
    assert(open_navi_backend_install_v1(NULL) == 0);

    /* MOVE callbacks retain the existing renderer-thread reentrant teardown
     * guarantee. */
    backend_init(&fresh);
    backend = make_backend(&fresh);
    open_navi_renderer_prepare_lifecycle();
    assert(open_navi_backend_install_v1(&backend) == 0);
    assert(timed_operation(1) == 0);
    assert(open_navi_renderer_test_submit_pointer(
               OPEN_NAVI_BACKEND_POINTER_PRESS, 1, 1, 1u) == 0);
    wait_phase(&fresh, PHASE_POINTER, 1u);
    fresh.destroy_phase = PHASE_POINTER;
    assert(open_navi_renderer_test_submit_pointer(
               OPEN_NAVI_BACKEND_POINTER_MOVE, 2, 2, 1u) == 0);
    wait_phase(&fresh, PHASE_POINTER, 2u);
    assert(timed_operation(0) == 0);
    wait_phase(&fresh, PHASE_STOP, 1u);
    open_navi_renderer_end_lifecycle();
    assert(open_navi_backend_install_v1(NULL) == 0);

    /* A perpetual full drain of unrelated Screen events must not bypass the
     * stop observation point or strand the thread owning destroy/join. */
    backend_init(&fresh);
    backend = make_backend(&fresh);
    open_navi_renderer_prepare_lifecycle();
    assert(open_navi_backend_install_v1(&backend) == 0);
    assert(timed_operation(1) == 0);
    wait_phase(&fresh, PHASE_RENDER, 1u);
    open_navi_renderer_stub_set_repeating_poll_result(2);
    assert(timed_operation(0) == 0);
    wait_phase(&fresh, PHASE_STOP, 1u);
    open_navi_renderer_stub_set_repeating_poll_result(0);
    open_navi_renderer_end_lifecycle();
    assert(open_navi_backend_install_v1(NULL) == 0);
}

static void test_v2_semantic_coalescing_and_lifecycle(void)
{
    StressBackend context;
    StressBackend fresh;
    OpenNaviBackendV2 backend;
    OpenNaviGpsSnapshot gps;
    uint32_t index;

    backend_init(&context);
    context.block_phase = PHASE_VISIBILITY;
    backend = make_backend_v2(&context);
    open_navi_renderer_prepare_lifecycle();
    assert(open_navi_backend_install_v2(&backend) == 0);
    assert(timed_operation(1) == 0);
    wait_phase(&context, PHASE_RENDER, 1u);
    assert(open_navi_renderer_notify_visibility(0u, 1u, 1u) == 0);
    (void)pthread_mutex_lock(&context.lock);
    while (context.block_entered == 0)
        (void)pthread_cond_wait(&context.condition, &context.lock);
    (void)pthread_mutex_unlock(&context.lock);

    memset(&gps, 0, sizeof(gps));
    for (index = 0u; index < 1000u; ++index) {
        assert(open_navi_renderer_notify_vehicle_status(
                   4u, 0u, (uint8_t)index) == 0);
        assert(open_navi_renderer_notify_gps_interval(6u, 1000u + index) == 0);
        assert(open_navi_renderer_notify_audio(
                   19u, 0u, 7u + index, (uint8_t)(index + 1u)) == 0);
        assert(open_navi_renderer_notify_vehicle_setting(
                   6u, 0u, (uint8_t)(index + 2u)) == 0);
        assert(open_navi_renderer_notify_vehicle_speed(8u, 0x3f800000u + index) == 0);
        gps.latitude_e7 = (int32_t)index;
        assert(open_navi_renderer_notify_gps(&gps) == 0);
    }
    (void)pthread_mutex_lock(&context.lock);
    context.block_release = 1;
    (void)pthread_cond_broadcast(&context.condition);
    (void)pthread_mutex_unlock(&context.lock);
    wait_semantic_counts(&context, 1u, 1u, 1u);
    wait_state_counts(&context, 0u, 1u, 0u);
    (void)pthread_mutex_lock(&context.lock);
    assert(context.vehicle_calls == 1u);
    assert(context.gps_interval_calls == 1u);
    assert(context.audio_calls == 1u);
    assert(context.vehicle.valid_fields ==
           (OPEN_NAVI_BACKEND_VEHICLE_DAY_NIGHT_STATUS |
            OPEN_NAVI_BACKEND_VEHICLE_DAY_NIGHT_SETTING |
            OPEN_NAVI_BACKEND_VEHICLE_SPEED));
    assert(context.vehicle.changed_fields == context.vehicle.valid_fields);
    assert(context.vehicle.day_night_status == (uint8_t)999u);
    assert(context.vehicle.day_night_setting == (uint8_t)1001u);
    assert(context.vehicle.displayed_vehicle_speed_bits ==
           0x3f800000u + 999u);
    assert(context.gps_interval.interval_ms == 1999u);
    assert(context.audio.audio_source == 1006u);
    assert(context.audio.volume == (uint8_t)1000u);
    assert(context.gps.latitude_e7 == 999);
    assert(context.state_order_count == 5u);
    assert(context.state_order[0] == 10u);
    assert(context.state_order[1] == 50u);
    assert(context.state_order[2] == 60u);
    assert(context.state_order[3] == 40u);
    assert(context.state_order[4] == 30u);
    (void)pthread_mutex_unlock(&context.lock);
    finish_lifecycle();
    assert(open_navi_renderer_notify_vehicle_status(4u, 0u, 1u) == -1);
    assert(open_navi_renderer_notify_gps_interval(6u, 1000u) == -1);
    assert(open_navi_renderer_notify_audio(19u, 0u, 1u, 15u) == -1);

    /* State accepted in an ended lifecycle must not cross into the next
     * renderer generation. */
    backend_init(&fresh);
    backend = make_backend_v2(&fresh);
    open_navi_renderer_prepare_lifecycle();
    assert(open_navi_backend_install_v2(&backend) == 0);
    assert(open_navi_renderer_notify_vehicle_status(4u, 0u, 9u) == 0);
    assert(open_navi_renderer_notify_gps_interval(6u, 9u) == 0);
    assert(open_navi_renderer_notify_audio(19u, 0u, 9u, 9u) == 0);
    open_navi_renderer_end_lifecycle();
    open_navi_renderer_prepare_lifecycle();
    assert(timed_operation(1) == 0);
    wait_phase(&fresh, PHASE_RENDER, 1u);
    (void)pthread_mutex_lock(&fresh.lock);
    assert(fresh.vehicle_calls == 0u);
    assert(fresh.gps_interval_calls == 0u);
    assert(fresh.audio_calls == 0u);
    (void)pthread_mutex_unlock(&fresh.lock);
    finish_lifecycle();

    /* Reinstalling a backend while no renderer exists starts a new backend
     * generation and drops semantic state queued for the prior descriptor. */
    open_navi_renderer_prepare_lifecycle();
    assert(open_navi_backend_install_v2(&backend) == 0);
    assert(open_navi_renderer_notify_vehicle_status(4u, 0u, 7u) == 0);
    assert(open_navi_renderer_notify_gps_interval(6u, 700u) == 0);
    assert(open_navi_renderer_notify_audio(19u, 0u, 7u, 7u) == 0);
    assert(open_navi_backend_install_v2(&backend) == 0);
    assert(timed_operation(1) == 0);
    wait_phase(&fresh, PHASE_RENDER, 2u);
    (void)pthread_mutex_lock(&fresh.lock);
    assert(fresh.vehicle_calls == 0u);
    assert(fresh.gps_interval_calls == 0u);
    assert(fresh.audio_calls == 0u);
    (void)pthread_mutex_unlock(&fresh.lock);
    finish_lifecycle();
}

int main(void)
{
    enum CallbackPhase phase;
    open_navi_renderer_stub_reset();
    test_stale_service();
    for (phase = PHASE_START; phase <= PHASE_STOP; ++phase)
        test_reentrant_phase(phase);
    test_coalescing_and_pointer_overflow();
    test_platform_input_and_backpressure();
    test_v2_semantic_coalescing_and_lifecycle();
    test_cleanup_context_loss();
    return 0;
}
