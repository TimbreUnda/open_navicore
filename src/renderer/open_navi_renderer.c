#define _POSIX_C_SOURCE 200112L

#include "open_navi_renderer.h"

#include "open_navi_default_backend.h"
#include "open_navi_renderer_platform.h"

#include <pthread.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum {
    OPEN_NAVI_POINTER_QUEUE_CAPACITY = 32u,
    OPEN_NAVI_POINTER_EDGE_RESERVE = 2u,
    OPEN_NAVI_PLATFORM_DRAIN_LIMIT = 16u,
    OPEN_NAVI_PLATFORM_POLL_MILLISECONDS = 10u,
    OPEN_NAVI_RENDERER_CANCELLED = -3
};

typedef enum {
    RENDER_EVENT_VISIBILITY = 1,
    RENDER_EVENT_SCREEN = 2,
    RENDER_EVENT_GPS = 3,
    RENDER_EVENT_VEHICLE = 4,
    RENDER_EVENT_GPS_INTERVAL = 5,
    RENDER_EVENT_AUDIO = 6,
    RENDER_EVENT_INTERNATIONALIZATION = 7,
    RENDER_EVENT_CLUSTER_DISPLAY = 8,
    RENDER_EVENT_DRIVER_DISTRACTION = 9,
    RENDER_EVENT_POINTER = 10,
    RENDER_EVENT_FRAME = 11
} RenderEventType;

typedef struct {
    uint64_t sequence;
    RenderEventType type;
    union {
        OpenNaviBackendVisibilityV1 visibility;
        OpenNaviBackendScreenV1 screen;
        OpenNaviBackendGpsV1 gps;
        OpenNaviBackendVehicleV2 vehicle;
        OpenNaviBackendGpsIntervalV2 gps_interval;
        OpenNaviBackendAudioV2 audio;
        OpenNaviBackendInternationalizationV1 internationalization;
        OpenNaviBackendClusterDisplayV1 cluster_display;
        OpenNaviBackendDriverDistractionV1 driver_distraction;
        OpenNaviBackendPointerV2 pointer;  /* V1 backends get a V1 copy */
    } data;
} RenderEvent;

typedef enum {
    RENDERER_COLD = 0,
    RENDERER_STARTING = 1,
    RENDERER_RUNNING = 2,
    RENDERER_STOPPING = 3
} RendererState;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendVisibilityV1 value;
} VisibilitySlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendScreenV1 value;
} ScreenSlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendGpsV1 value;
} GpsSlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendVehicleV2 value;
} VehicleSlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendGpsIntervalV2 value;
} GpsIntervalSlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendAudioV2 value;
} AudioSlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendInternationalizationV1 value;
} InternationalizationSlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendClusterDisplayV1 value;
} ClusterDisplaySlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendDriverDistractionV1 value;
} DriverDistractionSlot;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    pthread_t thread;
    RendererState state;
    int thread_created;
    int join_claimed;
    int thread_detached;
    int stop;
    int startup_done;
    int start_result;
    int accepting_state;
    uintptr_t next_service_token;
    uintptr_t active_service_token;
    int outbound_services_active;
    uint64_t next_event_sequence;
    int frame_pending;
    uint64_t frame_event_sequence;
    uint64_t frame_sequence;
    OpenNaviBackendV1 installed;
    OpenNaviBackendV2 installed_v2;
    OpenNaviBackendV3 installed_v3;
    OpenNaviBackendV4 installed_v4;
    OpenNaviBackendV5 installed_v5;
    uint32_t installed_version;
    OpenNaviBackendV1 active;
    OpenNaviBackendV2 active_v2;
    OpenNaviBackendV3 active_v3;
    OpenNaviBackendV4 active_v4;
    OpenNaviBackendV5 active_v5;
    uint32_t active_version;
    OpenNaviBackendRenderTargetV1 target;
    VisibilitySlot visibility[OPEN_NAVI_RENDERER_DISPLAY_COUNT];
    ScreenSlot screen[OPEN_NAVI_RENDERER_DISPLAY_COUNT];
    GpsSlot gps;
    VehicleSlot vehicle;
    GpsIntervalSlot gps_interval;
    AudioSlot audio;
    InternationalizationSlot internationalization;
    ClusterDisplaySlot cluster_display;
    DriverDistractionSlot driver_distraction;
    RenderEvent pointer_queue[OPEN_NAVI_POINTER_QUEUE_CAPACITY];
    unsigned int pointer_head;
    unsigned int pointer_count;
    uint32_t pointer_accepted;
    uint32_t pointer_dropped;
    uint32_t pointer_move_coalesced;
    uint32_t pointer_move_dropped;
    uint32_t pointer_edge_dropped;
    uint32_t pointer_move_evicted;
    uint32_t pointer_gesture_pairs_evicted;
    int pointer_contacts;
    uint32_t platform_ignored;
    uint32_t platform_errors;
    uint32_t platform_window_lost;
    uint32_t visibility_unsupported;
    uint32_t screen_unsupported;
    char diagnostic[257];
} RendererRuntime;

static RendererRuntime g_runtime = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .state = RENDERER_COLD
};
static pthread_once_t g_condition_once = PTHREAD_ONCE_INIT;
static int g_condition_init_result = -1;

#if defined(__GNUC__)
extern uint32_t open_navi_service_publish_cluster(
    const OpenNaviBackendClusterV1 *cluster) __attribute__((weak));
extern uint32_t open_navi_service_publish_audio_availability(
    const OpenNaviBackendAudioAvailabilityV1 *audio) __attribute__((weak));
extern uint32_t open_navi_service_publish_thread_status(
    const OpenNaviBackendThreadStatusV1 *thread_status) __attribute__((weak));
#else
extern uint32_t open_navi_service_publish_cluster(
    const OpenNaviBackendClusterV1 *cluster);
extern uint32_t open_navi_service_publish_audio_availability(
    const OpenNaviBackendAudioAvailabilityV1 *audio);
extern uint32_t open_navi_service_publish_thread_status(
    const OpenNaviBackendThreadStatusV1 *thread_status);
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

static uint64_t monotonic_milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0u;
    return (uint64_t)now.tv_sec * 1000u +
           (uint64_t)now.tv_nsec / 1000000u;
}

static uint64_t next_event_sequence_locked(void)
{
    ++g_runtime.next_event_sequence;
    if (g_runtime.next_event_sequence == 0u)
        ++g_runtime.next_event_sequence;
    return g_runtime.next_event_sequence;
}

static uintptr_t next_service_token_locked(void)
{
    if (g_runtime.next_service_token == UINTPTR_MAX)
        return (uintptr_t)0u;
    ++g_runtime.next_service_token;
    return g_runtime.next_service_token;
}

static int event_available_locked(void)
{
    unsigned int display;
    if (g_runtime.pointer_count != 0u || g_runtime.gps.pending != 0 ||
        g_runtime.vehicle.pending != 0 ||
        g_runtime.gps_interval.pending != 0 ||
        g_runtime.audio.pending != 0 ||
        g_runtime.internationalization.pending != 0 ||
        g_runtime.cluster_display.pending != 0 ||
        g_runtime.driver_distraction.pending != 0 ||
        g_runtime.frame_pending != 0) return 1;
    for (display = 0u; display < OPEN_NAVI_RENDERER_DISPLAY_COUNT; ++display) {
        if (g_runtime.visibility[display].pending != 0 ||
            g_runtime.screen[display].pending != 0) return 1;
    }
    return 0;
}

static int request_frame(void *runtime_context)
{
    uintptr_t token = (uintptr_t)runtime_context;
    int result = 0;

    (void)pthread_mutex_lock(&g_runtime.lock);
    if (token == (uintptr_t)0u ||
        token != g_runtime.active_service_token ||
        (g_runtime.state != RENDERER_STARTING &&
         g_runtime.state != RENDERER_RUNNING) ||
        g_runtime.stop != 0) {
        result = -1;
    } else if (g_runtime.frame_pending == 0) {
        g_runtime.frame_pending = 1;
        g_runtime.frame_event_sequence = next_event_sequence_locked();
        (void)pthread_cond_signal(&g_runtime.condition);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
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

static int take_event(RenderEvent *event)
{
    uint64_t selected = UINT64_MAX;
    RenderEventType type = 0;
    unsigned int selected_display = 0u;
    unsigned int display;

    (void)pthread_mutex_lock(&g_runtime.lock);
    while (g_runtime.stop == 0 && !event_available_locked()) {
        struct timespec deadline;
        int wait_result;
        if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
            (void)pthread_mutex_unlock(&g_runtime.lock);
            return -1;
        }
        deadline.tv_nsec +=
            (long)OPEN_NAVI_PLATFORM_POLL_MILLISECONDS * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            ++deadline.tv_sec;
            deadline.tv_nsec -= 1000000000L;
        }
        wait_result = pthread_cond_timedwait(&g_runtime.condition,
                                             &g_runtime.lock, &deadline);
        if (wait_result == ETIMEDOUT && !event_available_locked()) {
            (void)pthread_mutex_unlock(&g_runtime.lock);
            return 2;
        }
        if (wait_result != 0 && wait_result != ETIMEDOUT) {
            (void)pthread_mutex_unlock(&g_runtime.lock);
            return -1;
        }
    }
    if (g_runtime.stop != 0) {
        (void)pthread_mutex_unlock(&g_runtime.lock);
        return 0;
    }

    if (g_runtime.pointer_count != 0u) {
        selected = g_runtime.pointer_queue[g_runtime.pointer_head].sequence;
        type = RENDER_EVENT_POINTER;
    }
    for (display = 0u; display < OPEN_NAVI_RENDERER_DISPLAY_COUNT; ++display) {
        if (g_runtime.visibility[display].pending != 0 &&
            g_runtime.visibility[display].sequence < selected) {
            selected = g_runtime.visibility[display].sequence;
            selected_display = display;
            type = RENDER_EVENT_VISIBILITY;
        }
        if (g_runtime.screen[display].pending != 0 &&
            g_runtime.screen[display].sequence < selected) {
            selected = g_runtime.screen[display].sequence;
            selected_display = display;
            type = RENDER_EVENT_SCREEN;
        }
    }
    if (g_runtime.gps.pending != 0 && g_runtime.gps.sequence < selected) {
        selected = g_runtime.gps.sequence;
        type = RENDER_EVENT_GPS;
    }
    if (g_runtime.vehicle.pending != 0 &&
        g_runtime.vehicle.sequence < selected) {
        selected = g_runtime.vehicle.sequence;
        type = RENDER_EVENT_VEHICLE;
    }
    if (g_runtime.gps_interval.pending != 0 &&
        g_runtime.gps_interval.sequence < selected) {
        selected = g_runtime.gps_interval.sequence;
        type = RENDER_EVENT_GPS_INTERVAL;
    }
    if (g_runtime.audio.pending != 0 &&
        g_runtime.audio.sequence < selected) {
        selected = g_runtime.audio.sequence;
        type = RENDER_EVENT_AUDIO;
    }
    if (g_runtime.internationalization.pending != 0 &&
        g_runtime.internationalization.sequence < selected) {
        selected = g_runtime.internationalization.sequence;
        type = RENDER_EVENT_INTERNATIONALIZATION;
    }
    if (g_runtime.cluster_display.pending != 0 &&
        g_runtime.cluster_display.sequence < selected) {
        selected = g_runtime.cluster_display.sequence;
        type = RENDER_EVENT_CLUSTER_DISPLAY;
    }
    if (g_runtime.driver_distraction.pending != 0 &&
        g_runtime.driver_distraction.sequence < selected) {
        selected = g_runtime.driver_distraction.sequence;
        type = RENDER_EVENT_DRIVER_DISTRACTION;
    }
    if (g_runtime.frame_pending != 0 &&
        g_runtime.frame_event_sequence < selected) {
        type = RENDER_EVENT_FRAME;
    }

    memset(event, 0, sizeof(*event));
    event->type = type;
    switch (type) {
    case RENDER_EVENT_POINTER:
        *event = g_runtime.pointer_queue[g_runtime.pointer_head];
        g_runtime.pointer_head =
            (g_runtime.pointer_head + 1u) % OPEN_NAVI_POINTER_QUEUE_CAPACITY;
        --g_runtime.pointer_count;
        break;
    case RENDER_EVENT_VISIBILITY:
        event->sequence = g_runtime.visibility[selected_display].sequence;
        event->data.visibility =
            g_runtime.visibility[selected_display].value;
        g_runtime.visibility[selected_display].pending = 0;
        break;
    case RENDER_EVENT_SCREEN:
        event->sequence = g_runtime.screen[selected_display].sequence;
        event->data.screen = g_runtime.screen[selected_display].value;
        g_runtime.screen[selected_display].pending = 0;
        break;
    case RENDER_EVENT_GPS:
        event->sequence = g_runtime.gps.sequence;
        event->data.gps = g_runtime.gps.value;
        g_runtime.gps.pending = 0;
        break;
    case RENDER_EVENT_VEHICLE:
        event->sequence = g_runtime.vehicle.sequence;
        event->data.vehicle = g_runtime.vehicle.value;
        g_runtime.vehicle.pending = 0;
        g_runtime.vehicle.value.changed_fields = 0u;
        break;
    case RENDER_EVENT_GPS_INTERVAL:
        event->sequence = g_runtime.gps_interval.sequence;
        event->data.gps_interval = g_runtime.gps_interval.value;
        g_runtime.gps_interval.pending = 0;
        break;
    case RENDER_EVENT_AUDIO:
        event->sequence = g_runtime.audio.sequence;
        event->data.audio = g_runtime.audio.value;
        g_runtime.audio.pending = 0;
        break;
    case RENDER_EVENT_INTERNATIONALIZATION:
        event->sequence = g_runtime.internationalization.sequence;
        event->data.internationalization =
            g_runtime.internationalization.value;
        g_runtime.internationalization.pending = 0;
        break;
    case RENDER_EVENT_CLUSTER_DISPLAY:
        event->sequence = g_runtime.cluster_display.sequence;
        event->data.cluster_display = g_runtime.cluster_display.value;
        g_runtime.cluster_display.pending = 0;
        break;
    case RENDER_EVENT_DRIVER_DISTRACTION:
        event->sequence = g_runtime.driver_distraction.sequence;
        event->data.driver_distraction = g_runtime.driver_distraction.value;
        g_runtime.driver_distraction.pending = 0;
        break;
    case RENDER_EVENT_FRAME:
        event->sequence = g_runtime.frame_event_sequence;
        g_runtime.frame_pending = 0;
        break;
    default:
        (void)pthread_mutex_unlock(&g_runtime.lock);
        return 0;
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return 1;
}

static void increment_counter(uint32_t *counter)
{
    if (*counter != UINT32_MAX) ++*counter;
}

static void remove_pointer_offset_locked(unsigned int offset)
{
    unsigned int index;
    for (index = offset; index + 1u < g_runtime.pointer_count; ++index) {
        unsigned int destination =
            (g_runtime.pointer_head + index) % OPEN_NAVI_POINTER_QUEUE_CAPACITY;
        unsigned int source =
            (g_runtime.pointer_head + index + 1u) % OPEN_NAVI_POINTER_QUEUE_CAPACITY;
        g_runtime.pointer_queue[destination] = g_runtime.pointer_queue[source];
    }
    --g_runtime.pointer_count;
}

static int evict_oldest_gesture_pair_locked(void)
{
    unsigned int offset;
    for (offset = 0u; offset + 1u < g_runtime.pointer_count; ++offset) {
        unsigned int first = (g_runtime.pointer_head + offset) %
                             OPEN_NAVI_POINTER_QUEUE_CAPACITY;
        unsigned int second = (first + 1u) % OPEN_NAVI_POINTER_QUEUE_CAPACITY;
        if (g_runtime.pointer_queue[first].data.pointer.action ==
                OPEN_NAVI_BACKEND_POINTER_PRESS &&
            g_runtime.pointer_queue[second].data.pointer.action ==
                OPEN_NAVI_BACKEND_POINTER_RELEASE) {
            remove_pointer_offset_locked(offset);
            remove_pointer_offset_locked(offset);
            increment_counter(&g_runtime.pointer_dropped);
            increment_counter(&g_runtime.pointer_dropped);
            increment_counter(&g_runtime.pointer_gesture_pairs_evicted);
            return 1;
        }
    }
    return 0;
}

/* The queued move of the same contact a new move can replace: searched
 * back from the tail across moves only (other contacts' moves may be
 * passed, never a press or release), so per-contact order is kept. */
static RenderEvent *coalescible_move_locked(uint32_t contact)
{
    unsigned int back;
    for (back = 1u; back <= g_runtime.pointer_count; ++back) {
        RenderEvent *event = &g_runtime.pointer_queue[
            (g_runtime.pointer_head + g_runtime.pointer_count - back) %
            OPEN_NAVI_POINTER_QUEUE_CAPACITY];
        if (event->data.pointer.action != OPEN_NAVI_BACKEND_POINTER_MOVE)
            return NULL;
        if (event->data.pointer.contact_id == contact)
            return event;
    }
    return NULL;
}

static int enqueue_pointer(const OpenNaviBackendPointerV2 *pointer)
{
    RenderEvent *event;
    unsigned int tail;
    int result = 0;

    if (pointer == NULL ||
        (pointer->action != OPEN_NAVI_BACKEND_POINTER_PRESS &&
         pointer->action != OPEN_NAVI_BACKEND_POINTER_RELEASE &&
         pointer->action != OPEN_NAVI_BACKEND_POINTER_MOVE)) return -2;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.accepting_state == 0 ||
        (g_runtime.state != RENDERER_STARTING &&
         g_runtime.state != RENDERER_RUNNING) || g_runtime.stop != 0) {
        increment_counter(&g_runtime.pointer_dropped);
        result = -1;
    } else if (pointer->action == OPEN_NAVI_BACKEND_POINTER_MOVE &&
               (event = coalescible_move_locked(pointer->contact_id)) != NULL) {
        /* Keep the queued move's sequence: take_event() serves the oldest
         * event first, and a move renumbered on every coalesce stays newer
         * than the frame request the backend renews each frame, so during a
         * drag it was only dispatched about once a second. Updating the
         * position in place delivers the latest position every frame. */
        event->data.pointer = *pointer;
        increment_counter(&g_runtime.pointer_accepted);
        increment_counter(&g_runtime.pointer_move_coalesced);
        (void)pthread_cond_signal(&g_runtime.condition);
    } else if (pointer->action == OPEN_NAVI_BACKEND_POINTER_MOVE &&
               g_runtime.pointer_count >=
                   OPEN_NAVI_POINTER_QUEUE_CAPACITY - OPEN_NAVI_POINTER_EDGE_RESERVE) {
        increment_counter(&g_runtime.pointer_dropped);
        increment_counter(&g_runtime.pointer_move_dropped);
        result = -1;
    } else {
        if (g_runtime.pointer_count == OPEN_NAVI_POINTER_QUEUE_CAPACITY) {
            unsigned int offset;
            for (offset = 0u; offset < g_runtime.pointer_count; ++offset) {
                if (g_runtime.pointer_queue[
                        (g_runtime.pointer_head + offset) %
                        OPEN_NAVI_POINTER_QUEUE_CAPACITY].data.pointer.action ==
                    OPEN_NAVI_BACKEND_POINTER_MOVE) break;
            }
            if (offset == g_runtime.pointer_count) {
                if (!evict_oldest_gesture_pair_locked()) {
                    increment_counter(&g_runtime.pointer_dropped);
                    increment_counter(&g_runtime.pointer_edge_dropped);
                    result = -1;
                }
            } else {
                remove_pointer_offset_locked(offset);
                increment_counter(&g_runtime.pointer_dropped);
                increment_counter(&g_runtime.pointer_move_evicted);
            }
        }
        if (result == 0) {
            tail = (g_runtime.pointer_head + g_runtime.pointer_count) %
                   OPEN_NAVI_POINTER_QUEUE_CAPACITY;
            event = &g_runtime.pointer_queue[tail];
            memset(event, 0, sizeof(*event));
            event->sequence = next_event_sequence_locked();
            event->type = RENDER_EVENT_POINTER;
            event->data.pointer = *pointer;
            ++g_runtime.pointer_count;
            increment_counter(&g_runtime.pointer_accepted);
            (void)pthread_cond_signal(&g_runtime.condition);
        }
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

static int drain_platform_pointer(void)
{
    unsigned int drained;
    for (drained = 0u; drained < OPEN_NAVI_PLATFORM_DRAIN_LIMIT; ++drained) {
        OpenNaviBackendPointerV2 pointer;
        int result = open_navi_renderer_platform_poll_pointer(
            &pointer, g_runtime.pointer_contacts, g_runtime.diagnostic,
            sizeof(g_runtime.diagnostic));
        if (result == 0) return 0;
        if (result == 1) {
            (void)enqueue_pointer(&pointer);
        } else if (result == 2) {
            (void)pthread_mutex_lock(&g_runtime.lock);
            increment_counter(&g_runtime.platform_ignored);
            (void)pthread_mutex_unlock(&g_runtime.lock);
        } else if (result == 3) {
            (void)pthread_mutex_lock(&g_runtime.lock);
            increment_counter(&g_runtime.platform_errors);
            (void)pthread_mutex_unlock(&g_runtime.lock);
            return 0;
        } else {
            (void)pthread_mutex_lock(&g_runtime.lock);
            if (result == -2)
                increment_counter(&g_runtime.platform_window_lost);
            else
                increment_counter(&g_runtime.platform_errors);
            (void)pthread_mutex_unlock(&g_runtime.lock);
            return -1;
        }
    }
    return 1;
}

static void dispatch_event(const RenderEvent *event, uintptr_t service_token)
{
    OpenNaviBackendV1 backend = g_runtime.active;
    OpenNaviBackendV2 backend_v2 = g_runtime.active_v2;
    OpenNaviBackendV4 backend_v4 = g_runtime.active_v4;
    OpenNaviBackendV5 backend_v5 = g_runtime.active_v5;
    int request_render = event->type != RENDER_EVENT_FRAME;

    switch (event->type) {
    case RENDER_EVENT_VISIBILITY:
        if (backend.visibility_changed != NULL)
            (void)backend.visibility_changed(backend.backend_context,
                                             &event->data.visibility);
        break;
    case RENDER_EVENT_SCREEN:
        if (backend.screen_changed != NULL)
            (void)backend.screen_changed(backend.backend_context,
                                         &event->data.screen);
        break;
    case RENDER_EVENT_GPS:
        if (backend.gps_changed != NULL)
            (void)backend.gps_changed(backend.backend_context,
                                      &event->data.gps);
        break;
    case RENDER_EVENT_VEHICLE:
        if ((g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V2 ||
             g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V3 ||
             g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V4 ||
             g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V5) &&
            backend_v2.vehicle_changed != NULL) {
            (void)backend_v2.vehicle_changed(
                backend.backend_context, &event->data.vehicle);
        }
        break;
    case RENDER_EVENT_GPS_INTERVAL:
        if ((g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V2 ||
             g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V3 ||
             g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V4 ||
             g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V5) &&
            backend_v2.gps_interval_changed != NULL) {
            (void)backend_v2.gps_interval_changed(
                backend.backend_context, &event->data.gps_interval);
        }
        break;
    case RENDER_EVENT_AUDIO:
        if ((g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V2 ||
             g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V3 ||
             g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V4 ||
             g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V5) &&
            backend_v2.audio_changed != NULL) {
            (void)backend_v2.audio_changed(
                backend.backend_context, &event->data.audio);
        }
        break;
    case RENDER_EVENT_INTERNATIONALIZATION:
        if ((g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V4 ||
             g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V5) &&
            backend_v4.internationalization_changed != NULL) {
            (void)backend_v4.internationalization_changed(
                backend.backend_context,
                &event->data.internationalization);
        }
        break;
    case RENDER_EVENT_CLUSTER_DISPLAY:
        if ((g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V4 ||
             g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V5) &&
            backend_v4.cluster_display_changed != NULL) {
            (void)backend_v4.cluster_display_changed(
                backend.backend_context, &event->data.cluster_display);
        }
        break;
    case RENDER_EVENT_DRIVER_DISTRACTION:
        if (g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V5 &&
            backend_v5.driver_distraction_changed != NULL) {
            (void)backend_v5.driver_distraction_changed(
                backend.backend_context,
                &event->data.driver_distraction);
        }
        break;
    case RENDER_EVENT_POINTER:
        if (backend.pointer_changed != NULL) {
            if (g_runtime.pointer_contacts != 0) {
                /* V2 shares the exact V1 prefix; the callback type is the
                 * V1 pointer, the payload says V2 by version and size. */
                (void)backend.pointer_changed(
                    backend.backend_context,
                    (const OpenNaviBackendPointerV1 *)(const void *)&event->data.pointer);
            } else if (event->data.pointer.contact_id == 0u) {
                OpenNaviBackendPointerV1 v1;
                memset(&v1, 0, sizeof(v1));
                v1.abi_version = OPEN_NAVI_BACKEND_ABI_V1;
                v1.struct_size = sizeof(v1);
                v1.action = event->data.pointer.action;
                v1.x = event->data.pointer.x;
                v1.y = event->data.pointer.y;
                v1.buttons = event->data.pointer.buttons;
                (void)backend.pointer_changed(backend.backend_context, &v1);
            }
        }
        break;
    case RENDER_EVENT_FRAME:
        if (backend.render_frame != NULL) {
            OpenNaviBackendFrameV1 frame;
            char diagnostic[257];
            memset(&frame, 0, sizeof(frame));
            frame.abi_version = OPEN_NAVI_BACKEND_ABI_V1;
            frame.struct_size = sizeof(frame);
            frame.sequence = ++g_runtime.frame_sequence;
            frame.monotonic_milliseconds = monotonic_milliseconds();
            diagnostic[0] = '\0';
            if (open_navi_renderer_platform_begin_frame(
                    diagnostic, sizeof(diagnostic)) == 0) {
                int render_status = backend.render_frame(
                    backend.backend_context, &g_runtime.target, &frame);
                (void)open_navi_renderer_platform_end_frame(
                    render_status == 0, diagnostic, sizeof(diagnostic));
            }
        }
        break;
    default:
        break;
    }
    if (request_render != 0)
        (void)request_frame((void *)service_token);
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
    OpenNaviBackendV1 backend = g_runtime.active;
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

static void *renderer_thread(void *unused)
{
    OpenNaviBackendServicesV1 services;
    OpenNaviBackendServicesV2 services_v2;
    RenderEvent event;
    uintptr_t service_token;
    int status;
    int started = 0;
    int surfaced = 0;
    (void)unused;

    (void)pthread_mutex_lock(&g_runtime.lock);
    service_token = g_runtime.active_service_token;
    (void)pthread_mutex_unlock(&g_runtime.lock);
    memset(&services, 0, sizeof(services));
    services.abi_version = OPEN_NAVI_BACKEND_ABI_V1;
    services.struct_size = sizeof(services);
    services.runtime_context = (void *)service_token;
    services.request_frame = request_frame;
    memset(&services_v2, 0, sizeof(services_v2));
    services_v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V2;
    services_v2.v1.struct_size = sizeof(services_v2);
    services_v2.v1.runtime_context = (void *)service_token;
    services_v2.v1.request_frame = request_frame;
    services_v2.publish_cluster = service_publish_cluster;
    services_v2.publish_audio_availability =
        service_publish_audio_availability;
    services_v2.publish_thread_status = service_publish_thread_status;

    status = open_navi_renderer_platform_create(&g_runtime.target,
                                           g_runtime.diagnostic,
                                           sizeof(g_runtime.diagnostic));
    if (status == 0 &&
        (g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V3 ||
         g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V4 ||
         g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V5)) {
        status = g_runtime.active_v3.start_v2(
            g_runtime.active.backend_context, &services_v2);
        if (status == 0) started = 1;
    } else if (status == 0 && g_runtime.active.start != NULL) {
        status = g_runtime.active.start(g_runtime.active.backend_context,
                                        &services);
        if (status == 0) started = 1;
    } else if (status == 0) {
        started = 1;
    }
    if (status == 0 &&
        (g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V3 ||
         g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V4 ||
         g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V5)) {
        (void)pthread_mutex_lock(&g_runtime.lock);
        if (g_runtime.state == RENDERER_STARTING && g_runtime.stop == 0)
            g_runtime.outbound_services_active = 1;
        (void)pthread_mutex_unlock(&g_runtime.lock);
    }
    if (status == 0 && stop_requested()) status = OPEN_NAVI_RENDERER_CANCELLED;

    if (status == 0 && g_runtime.active.surface_created != NULL) {
        char diagnostic[257];
        diagnostic[0] = '\0';
        if (open_navi_renderer_platform_begin_frame(
                diagnostic, sizeof(diagnostic)) != 0) {
            status = -1;
        } else {
            status = g_runtime.active.surface_created(
                g_runtime.active.backend_context, &g_runtime.target);
            (void)open_navi_renderer_platform_end_frame(
                0, diagnostic, sizeof(diagnostic));
            if (status == 0) surfaced = 1;
        }
    } else if (status == 0) {
        surfaced = 1;
    }
    if (status == 0 && stop_requested()) status = OPEN_NAVI_RENDERER_CANCELLED;

    (void)pthread_mutex_lock(&g_runtime.lock);
    if (status == 0 && g_runtime.stop == 0 &&
        g_runtime.state == RENDERER_STARTING) {
        g_runtime.state = RENDERER_RUNNING;
        g_runtime.outbound_services_active =
            g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V3 ||
            g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V4 ||
            g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V5;
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

    if (status == 0) (void)request_frame((void *)service_token);
    while (status == 0) {
        int drain_result;
        int take_result;
        drain_result = drain_platform_pointer();
        if (drain_result < 0) {
            status = -1;
            break;
        }
        /* A full bounded drain can be followed by another full batch forever.
         * Observe teardown between every batch rather than relying on a
         * condition wait that a perpetual unrelated-event flood bypasses. */
        if (stop_requested()) break;
        if (drain_result > 0) {
            int available;
            (void)pthread_mutex_lock(&g_runtime.lock);
            available = event_available_locked();
            (void)pthread_mutex_unlock(&g_runtime.lock);
            if (!available) continue;
        }
        take_result = take_event(&event);
        if (take_result == 0) break;
        if (take_result < 0) {
            status = -1;
            break;
        }
        if (take_result == 1) dispatch_event(&event, service_token);
    }
    if (status != 0) {
        unsigned int display;
        (void)pthread_mutex_lock(&g_runtime.lock);
        g_runtime.accepting_state = 0;
        g_runtime.stop = 1;
        g_runtime.active_service_token = (uintptr_t)0u;
        g_runtime.outbound_services_active = 0;
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
        g_runtime.pointer_head = 0u;
        g_runtime.pointer_count = 0u;
        g_runtime.frame_pending = 0;
        if (g_runtime.state != RENDERER_COLD)
            g_runtime.state = RENDERER_STOPPING;
        (void)pthread_mutex_unlock(&g_runtime.lock);
    }
    renderer_cleanup(started, surfaced);

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
    return NULL;
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

int open_navi_backend_install_v1(const OpenNaviBackendV1 *backend)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state != RENDERER_COLD) {
        result = -1;
    } else if (backend == NULL) {
        memset(&g_runtime.installed, 0, sizeof(g_runtime.installed));
        memset(&g_runtime.installed_v2, 0, sizeof(g_runtime.installed_v2));
        memset(&g_runtime.installed_v3, 0, sizeof(g_runtime.installed_v3));
        memset(&g_runtime.vehicle, 0, sizeof(g_runtime.vehicle));
        memset(&g_runtime.gps_interval, 0, sizeof(g_runtime.gps_interval));
        memset(&g_runtime.audio, 0, sizeof(g_runtime.audio));
        g_runtime.installed_version = 0u;
    } else if (backend->abi_version != OPEN_NAVI_BACKEND_ABI_V1 ||
               backend->struct_size < sizeof(OpenNaviBackendV1)) {
        result = -2;
    } else {
        g_runtime.installed = *backend;
        memset(&g_runtime.installed_v2, 0, sizeof(g_runtime.installed_v2));
        memset(&g_runtime.installed_v3, 0, sizeof(g_runtime.installed_v3));
        memset(&g_runtime.vehicle, 0, sizeof(g_runtime.vehicle));
        memset(&g_runtime.gps_interval, 0, sizeof(g_runtime.gps_interval));
        memset(&g_runtime.audio, 0, sizeof(g_runtime.audio));
        g_runtime.installed_version = OPEN_NAVI_BACKEND_ABI_V1;
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_backend_install_v2(const OpenNaviBackendV2 *backend)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state != RENDERER_COLD) {
        result = -1;
    } else if (backend == NULL) {
        memset(&g_runtime.installed, 0, sizeof(g_runtime.installed));
        memset(&g_runtime.installed_v2, 0, sizeof(g_runtime.installed_v2));
        memset(&g_runtime.installed_v3, 0, sizeof(g_runtime.installed_v3));
        memset(&g_runtime.vehicle, 0, sizeof(g_runtime.vehicle));
        memset(&g_runtime.gps_interval, 0, sizeof(g_runtime.gps_interval));
        memset(&g_runtime.audio, 0, sizeof(g_runtime.audio));
        g_runtime.installed_version = 0u;
    } else if (backend->v1.abi_version != OPEN_NAVI_BACKEND_ABI_V2 ||
               backend->v1.struct_size < sizeof(OpenNaviBackendV2)) {
        result = -2;
    } else {
        g_runtime.installed = backend->v1;
        g_runtime.installed_v2 = *backend;
        memset(&g_runtime.installed_v3, 0, sizeof(g_runtime.installed_v3));
        memset(&g_runtime.vehicle, 0, sizeof(g_runtime.vehicle));
        memset(&g_runtime.gps_interval, 0, sizeof(g_runtime.gps_interval));
        memset(&g_runtime.audio, 0, sizeof(g_runtime.audio));
        g_runtime.installed_version = OPEN_NAVI_BACKEND_ABI_V2;
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_backend_install_v3(const OpenNaviBackendV3 *backend)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state != RENDERER_COLD) {
        result = -1;
    } else if (backend == NULL) {
        memset(&g_runtime.installed, 0, sizeof(g_runtime.installed));
        memset(&g_runtime.installed_v2, 0, sizeof(g_runtime.installed_v2));
        memset(&g_runtime.installed_v3, 0, sizeof(g_runtime.installed_v3));
        memset(&g_runtime.vehicle, 0, sizeof(g_runtime.vehicle));
        memset(&g_runtime.gps_interval, 0, sizeof(g_runtime.gps_interval));
        memset(&g_runtime.audio, 0, sizeof(g_runtime.audio));
        g_runtime.installed_version = 0u;
    } else if (backend->v2.v1.abi_version != OPEN_NAVI_BACKEND_ABI_V3 ||
               backend->v2.v1.struct_size < sizeof(OpenNaviBackendV3) ||
               backend->start_v2 == NULL) {
        result = -2;
    } else {
        g_runtime.installed = backend->v2.v1;
        g_runtime.installed_v2 = backend->v2;
        g_runtime.installed_v3 = *backend;
        memset(&g_runtime.vehicle, 0, sizeof(g_runtime.vehicle));
        memset(&g_runtime.gps_interval, 0, sizeof(g_runtime.gps_interval));
        memset(&g_runtime.audio, 0, sizeof(g_runtime.audio));
        g_runtime.installed_version = OPEN_NAVI_BACKEND_ABI_V3;
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_backend_install_v4(const OpenNaviBackendV4 *backend)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state != RENDERER_COLD) {
        result = -1;
    } else if (backend == NULL) {
        memset(&g_runtime.installed, 0, sizeof(g_runtime.installed));
        memset(&g_runtime.installed_v2, 0, sizeof(g_runtime.installed_v2));
        memset(&g_runtime.installed_v3, 0, sizeof(g_runtime.installed_v3));
        memset(&g_runtime.installed_v4, 0, sizeof(g_runtime.installed_v4));
        memset(&g_runtime.vehicle, 0, sizeof(g_runtime.vehicle));
        memset(&g_runtime.gps_interval, 0, sizeof(g_runtime.gps_interval));
        memset(&g_runtime.audio, 0, sizeof(g_runtime.audio));
        memset(&g_runtime.internationalization, 0,
               sizeof(g_runtime.internationalization));
        memset(&g_runtime.cluster_display, 0,
               sizeof(g_runtime.cluster_display));
        memset(&g_runtime.driver_distraction, 0,
               sizeof(g_runtime.driver_distraction));
        g_runtime.installed_version = 0u;
    } else if (backend->v3.v2.v1.abi_version != OPEN_NAVI_BACKEND_ABI_V4 ||
               backend->v3.v2.v1.struct_size < sizeof(OpenNaviBackendV4) ||
               backend->v3.start_v2 == NULL) {
        result = -2;
    } else {
        g_runtime.installed = backend->v3.v2.v1;
        g_runtime.installed_v2 = backend->v3.v2;
        g_runtime.installed_v3 = backend->v3;
        g_runtime.installed_v4 = *backend;
        memset(&g_runtime.vehicle, 0, sizeof(g_runtime.vehicle));
        memset(&g_runtime.gps_interval, 0, sizeof(g_runtime.gps_interval));
        memset(&g_runtime.audio, 0, sizeof(g_runtime.audio));
        memset(&g_runtime.internationalization, 0,
               sizeof(g_runtime.internationalization));
        memset(&g_runtime.cluster_display, 0,
               sizeof(g_runtime.cluster_display));
        memset(&g_runtime.driver_distraction, 0,
               sizeof(g_runtime.driver_distraction));
        g_runtime.installed_version = OPEN_NAVI_BACKEND_ABI_V4;
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_backend_install_v5(const OpenNaviBackendV5 *backend)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state != RENDERER_COLD) {
        result = -1;
    } else if (backend == NULL) {
        memset(&g_runtime.installed, 0, sizeof(g_runtime.installed));
        memset(&g_runtime.installed_v2, 0, sizeof(g_runtime.installed_v2));
        memset(&g_runtime.installed_v3, 0, sizeof(g_runtime.installed_v3));
        memset(&g_runtime.installed_v4, 0, sizeof(g_runtime.installed_v4));
        memset(&g_runtime.installed_v5, 0, sizeof(g_runtime.installed_v5));
        memset(&g_runtime.vehicle, 0, sizeof(g_runtime.vehicle));
        memset(&g_runtime.gps_interval, 0, sizeof(g_runtime.gps_interval));
        memset(&g_runtime.audio, 0, sizeof(g_runtime.audio));
        memset(&g_runtime.internationalization, 0,
               sizeof(g_runtime.internationalization));
        memset(&g_runtime.cluster_display, 0,
               sizeof(g_runtime.cluster_display));
        memset(&g_runtime.driver_distraction, 0,
               sizeof(g_runtime.driver_distraction));
        g_runtime.installed_version = 0u;
    } else if (backend->v4.v3.v2.v1.abi_version !=
                   OPEN_NAVI_BACKEND_ABI_V5 ||
               backend->v4.v3.v2.v1.struct_size < sizeof(OpenNaviBackendV5) ||
               backend->v4.v3.start_v2 == NULL) {
        result = -2;
    } else {
        g_runtime.installed = backend->v4.v3.v2.v1;
        g_runtime.installed_v2 = backend->v4.v3.v2;
        g_runtime.installed_v3 = backend->v4.v3;
        g_runtime.installed_v4 = backend->v4;
        g_runtime.installed_v5 = *backend;
        memset(&g_runtime.vehicle, 0, sizeof(g_runtime.vehicle));
        memset(&g_runtime.gps_interval, 0, sizeof(g_runtime.gps_interval));
        memset(&g_runtime.audio, 0, sizeof(g_runtime.audio));
        memset(&g_runtime.internationalization, 0,
               sizeof(g_runtime.internationalization));
        memset(&g_runtime.cluster_display, 0,
               sizeof(g_runtime.cluster_display));
        memset(&g_runtime.driver_distraction, 0,
               sizeof(g_runtime.driver_distraction));
        g_runtime.installed_version = OPEN_NAVI_BACKEND_ABI_V5;
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

void open_navi_renderer_prepare_lifecycle(void)
{
    unsigned int display;
    if (ensure_condition() != 0) return;
    (void)pthread_mutex_lock(&g_runtime.lock);
    g_runtime.accepting_state = 1;
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
    g_runtime.pointer_head = 0u;
    g_runtime.pointer_count = 0u;
    g_runtime.pointer_accepted = 0u;
    g_runtime.pointer_dropped = 0u;
    g_runtime.pointer_move_coalesced = 0u;
    g_runtime.pointer_move_dropped = 0u;
    g_runtime.pointer_edge_dropped = 0u;
    g_runtime.pointer_move_evicted = 0u;
    g_runtime.pointer_gesture_pairs_evicted = 0u;
    g_runtime.platform_ignored = 0u;
    g_runtime.platform_errors = 0u;
    g_runtime.platform_window_lost = 0u;
    g_runtime.visibility_unsupported = 0u;
    g_runtime.screen_unsupported = 0u;
    g_runtime.frame_pending = 0;
    (void)pthread_mutex_unlock(&g_runtime.lock);
}

void open_navi_renderer_end_lifecycle(void)
{
    unsigned int display;
    (void)pthread_mutex_lock(&g_runtime.lock);
    g_runtime.accepting_state = 0;
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
    g_runtime.pointer_head = 0u;
    g_runtime.pointer_count = 0u;
    g_runtime.frame_pending = 0;
    (void)pthread_mutex_unlock(&g_runtime.lock);
}

int open_navi_renderer_create(char *diagnostic, size_t diagnostic_capacity)
{
    int create_result;
    int start_result;
    unsigned int display;
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
        g_runtime.pointer_head = 0u;
        g_runtime.pointer_count = 0u;
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
    memset(&g_runtime.active_v2, 0, sizeof(g_runtime.active_v2));
    memset(&g_runtime.active_v3, 0, sizeof(g_runtime.active_v3));
    memset(&g_runtime.active_v4, 0, sizeof(g_runtime.active_v4));
    memset(&g_runtime.active_v5, 0, sizeof(g_runtime.active_v5));
    g_runtime.outbound_services_active = 0;
    if (g_runtime.installed_version == OPEN_NAVI_BACKEND_ABI_V5) {
        g_runtime.active = g_runtime.installed_v5.v4.v3.v2.v1;
        g_runtime.active_v2 = g_runtime.installed_v5.v4.v3.v2;
        g_runtime.active_v3 = g_runtime.installed_v5.v4.v3;
        g_runtime.active_v4 = g_runtime.installed_v5.v4;
        g_runtime.active_v5 = g_runtime.installed_v5;
        g_runtime.active_version = OPEN_NAVI_BACKEND_ABI_V5;
    } else if (g_runtime.installed_version == OPEN_NAVI_BACKEND_ABI_V4) {
        g_runtime.active = g_runtime.installed_v4.v3.v2.v1;
        g_runtime.active_v2 = g_runtime.installed_v4.v3.v2;
        g_runtime.active_v3 = g_runtime.installed_v4.v3;
        g_runtime.active_v4 = g_runtime.installed_v4;
        g_runtime.active_version = OPEN_NAVI_BACKEND_ABI_V4;
    } else if (g_runtime.installed_version == OPEN_NAVI_BACKEND_ABI_V3) {
        g_runtime.active = g_runtime.installed_v3.v2.v1;
        g_runtime.active_v2 = g_runtime.installed_v3.v2;
        g_runtime.active_v3 = g_runtime.installed_v3;
        g_runtime.active_version = OPEN_NAVI_BACKEND_ABI_V3;
    } else if (g_runtime.installed_version == OPEN_NAVI_BACKEND_ABI_V2) {
        g_runtime.active = g_runtime.installed_v2.v1;
        g_runtime.active_v2 = g_runtime.installed_v2;
        g_runtime.active_version = OPEN_NAVI_BACKEND_ABI_V2;
    } else if (g_runtime.installed_version == OPEN_NAVI_BACKEND_ABI_V1) {
        g_runtime.active = g_runtime.installed;
        g_runtime.active_version = OPEN_NAVI_BACKEND_ABI_V1;
    } else {
        g_runtime.active = *open_navi_default_backend_v1();
        g_runtime.active_version = OPEN_NAVI_BACKEND_ABI_V1;
    }
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
        g_runtime.pointer_head = 0u;
        g_runtime.pointer_count = 0u;
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

int open_navi_renderer_notify_visibility(uint32_t display, uint32_t visibility,
                                    uint32_t interactivity)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (display >= OPEN_NAVI_RENDERER_DISPLAY_COUNT) {
        if (g_runtime.visibility_unsupported != UINT32_MAX)
            ++g_runtime.visibility_unsupported;
        result = -2;
    } else if (g_runtime.accepting_state == 0 ||
        g_runtime.state == RENDERER_STOPPING) {
        result = -1;
    } else {
        VisibilitySlot *slot = &g_runtime.visibility[display];
        slot->value.abi_version = OPEN_NAVI_BACKEND_ABI_V1;
        slot->value.struct_size = sizeof(slot->value);
        slot->value.display = display;
        slot->value.visibility = visibility;
        slot->value.interactivity = interactivity;
        slot->sequence = next_event_sequence_locked();
        slot->pending = 1;
        (void)pthread_cond_signal(&g_runtime.condition);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_renderer_notify_screen(uint32_t display, uint32_t screen_type,
                                uint32_t animation)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (display >= OPEN_NAVI_RENDERER_DISPLAY_COUNT) {
        if (g_runtime.screen_unsupported != UINT32_MAX)
            ++g_runtime.screen_unsupported;
        result = -2;
    } else if (g_runtime.accepting_state == 0 ||
        g_runtime.state == RENDERER_STOPPING) {
        result = -1;
    } else {
        ScreenSlot *slot = &g_runtime.screen[display];
        slot->value.abi_version = OPEN_NAVI_BACKEND_ABI_V1;
        slot->value.struct_size = sizeof(slot->value);
        slot->value.display = display;
        slot->value.screen_type = screen_type;
        slot->value.animation = animation;
        slot->sequence = next_event_sequence_locked();
        slot->pending = 1;
        (void)pthread_cond_signal(&g_runtime.condition);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_renderer_notify_gps(const OpenNaviGpsSnapshot *snapshot)
{
    int result = 0;
    if (snapshot == NULL) return -2;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.accepting_state == 0 ||
        g_runtime.state == RENDERER_STOPPING) {
        result = -1;
    } else {
        OpenNaviBackendGpsV1 *gps = &g_runtime.gps.value;
        memset(gps, 0, sizeof(*gps));
        gps->abi_version = OPEN_NAVI_BACKEND_ABI_V1;
        gps->struct_size = sizeof(*gps);
        gps->latitude_e7 = snapshot->latitude_e7;
        gps->longitude_e7 = snapshot->longitude_e7;
        gps->accuracy_raw = snapshot->accuracy_raw;
        gps->altitude_raw = snapshot->altitude_raw;
        gps->ellipsoid_altitude_raw = snapshot->ellipsoid_altitude_raw;
        gps->heading_raw = snapshot->heading_raw;
        gps->speed_raw_masked = snapshot->speed_raw_masked;
        gps->hdop_raw = snapshot->hdop_raw;
        gps->pdop_raw = snapshot->pdop_raw;
        gps->vdop_raw = snapshot->vdop_raw;
        gps->unknown_100 = snapshot->unknown_100;
        gps->fix_type = snapshot->fix_type;
        gps->satellites_used = snapshot->sats_used;
        gps->satellites_in_view = snapshot->sats_in_view;
        gps->quality_status = snapshot->quality_status;
        gps->sync_counter = snapshot->sync_counter;
        memcpy(gps->satellites, snapshot->satellites,
               sizeof(gps->satellites));
        gps->travel_distance_raw = snapshot->travel_distance_raw;
        gps->altitude_change_raw = snapshot->altitude_change_raw;
        gps->noise_raw = snapshot->noise_raw;
        g_runtime.gps.sequence = next_event_sequence_locked();
        g_runtime.gps.pending = 1;
        (void)pthread_cond_signal(&g_runtime.condition);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

static int renderer_v2_enabled_locked(void)
{
    if (g_runtime.state == RENDERER_STARTING ||
        g_runtime.state == RENDERER_RUNNING) {
        return g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V2 ||
               g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V3 ||
               g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V4 ||
               g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V5;
    }
    return g_runtime.installed_version == OPEN_NAVI_BACKEND_ABI_V2 ||
           g_runtime.installed_version == OPEN_NAVI_BACKEND_ABI_V3 ||
           g_runtime.installed_version == OPEN_NAVI_BACKEND_ABI_V4 ||
           g_runtime.installed_version == OPEN_NAVI_BACKEND_ABI_V5;
}

static int renderer_v4_enabled_locked(void)
{
    if (g_runtime.state == RENDERER_STARTING ||
        g_runtime.state == RENDERER_RUNNING)
        return g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V4 ||
               g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V5;
    return g_runtime.installed_version == OPEN_NAVI_BACKEND_ABI_V4 ||
           g_runtime.installed_version == OPEN_NAVI_BACKEND_ABI_V5;
}

static int renderer_v5_enabled_locked(void)
{
    if (g_runtime.state == RENDERER_STARTING ||
        g_runtime.state == RENDERER_RUNNING)
        return g_runtime.active_version == OPEN_NAVI_BACKEND_ABI_V5;
    return g_runtime.installed_version == OPEN_NAVI_BACKEND_ABI_V5;
}

static int notify_vehicle(uint32_t source_slot,
                          uint32_t request_handle,
                          uint32_t field,
                          uint32_t value)
{
    OpenNaviBackendVehicleV2 *vehicle;
    uint32_t changed = 0u;
    int result = 0;

    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.accepting_state == 0 ||
        g_runtime.state == RENDERER_STOPPING) {
        result = -1;
    } else if (renderer_v2_enabled_locked() != 0) {
        vehicle = &g_runtime.vehicle.value;
        if ((vehicle->valid_fields & field) == 0u) {
            changed = field;
        } else if (field == OPEN_NAVI_BACKEND_VEHICLE_DAY_NIGHT_STATUS) {
            changed = vehicle->day_night_status != (uint8_t)value ? field : 0u;
        } else if (field == OPEN_NAVI_BACKEND_VEHICLE_DAY_NIGHT_SETTING) {
            changed = vehicle->day_night_setting != (uint8_t)value ? field : 0u;
        } else {
            changed = vehicle->displayed_vehicle_speed_bits != value
                          ? field : 0u;
        }
        vehicle->abi_version = OPEN_NAVI_BACKEND_ABI_V2;
        vehicle->struct_size = sizeof(*vehicle);
        vehicle->valid_fields |= field;
        if (g_runtime.vehicle.pending == 0)
            vehicle->changed_fields = changed;
        else
            vehicle->changed_fields |= changed;
        vehicle->source_slot = source_slot;
        vehicle->request_handle = request_handle;
        if (field == OPEN_NAVI_BACKEND_VEHICLE_DAY_NIGHT_STATUS)
            vehicle->day_night_status = (uint8_t)value;
        else if (field == OPEN_NAVI_BACKEND_VEHICLE_DAY_NIGHT_SETTING)
            vehicle->day_night_setting = (uint8_t)value;
        else
            vehicle->displayed_vehicle_speed_bits = value;
        g_runtime.vehicle.sequence = next_event_sequence_locked();
        g_runtime.vehicle.pending = 1;
        (void)pthread_cond_signal(&g_runtime.condition);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_renderer_notify_vehicle_status(uint32_t source_slot,
                                        uint32_t request_handle,
                                        uint8_t value)
{
    return notify_vehicle(source_slot, request_handle,
                          OPEN_NAVI_BACKEND_VEHICLE_DAY_NIGHT_STATUS,
                          (uint32_t)value);
}

int open_navi_renderer_notify_vehicle_setting(uint32_t source_slot,
                                         uint32_t request_handle,
                                         uint8_t value)
{
    return notify_vehicle(source_slot, request_handle,
                          OPEN_NAVI_BACKEND_VEHICLE_DAY_NIGHT_SETTING,
                          (uint32_t)value);
}

int open_navi_renderer_notify_vehicle_speed(uint32_t source_slot,
                                       uint32_t speed_bits)
{
    return notify_vehicle(source_slot, 0u,
                          OPEN_NAVI_BACKEND_VEHICLE_SPEED, speed_bits);
}

int open_navi_renderer_notify_gps_interval(uint32_t source_slot,
                                      uint32_t interval_ms)
{
    OpenNaviBackendGpsIntervalV2 *gps_interval;
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.accepting_state == 0 ||
        g_runtime.state == RENDERER_STOPPING) {
        result = -1;
    } else if (renderer_v2_enabled_locked() != 0) {
        gps_interval = &g_runtime.gps_interval.value;
        memset(gps_interval, 0, sizeof(*gps_interval));
        gps_interval->abi_version = OPEN_NAVI_BACKEND_ABI_V2;
        gps_interval->struct_size = sizeof(*gps_interval);
        gps_interval->source_slot = source_slot;
        gps_interval->interval_ms = interval_ms;
        g_runtime.gps_interval.sequence = next_event_sequence_locked();
        g_runtime.gps_interval.pending = 1;
        (void)pthread_cond_signal(&g_runtime.condition);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_renderer_notify_audio(uint32_t source_slot,
                               uint32_t request_handle,
                               uint32_t audio_source,
                               uint8_t volume)
{
    OpenNaviBackendAudioV2 *audio;
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.accepting_state == 0 ||
        g_runtime.state == RENDERER_STOPPING) {
        result = -1;
    } else if (renderer_v2_enabled_locked() != 0) {
        audio = &g_runtime.audio.value;
        memset(audio, 0, sizeof(*audio));
        audio->abi_version = OPEN_NAVI_BACKEND_ABI_V2;
        audio->struct_size = sizeof(*audio);
        audio->source_slot = source_slot;
        audio->request_handle = request_handle;
        audio->audio_source = audio_source;
        audio->volume = volume;
        g_runtime.audio.sequence = next_event_sequence_locked();
        g_runtime.audio.pending = 1;
        (void)pthread_cond_signal(&g_runtime.condition);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_renderer_notify_internationalization(
    uint32_t source_slot, uint32_t request_handle,
    const uint8_t settings_raw[4], int settings_present)
{
    OpenNaviBackendInternationalizationV1 *event;
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.accepting_state == 0 ||
        g_runtime.state == RENDERER_STOPPING) {
        result = -1;
    } else if (renderer_v4_enabled_locked() != 0) {
        event = &g_runtime.internationalization.value;
        memset(event, 0, sizeof(*event));
        event->abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
        event->struct_size = sizeof(*event);
        event->source_slot = source_slot;
        event->request_handle = request_handle;
        event->settings_present = settings_present != 0 ? 1u : 0u;
        if (settings_present != 0 && settings_raw != NULL) {
            event->language_raw = (uint16_t)(
                (uint16_t)settings_raw[0] |
                ((uint16_t)settings_raw[1] << 8));
            event->date_format_raw = settings_raw[2];
            event->time_format_raw = settings_raw[3];
        }
        g_runtime.internationalization.sequence =
            next_event_sequence_locked();
        g_runtime.internationalization.pending = 1;
        (void)pthread_cond_signal(&g_runtime.condition);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_renderer_notify_cluster_display(uint32_t source_slot,
                                         uint32_t request_handle,
                                         uint8_t enabled_raw)
{
    OpenNaviBackendClusterDisplayV1 *event;
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.accepting_state == 0 ||
        g_runtime.state == RENDERER_STOPPING) {
        result = -1;
    } else if (renderer_v4_enabled_locked() != 0) {
        event = &g_runtime.cluster_display.value;
        memset(event, 0, sizeof(*event));
        event->abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
        event->struct_size = sizeof(*event);
        event->source_slot = source_slot;
        event->request_handle = request_handle;
        event->enabled_raw = enabled_raw;
        g_runtime.cluster_display.sequence = next_event_sequence_locked();
        g_runtime.cluster_display.pending = 1;
        (void)pthread_cond_signal(&g_runtime.condition);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_renderer_notify_driver_distraction(uint32_t source_slot,
                                            uint32_t request_handle,
                                            uint32_t status_raw)
{
    OpenNaviBackendDriverDistractionV1 *event;
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.accepting_state == 0 ||
        g_runtime.state == RENDERER_STOPPING) {
        result = -1;
    } else if (renderer_v5_enabled_locked() != 0) {
        event = &g_runtime.driver_distraction.value;
        memset(event, 0, sizeof(*event));
        event->abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
        event->struct_size = sizeof(*event);
        event->source_slot = source_slot;
        event->request_handle = request_handle;
        event->distraction_status_raw = status_raw;
        g_runtime.driver_distraction.sequence = next_event_sequence_locked();
        g_runtime.driver_distraction.pending = 1;
        (void)pthread_cond_signal(&g_runtime.condition);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

#if defined(OPEN_NAVI_TESTING)
int open_navi_renderer_test_submit_pointer(uint32_t action, int32_t x, int32_t y,
                                      uint32_t buttons)
{
    return open_navi_renderer_test_submit_contact(action, x, y, buttons, 0u, 1u);
}

int open_navi_renderer_test_submit_contact(uint32_t action, int32_t x, int32_t y,
                                      uint32_t buttons, uint32_t contact,
                                      uint32_t count)
{
    OpenNaviBackendPointerV2 pointer;
    memset(&pointer, 0, sizeof(pointer));
    pointer.abi_version = OPEN_NAVI_BACKEND_POINTER_ABI_V2;
    pointer.struct_size = sizeof(pointer);
    pointer.action = action;
    pointer.x = x;
    pointer.y = y;
    pointer.buttons = buttons;
    pointer.contact_id = contact;
    pointer.contact_count = count;
    return enqueue_pointer(&pointer);
}

uint32_t open_navi_renderer_test_pointer_accepted(void)
{
    uint32_t result;
    (void)pthread_mutex_lock(&g_runtime.lock);
    result = g_runtime.pointer_accepted;
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

uint32_t open_navi_renderer_test_pointer_dropped(void)
{
    uint32_t result;
    (void)pthread_mutex_lock(&g_runtime.lock);
    result = g_runtime.pointer_dropped;
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

void open_navi_renderer_test_pointer_stats(OpenNaviRendererPointerQueueStats *stats_out)
{
    if (stats_out == NULL) return;
    (void)pthread_mutex_lock(&g_runtime.lock);
    stats_out->accepted = g_runtime.pointer_accepted;
    stats_out->dropped = g_runtime.pointer_dropped;
    stats_out->move_coalesced = g_runtime.pointer_move_coalesced;
    stats_out->move_dropped = g_runtime.pointer_move_dropped;
    stats_out->edge_dropped = g_runtime.pointer_edge_dropped;
    stats_out->move_evicted = g_runtime.pointer_move_evicted;
    stats_out->gesture_pairs_evicted =
        g_runtime.pointer_gesture_pairs_evicted;
    stats_out->platform_ignored = g_runtime.platform_ignored;
    stats_out->platform_errors = g_runtime.platform_errors;
    stats_out->platform_window_lost = g_runtime.platform_window_lost;
    (void)pthread_mutex_unlock(&g_runtime.lock);
}

uint32_t open_navi_renderer_test_visibility_unsupported(void)
{
    uint32_t result;
    (void)pthread_mutex_lock(&g_runtime.lock);
    result = g_runtime.visibility_unsupported;
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

uint32_t open_navi_renderer_test_screen_unsupported(void)
{
    uint32_t result;
    (void)pthread_mutex_lock(&g_runtime.lock);
    result = g_runtime.screen_unsupported;
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_renderer_test_pending_visibility(
    uint32_t display, OpenNaviBackendVisibilityV1 *value_out)
{
    int pending = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (display < OPEN_NAVI_RENDERER_DISPLAY_COUNT) {
        pending = g_runtime.visibility[display].pending;
        if (pending != 0 && value_out != NULL)
            *value_out = g_runtime.visibility[display].value;
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return pending;
}

int open_navi_renderer_test_pending_screen(
    uint32_t display, OpenNaviBackendScreenV1 *value_out)
{
    int pending = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (display < OPEN_NAVI_RENDERER_DISPLAY_COUNT) {
        pending = g_runtime.screen[display].pending;
        if (pending != 0 && value_out != NULL)
            *value_out = g_runtime.screen[display].value;
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return pending;
}
#endif

