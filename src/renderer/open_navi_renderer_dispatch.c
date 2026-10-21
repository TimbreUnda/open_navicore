#define _POSIX_C_SOURCE 200112L

#include "open_navi_renderer_internal.h"

#include "open_navi_renderer_platform.h"

#include <string.h>
#include <time.h>

static uint64_t monotonic_milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0u;
    return (uint64_t)now.tv_sec * 1000u +
           (uint64_t)now.tv_nsec / 1000000u;
}

static void dispatch_visibility(const RenderEvent *event,
                                const OpenNaviBackendV1 *backend)
{
    if (backend->visibility_changed != NULL) {
        OpenNaviBackendVisibilityV2 visibility = event->data.visibility;
        if (g_runtime.window_area != 0) {
            visibility.abi_version = OPEN_NAVI_BACKEND_VISIBILITY_ABI_V2;
            visibility.struct_size = sizeof(visibility);
        }
        (void)backend->visibility_changed(
            backend->backend_context,
            (const OpenNaviBackendVisibilityV1 *)&visibility);
    }
}

static void dispatch_pointer(const RenderEvent *event,
                             const OpenNaviBackendV1 *backend)
{
    if (backend->pointer_changed != NULL) {
        if (g_runtime.pointer_contacts != 0) {
            /* V2 shares the exact V1 prefix; the callback type is the
             * V1 pointer, the payload says V2 by version and size. */
            (void)backend->pointer_changed(
                backend->backend_context,
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
            (void)backend->pointer_changed(backend->backend_context, &v1);
        }
    }
}

static void dispatch_frame(const OpenNaviBackendV1 *backend)
{
    if (backend->render_frame != NULL) {
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
            int render_status = backend->render_frame(
                backend->backend_context, &g_runtime.target, &frame);
            (void)open_navi_renderer_platform_end_frame(
                render_status == 0, diagnostic, sizeof(diagnostic));
        }
    }
}

void open_navi_renderer_dispatch_event(const RenderEvent *event, uintptr_t service_token)
{
    OpenNaviBackendV1 backend = g_runtime.backend.active;
    OpenNaviBackendV2 backend_v2 = g_runtime.backend.active_v2;
    OpenNaviBackendV4 backend_v4 = g_runtime.backend.active_v4;
    OpenNaviBackendV5 backend_v5 = g_runtime.backend.active_v5;
    OpenNaviBackendV6 backend_v6 = g_runtime.backend.active_v6;
    int request_render = event->type != RENDER_EVENT_FRAME;

    switch (event->type) {
    case RENDER_EVENT_VISIBILITY:
        dispatch_visibility(event, &backend);
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
        if (active_abi_at_least(OPEN_NAVI_BACKEND_ABI_V2) &&
            backend_v2.vehicle_changed != NULL) {
            (void)backend_v2.vehicle_changed(
                backend.backend_context, &event->data.vehicle);
        }
        break;
    case RENDER_EVENT_GPS_INTERVAL:
        if (active_abi_at_least(OPEN_NAVI_BACKEND_ABI_V2) &&
            backend_v2.gps_interval_changed != NULL) {
            (void)backend_v2.gps_interval_changed(
                backend.backend_context, &event->data.gps_interval);
        }
        break;
    case RENDER_EVENT_AUDIO:
        if (active_abi_at_least(OPEN_NAVI_BACKEND_ABI_V2) &&
            backend_v2.audio_changed != NULL) {
            (void)backend_v2.audio_changed(
                backend.backend_context, &event->data.audio);
        }
        break;
    case RENDER_EVENT_INTERNATIONALIZATION:
        if (active_abi_at_least(OPEN_NAVI_BACKEND_ABI_V4) &&
            backend_v4.internationalization_changed != NULL) {
            (void)backend_v4.internationalization_changed(
                backend.backend_context,
                &event->data.internationalization);
        }
        break;
    case RENDER_EVENT_CLUSTER_DISPLAY:
        if (active_abi_at_least(OPEN_NAVI_BACKEND_ABI_V4) &&
            backend_v4.cluster_display_changed != NULL) {
            (void)backend_v4.cluster_display_changed(
                backend.backend_context, &event->data.cluster_display);
        }
        break;
    case RENDER_EVENT_DRIVER_DISTRACTION:
        if (active_abi_at_least(OPEN_NAVI_BACKEND_ABI_V5) &&
            backend_v5.driver_distraction_changed != NULL) {
            (void)backend_v5.driver_distraction_changed(
                backend.backend_context,
                &event->data.driver_distraction);
        }
        break;
    case RENDER_EVENT_GUIDANCE_STOP:
        if (active_abi_at_least(OPEN_NAVI_BACKEND_ABI_V6) &&
            backend_v6.guidance_stop_requested != NULL) {
            (void)backend_v6.guidance_stop_requested(
                backend.backend_context, &event->data.guidance_stop);
        }
        break;
    case RENDER_EVENT_POINTER:
        dispatch_pointer(event, &backend);
        break;
    case RENDER_EVENT_FRAME:
        dispatch_frame(&backend);
        break;
    default:
        break;
    }
    if (request_render != 0)
        (void)open_navi_renderer_request_frame((void *)service_token);
}
