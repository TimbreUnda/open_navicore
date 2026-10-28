#define _POSIX_C_SOURCE 200112L

#include "open_navi_renderer.h"

#include "open_navi_default_backend.h"
#include "open_navi_renderer_internal.h"
#include "open_navi_renderer_platform.h"

#include <pthread.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>


RendererRuntime g_runtime = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .state = RENDERER_COLD
};
uint64_t open_navi_renderer_next_event_sequence_locked(void)
{
    ++g_runtime.next_event_sequence;
    if (g_runtime.next_event_sequence == 0u)
        ++g_runtime.next_event_sequence;
    return g_runtime.next_event_sequence;
}

int open_navi_renderer_event_available_locked(void)
{
    unsigned int display;
    if (g_runtime.pointer.count != 0u || g_runtime.gps.pending != 0 ||
        g_runtime.vehicle.pending != 0 ||
        g_runtime.gps_interval.pending != 0 ||
        g_runtime.audio.pending != 0 ||
        g_runtime.internationalization.pending != 0 ||
        g_runtime.cluster_display.pending != 0 ||
        g_runtime.driver_distraction.pending != 0 ||
        g_runtime.guidance_stop.pending != 0 ||
        g_runtime.frame_pending != 0) return 1;
    for (display = 0u; display < OPEN_NAVI_RENDERER_DISPLAY_COUNT; ++display) {
        if (g_runtime.visibility[display].pending != 0 ||
            g_runtime.screen[display].pending != 0) return 1;
    }
    return 0;
}

int open_navi_renderer_request_frame(void *runtime_context)
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
        g_runtime.frame_event_sequence = open_navi_renderer_next_event_sequence_locked();
        (void)pthread_cond_signal(&g_runtime.condition);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

/* Waits, with g_runtime.lock held, until an event is pending or stop is
 * requested. Returns 1 when an event is ready, 0 when stopping, 2 when the
 * poll interval elapsed with nothing pending and -1 on a wait failure. */
static int wait_for_event_locked(void)
{
    while (g_runtime.stop == 0 && !open_navi_renderer_event_available_locked()) {
        struct timespec deadline;
        int wait_result;
        if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) return -1;
        deadline.tv_nsec +=
            (long)OPEN_NAVI_PLATFORM_POLL_MILLISECONDS * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            ++deadline.tv_sec;
            deadline.tv_nsec -= 1000000000L;
        }
        wait_result = pthread_cond_timedwait(&g_runtime.condition,
                                             &g_runtime.lock, &deadline);
        if (wait_result == ETIMEDOUT && !open_navi_renderer_event_available_locked()) return 2;
        if (wait_result != 0 && wait_result != ETIMEDOUT) return -1;
    }
    return g_runtime.stop != 0 ? 0 : 1;
}

/* Picks the pending event with the lowest sequence number, so events are
 * delivered in the order they were posted. Returns 0 when nothing is
 * pending. *display_out is written only for per-display event types
 * (visibility, screen); the caller supplies its default. */
static RenderEventType select_next_event_locked(unsigned int *display_out)
{
    uint64_t selected = UINT64_MAX;
    RenderEventType type = 0;
    unsigned int display;

/* Uses the locals `selected` and `type` of this function. */
#define SELECT_IF_OLDER(slot, event_type)                    \
    do {                                                     \
        if (g_runtime.slot.pending != 0 &&                   \
            g_runtime.slot.sequence < selected) {            \
            selected = g_runtime.slot.sequence;              \
            type = (event_type);                             \
        }                                                    \
    } while (0)

    if (g_runtime.pointer.count != 0u) {
        selected = g_runtime.pointer.queue[g_runtime.pointer.head].sequence;
        type = RENDER_EVENT_POINTER;
    }
    for (display = 0u; display < OPEN_NAVI_RENDERER_DISPLAY_COUNT; ++display) {
        if (g_runtime.visibility[display].pending != 0 &&
            g_runtime.visibility[display].sequence < selected) {
            selected = g_runtime.visibility[display].sequence;
            *display_out = display;
            type = RENDER_EVENT_VISIBILITY;
        }
        if (g_runtime.screen[display].pending != 0 &&
            g_runtime.screen[display].sequence < selected) {
            selected = g_runtime.screen[display].sequence;
            *display_out = display;
            type = RENDER_EVENT_SCREEN;
        }
    }
    SELECT_IF_OLDER(gps, RENDER_EVENT_GPS);
    SELECT_IF_OLDER(vehicle, RENDER_EVENT_VEHICLE);
    SELECT_IF_OLDER(gps_interval, RENDER_EVENT_GPS_INTERVAL);
    SELECT_IF_OLDER(audio, RENDER_EVENT_AUDIO);
    SELECT_IF_OLDER(internationalization, RENDER_EVENT_INTERNATIONALIZATION);
    SELECT_IF_OLDER(cluster_display, RENDER_EVENT_CLUSTER_DISPLAY);
    SELECT_IF_OLDER(driver_distraction, RENDER_EVENT_DRIVER_DISTRACTION);
    SELECT_IF_OLDER(guidance_stop, RENDER_EVENT_GUIDANCE_STOP);
#undef SELECT_IF_OLDER
    if (g_runtime.frame_pending != 0 &&
        g_runtime.frame_event_sequence < selected) {
        type = RENDER_EVENT_FRAME;
    }
    return type;
}

/* Moves the event chosen by select_next_event_locked() out of its pending
 * slot into *event. Returns 0 for a type that is not a known event. */
static int pop_event_locked(RenderEventType type, unsigned int display,
                            RenderEvent *event)
{
    memset(event, 0, sizeof(*event));
    event->type = type;
    switch (type) {
    case RENDER_EVENT_POINTER:
        *event = g_runtime.pointer.queue[g_runtime.pointer.head];
        g_runtime.pointer.head =
            (g_runtime.pointer.head + 1u) % OPEN_NAVI_POINTER_QUEUE_CAPACITY;
        --g_runtime.pointer.count;
        break;
    case RENDER_EVENT_VISIBILITY:
        event->sequence = g_runtime.visibility[display].sequence;
        event->data.visibility = g_runtime.visibility[display].value;
        g_runtime.visibility[display].pending = 0;
        break;
    case RENDER_EVENT_SCREEN:
        event->sequence = g_runtime.screen[display].sequence;
        event->data.screen = g_runtime.screen[display].value;
        g_runtime.screen[display].pending = 0;
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
    case RENDER_EVENT_GUIDANCE_STOP:
        event->sequence = g_runtime.guidance_stop.sequence;
        event->data.guidance_stop = g_runtime.guidance_stop.value;
        g_runtime.guidance_stop.pending = 0;
        break;
    case RENDER_EVENT_FRAME:
        event->sequence = g_runtime.frame_event_sequence;
        g_runtime.frame_pending = 0;
        break;
    default:
        return 0;
    }
    return 1;
}

/* Blocks for the next event. Returns 1 with *event filled, 0 on stop (or an
 * unknown event), 2 on poll timeout and -1 on error. */
int open_navi_renderer_take_event(RenderEvent *event)
{
    unsigned int display = 0u;
    RenderEventType type;
    int result;

    (void)pthread_mutex_lock(&g_runtime.lock);
    result = wait_for_event_locked();
    if (result == 1) {
        type = select_next_event_locked(&display);
        result = pop_event_locked(type, display, event);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_renderer_notify_visibility(uint32_t display, uint32_t visibility,
                                    uint32_t interactivity)
{
    return open_navi_renderer_notify_visibility_area(display, visibility,
                                                     interactivity, NULL);
}

/* points: the native window attribute, four (x, y) float pairs in window
 * pixels (corner order not recovered, so only their bounding box is used);
 * NULL or a non-finite / empty box leaves the area empty. */
/* Nearest integer without libm (the provider links no math library). */
static int32_t round_to_int(float value)
{
    return (int32_t)(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

static void window_area_from_points(const float *points,
                                    OpenNaviBackendVisibilityV2 *value)
{
    float min_x, min_y, max_x, max_y;
    int32_t origin_x = 0, origin_y = 0;
    unsigned int index;

    value->area_x = 0;
    value->area_y = 0;
    value->area_width = 0u;
    value->area_height = 0u;
    if (points == NULL) return;
    min_x = max_x = points[0];
    min_y = max_y = points[1];
    for (index = 0u; index < 4u; ++index) {
        const float x = points[index * 2u];
        const float y = points[index * 2u + 1u];
        if (!(x > -65536.0f && x < 65536.0f && y > -65536.0f && y < 65536.0f))
            return;
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
    }
    if (max_x - min_x < 1.0f || max_y - min_y < 1.0f) return;
    open_navi_renderer_platform_window_origin(&origin_x, &origin_y);
    value->area_x = round_to_int(min_x) - origin_x;
    value->area_y = round_to_int(min_y) - origin_y;
    value->area_width = (uint32_t)round_to_int(max_x - min_x);
    value->area_height = (uint32_t)round_to_int(max_y - min_y);
}

int open_navi_renderer_notify_visibility_area(uint32_t display,
                                              uint32_t visibility,
                                              uint32_t interactivity,
                                              const float *points)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (display >= OPEN_NAVI_RENDERER_DISPLAY_COUNT) {
        if (g_runtime.counters.visibility_unsupported != UINT32_MAX)
            ++g_runtime.counters.visibility_unsupported;
        result = -2;
    } else if (g_runtime.accepting_state == 0 ||
        g_runtime.state == RENDERER_STOPPING) {
        result = -1;
    } else {
        VisibilitySlot *slot = &g_runtime.visibility[display];
        slot->value.abi_version = OPEN_NAVI_BACKEND_ABI_V1;
        slot->value.struct_size = sizeof(OpenNaviBackendVisibilityV1);
        slot->value.display = display;
        slot->value.visibility = visibility;
        slot->value.interactivity = interactivity;
        window_area_from_points(points, &slot->value);
        slot->sequence = open_navi_renderer_next_event_sequence_locked();
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
        if (g_runtime.counters.screen_unsupported != UINT32_MAX)
            ++g_runtime.counters.screen_unsupported;
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
        slot->sequence = open_navi_renderer_next_event_sequence_locked();
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
        g_runtime.gps.sequence = open_navi_renderer_next_event_sequence_locked();
        g_runtime.gps.pending = 1;
        (void)pthread_cond_signal(&g_runtime.condition);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

/* While a backend is starting or running its negotiated version applies;
 * otherwise the installed one does. */
static int renderer_abi_at_least_locked(uint32_t first_version)
{
    const uint32_t version =
        (g_runtime.state == RENDERER_STARTING ||
         g_runtime.state == RENDERER_RUNNING)
            ? g_runtime.backend.active_version
            : g_runtime.backend.installed_version;
    return version >= first_version && version <= OPEN_NAVI_BACKEND_ABI_V6;
}

static int renderer_v2_enabled_locked(void)
{
    return renderer_abi_at_least_locked(OPEN_NAVI_BACKEND_ABI_V2);
}

static int renderer_v4_enabled_locked(void)
{
    return renderer_abi_at_least_locked(OPEN_NAVI_BACKEND_ABI_V4);
}

static int renderer_v5_enabled_locked(void)
{
    return renderer_abi_at_least_locked(OPEN_NAVI_BACKEND_ABI_V5);
}

static int renderer_v6_enabled_locked(void)
{
    return renderer_abi_at_least_locked(OPEN_NAVI_BACKEND_ABI_V6);
}

static int notify_vehicle(uint32_t source_slot,
                          uint32_t request_handle,
                          uint32_t field,
                          uint32_t value, uint32_t unit)
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
            changed = (vehicle->displayed_vehicle_speed_bits != value ||
                       vehicle->displayed_vehicle_speed_unit != unit) ? field : 0u;
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
        else {
            vehicle->displayed_vehicle_speed_bits = value;
            vehicle->displayed_vehicle_speed_unit = unit;
        }
        g_runtime.vehicle.sequence = open_navi_renderer_next_event_sequence_locked();
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
                          (uint32_t)value, 0u);
}

int open_navi_renderer_notify_vehicle_setting(uint32_t source_slot,
                                         uint32_t request_handle,
                                         uint8_t value)
{
    return notify_vehicle(source_slot, request_handle,
                          OPEN_NAVI_BACKEND_VEHICLE_DAY_NIGHT_SETTING,
                          (uint32_t)value, 0u);
}

int open_navi_renderer_notify_vehicle_speed(uint32_t source_slot,
                                       uint32_t speed_bits, uint32_t unit)
{
    return notify_vehicle(source_slot, 0u,
                          OPEN_NAVI_BACKEND_VEHICLE_SPEED, speed_bits, unit);
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
        g_runtime.gps_interval.sequence = open_navi_renderer_next_event_sequence_locked();
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
        g_runtime.audio.sequence = open_navi_renderer_next_event_sequence_locked();
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
            open_navi_renderer_next_event_sequence_locked();
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
        g_runtime.cluster_display.sequence = open_navi_renderer_next_event_sequence_locked();
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
        g_runtime.driver_distraction.sequence = open_navi_renderer_next_event_sequence_locked();
        g_runtime.driver_distraction.pending = 1;
        (void)pthread_cond_signal(&g_runtime.condition);
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_renderer_notify_guidance_stop(uint32_t source_slot,
                                            uint32_t request_handle)
{
    OpenNaviBackendGuidanceStopV1 *event;
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.accepting_state == 0 ||
        g_runtime.state == RENDERER_STOPPING) {
        result = -1;
    } else if (renderer_v6_enabled_locked() == 0) {
        result = 1;
    } else {
        event = &g_runtime.guidance_stop.value;
        memset(event, 0, sizeof(*event));
        event->abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
        event->struct_size = sizeof(*event);
        event->source_slot = source_slot;
        event->request_handle = request_handle;
        g_runtime.guidance_stop.sequence = open_navi_renderer_next_event_sequence_locked();
        g_runtime.guidance_stop.pending = 1;
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
    return open_navi_renderer_enqueue_pointer(&pointer);
}

uint32_t open_navi_renderer_test_pointer_accepted(void)
{
    uint32_t result;
    (void)pthread_mutex_lock(&g_runtime.lock);
    result = g_runtime.pointer.accepted;
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

uint32_t open_navi_renderer_test_pointer_dropped(void)
{
    uint32_t result;
    (void)pthread_mutex_lock(&g_runtime.lock);
    result = g_runtime.pointer.dropped;
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

void open_navi_renderer_test_pointer_stats(OpenNaviRendererPointerQueueStats *stats_out)
{
    if (stats_out == NULL) return;
    (void)pthread_mutex_lock(&g_runtime.lock);
    stats_out->accepted = g_runtime.pointer.accepted;
    stats_out->dropped = g_runtime.pointer.dropped;
    stats_out->move_coalesced = g_runtime.pointer.move_coalesced;
    stats_out->move_dropped = g_runtime.pointer.move_dropped;
    stats_out->edge_dropped = g_runtime.pointer.edge_dropped;
    stats_out->move_evicted = g_runtime.pointer.move_evicted;
    stats_out->gesture_pairs_evicted =
        g_runtime.pointer.gesture_pairs_evicted;
    stats_out->platform_ignored = g_runtime.counters.platform_ignored;
    stats_out->platform_errors = g_runtime.counters.platform_errors;
    stats_out->platform_window_lost = g_runtime.counters.platform_window_lost;
    (void)pthread_mutex_unlock(&g_runtime.lock);
}

uint32_t open_navi_renderer_test_visibility_unsupported(void)
{
    uint32_t result;
    (void)pthread_mutex_lock(&g_runtime.lock);
    result = g_runtime.counters.visibility_unsupported;
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

uint32_t open_navi_renderer_test_screen_unsupported(void)
{
    uint32_t result;
    (void)pthread_mutex_lock(&g_runtime.lock);
    result = g_runtime.counters.screen_unsupported;
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
            memcpy(value_out, &g_runtime.visibility[display].value,
                   sizeof(*value_out));
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

