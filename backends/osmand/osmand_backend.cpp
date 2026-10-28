#include <open_navicore/backend_plugin.h>
#include "session.h"
#include "diagnostics.h"

#include <EGL/egl.h>
#include <climits>
#include <cstring>

namespace {
using namespace navicore_osmand;

Session& session()
{
    static Session instance(make_engine);
    return instance;
}

template <typename T>
bool valid(const T* payload, uint32_t version = OPEN_NAVI_BACKEND_ABI_V1) noexcept
{
    // The caller must supply an accessible two-word header. Validate it
    // before copying or reading any payload fields; ignore unknown tails.
    return payload && payload->abi_version == version &&
           payload->struct_size >= sizeof(T);
}

bool valid_target(const OpenNaviBackendRenderTargetV1* target) noexcept
{
    return valid(target) && target->width > 0 && target->height > 0 &&
           target->width <= INT32_MAX && target->height <= INT32_MAX &&
           target->native_pointer_bytes == sizeof(void*) &&
           target->native_egl_context != 0;
}

uintptr_t current_context() noexcept
{
    return reinterpret_cast<uintptr_t>(eglGetCurrentContext());
}

int backend_start(void* context, const OpenNaviBackendServicesV1* services) noexcept
{
    if (!context || !valid(services) || !services->request_frame)
        return -1;
    try {
        return static_cast<Session*>(context)->start(*services, Config::from_environment());
    } catch (...) {
        trace("error=exception_start");
        return -1;
    }
}

void backend_stop(void* context) noexcept
{
    if (context)
        static_cast<Session*>(context)->stop();
}

int backend_surface_created(void* context,
                            const OpenNaviBackendRenderTargetV1* target) noexcept
{
    if (!context || !valid_target(target) ||
        current_context() != target->native_egl_context)
        return -1;
    return static_cast<Session*>(context)->attach(*target);
}

void backend_surface_destroyed(void* context) noexcept
{
    if (context)
        static_cast<Session*>(context)->detach(current_context());
}

template <typename T, uint32_t Version = OPEN_NAVI_BACKEND_ABI_V1>
int accept_event(void* context, const T* event) noexcept
{
    // GPS interval and audio state are not used by the navigation.
    return context && valid(event, Version) &&
           static_cast<Session*>(context)->accepts_events() ? 0 : -1;
}

// Platform inputs the session records and forwards to the engine.
template <typename T, uint32_t Version = OPEN_NAVI_BACKEND_ABI_V1>
int forward_event(void* context, const T* event) noexcept
{
    if (!context || !valid(event, Version))
        return -1;
    return static_cast<Session*>(context)->input(*event);
}

int backend_vehicle(void* context, const OpenNaviBackendVehicleV2* event) noexcept
{
    constexpr size_t prefix = offsetof(OpenNaviBackendVehicleV2, displayed_vehicle_speed_unit);
    if (!context || !event || event->abi_version != OPEN_NAVI_BACKEND_ABI_V2 ||
        event->struct_size < prefix)
        return -1;
    OpenNaviBackendVehicleV2 copy{};
    std::memcpy(&copy, event, event->struct_size >= sizeof(copy) ? sizeof(copy) : prefix);
    return static_cast<Session*>(context)->input(copy);
}

// Backend V3+: started with the outbound services (V2 prefix size).
int backend_start_v2(void* context, const OpenNaviBackendServicesV2* services) noexcept
{
    if (!context || !services || services->v1.abi_version != OPEN_NAVI_BACKEND_ABI_V2 ||
        services->v1.struct_size < sizeof(*services) || !services->v1.request_frame)
        return -1;
    try {
        return static_cast<Session*>(context)->start_v2(*services, Config::from_environment());
    } catch (...) {
        trace("error=exception_start");
        return -1;
    }
}

// Backend V6: started with the guidance state service as well.
int backend_start_v3(void* context, const OpenNaviBackendServicesV3* services) noexcept
{
    if (!context || !services || services->v2.v1.abi_version != OPEN_NAVI_BACKEND_ABI_V3 ||
        services->v2.v1.struct_size < sizeof(*services) || !services->v2.v1.request_frame ||
        !services->publish_guidance_state)
        return -1;
    try {
        return static_cast<Session*>(context)->start_v3(*services, Config::from_environment());
    } catch (...) {
        trace("error=exception_start");
        return -1;
    }
}

int backend_guidance_stop(void* context, const OpenNaviBackendGuidanceStopV1* event) noexcept
{
    if (!context || !valid(event))
        return -1;
    return static_cast<Session*>(context)->guidance_stop(*event);
}

// V2 payloads carry the touch contact (we declare the contacts capability);
// a V1 payload is the first contact.
int backend_pointer(void* context, const OpenNaviBackendPointerV1* event) noexcept
{
    if (!context || !event)
        return -1;
    OpenNaviBackendPointerV2 pointer{};
    if (event->abi_version == OPEN_NAVI_BACKEND_POINTER_ABI_V2 &&
        event->struct_size >= sizeof(OpenNaviBackendPointerV2)) {
        std::memcpy(&pointer, event, sizeof(pointer));
    } else if (valid(event)) {
        std::memcpy(&pointer, event, sizeof(*event));
        pointer.contact_id = 0;
        pointer.contact_count = event->action == OPEN_NAVI_BACKEND_POINTER_RELEASE ? 0u : 1u;
    } else {
        return -1;
    }
    return static_cast<Session*>(context)->pointer(pointer);
}

// V2 payloads carry the window area the HMI shows (we declare the window
// area capability); a V1 payload has no area.
int backend_visibility(void* context, const OpenNaviBackendVisibilityV1* event) noexcept
{
    if (!context || !event)
        return -1;
    OpenNaviBackendVisibilityV2 visibility{};
    if (event->abi_version == OPEN_NAVI_BACKEND_VISIBILITY_ABI_V2 &&
        event->struct_size >= sizeof(OpenNaviBackendVisibilityV2)) {
        std::memcpy(&visibility, event, sizeof(visibility));
    } else if (valid(event)) {
        std::memcpy(&visibility, event, sizeof(*event));
    } else {
        return -1;
    }
    return static_cast<Session*>(context)->input(visibility);
}

int backend_gps(void* context, const OpenNaviBackendGpsV1* event) noexcept
{
    if (!context || !valid(event))
        return -1;
    return static_cast<Session*>(context)->gps(*event);
}

int backend_render(void* context, const OpenNaviBackendRenderTargetV1* target,
                   const OpenNaviBackendFrameV1* frame) noexcept
{
    if (!context || !valid_target(target) || !valid(frame) ||
        current_context() != target->native_egl_context)
        return -1;
    const auto result = static_cast<Session*>(context)->frame(*target);
    // Query V1 has no no-swap/pending result. Both Pending and Presented must
    // remain zero: nonzero permanently selects the loader's fallback.
    return result.outcome == FrameOutcome::Fault ? -1 : 0;
}

void fill_v1_query(OpenNaviBackendPluginV1* output)
{
    static const uint8_t build_id[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
        OPEN_NAVI_PLUGIN_EXPECTED_BUILD_ID_INITIALIZER;
    std::memset(output, 0, sizeof(*output));
    output->magic0 = OPEN_NAVI_PLUGIN_MAGIC0;
    output->magic1 = OPEN_NAVI_PLUGIN_MAGIC1;
    output->query_abi_version = OPEN_NAVI_PLUGIN_QUERY_ABI_V1;
    output->struct_size = sizeof(*output);
    output->endian_tag = OPEN_NAVI_PLUGIN_ENDIAN_TAG;
    output->pointer_bytes = sizeof(void*);
    output->backend_abi_version = OPEN_NAVI_BACKEND_ABI_V2;
    output->backend_v1_size = sizeof(OpenNaviBackendV1);
    output->backend_v2_size = sizeof(OpenNaviBackendV2);
    output->render_target_v1_size = sizeof(OpenNaviBackendRenderTargetV1);
    output->frame_v1_size = sizeof(OpenNaviBackendFrameV1);
    output->gps_v1_size = sizeof(OpenNaviBackendGpsV1);
    output->visibility_v1_size = sizeof(OpenNaviBackendVisibilityV1);
    output->screen_v1_size = sizeof(OpenNaviBackendScreenV1);
    output->pointer_v1_size = sizeof(OpenNaviBackendPointerV1);
    output->services_v1_size = sizeof(OpenNaviBackendServicesV1);
    output->vehicle_v2_size = sizeof(OpenNaviBackendVehicleV2);
    output->gps_interval_v2_size = sizeof(OpenNaviBackendGpsIntervalV2);
    output->audio_v2_size = sizeof(OpenNaviBackendAudioV2);
    output->capabilities = OPEN_NAVI_PLUGIN_REQUIRED_CAPABILITIES |
                           OPEN_NAVI_PLUGIN_CAP_POINTER_CONTACTS |
                           OPEN_NAVI_PLUGIN_CAP_WINDOW_AREA;
    std::memcpy(output->build_id, build_id, sizeof(build_id));
    output->backend.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V2;
    output->backend.v1.struct_size = sizeof(OpenNaviBackendV2);
    output->backend.v1.backend_context = &session();
    output->backend.v1.start = backend_start;
    output->backend.v1.stop = backend_stop;
    output->backend.v1.surface_created = backend_surface_created;
    output->backend.v1.surface_destroyed = backend_surface_destroyed;
    output->backend.v1.visibility_changed = backend_visibility;
    output->backend.v1.screen_changed = forward_event<OpenNaviBackendScreenV1>;
    output->backend.v1.gps_changed = backend_gps;
    output->backend.v1.pointer_changed = backend_pointer;
    output->backend.v1.render_frame = backend_render;
    output->backend.vehicle_changed = backend_vehicle;
    output->backend.gps_interval_changed =
        accept_event<OpenNaviBackendGpsIntervalV2, OPEN_NAVI_BACKEND_ABI_V2>;
    output->backend.audio_changed = accept_event<OpenNaviBackendAudioV2, OPEN_NAVI_BACKEND_ABI_V2>;
}

// Query V4 selects backend V5: the V1 query is the prefix; the outbound
// services, language, cluster display and driver distraction are added.
void fill_v4_query(OpenNaviBackendPluginV4* output)
{
    static const uint8_t build_id[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
        OPEN_NAVI_PLUGIN_V4_EXPECTED_BUILD_ID_INITIALIZER;
    OpenNaviBackendPluginV1 legacy;
    fill_v1_query(&legacy);
    std::memset(output, 0, sizeof(*output));
    OpenNaviBackendPluginV1& v1 = output->v3.v2.v1;
    v1 = legacy;
    v1.query_abi_version = OPEN_NAVI_PLUGIN_QUERY_ABI_V4;
    v1.struct_size = sizeof(*output);
    v1.backend_abi_version = OPEN_NAVI_BACKEND_ABI_V5;
    std::memcpy(v1.build_id, build_id, sizeof(build_id));
    OpenNaviBackendPluginV2& v2 = output->v3.v2;
    v2.services_v2_size = sizeof(OpenNaviBackendServicesV2);
    v2.cluster_v1_size = sizeof(OpenNaviBackendClusterV1);
    v2.utf8_span_v1_size = sizeof(OpenNaviBackendUtf8SpanV1);
    v2.audio_availability_v1_size = sizeof(OpenNaviBackendAudioAvailabilityV1);
    v2.thread_status_v1_size = sizeof(OpenNaviBackendThreadStatusV1);
    v2.backend_v3_size = sizeof(OpenNaviBackendV3);
    v2.capabilities_v2 = OPEN_NAVI_PLUGIN_V2_REQUIRED_CAPABILITIES;
    OpenNaviBackendPluginV3& v3 = output->v3;
    v3.internationalization_v1_size = sizeof(OpenNaviBackendInternationalizationV1);
    v3.cluster_display_v1_size = sizeof(OpenNaviBackendClusterDisplayV1);
    v3.backend_v4_size = sizeof(OpenNaviBackendV4);
    v3.capabilities_v3 = OPEN_NAVI_PLUGIN_V3_REQUIRED_CAPABILITIES;
    output->driver_distraction_v1_size = sizeof(OpenNaviBackendDriverDistractionV1);
    output->backend_v5_size = sizeof(OpenNaviBackendV5);
    output->capabilities_v4 = OPEN_NAVI_PLUGIN_V4_REQUIRED_CAPABILITIES;
    OpenNaviBackendV5& backend = output->backend_v5;
    backend.v4.v3.v2 = legacy.backend;
    backend.v4.v3.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V5;
    backend.v4.v3.v2.v1.struct_size = sizeof(OpenNaviBackendV5);
    backend.v4.v3.start_v2 = backend_start_v2;
    backend.v4.internationalization_changed = forward_event<OpenNaviBackendInternationalizationV1>;
    backend.v4.cluster_display_changed = forward_event<OpenNaviBackendClusterDisplayV1>;
    backend.driver_distraction_changed = forward_event<OpenNaviBackendDriverDistractionV1>;
}

// Query V5 selects backend V6: the complete query V4 (backend V5 prefix),
// then the guidance state service and the guidance stop input.
void fill_v5_query(OpenNaviBackendPluginV5* output)
{
    static const uint8_t build_id[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
        OPEN_NAVI_PLUGIN_V5_EXPECTED_BUILD_ID_INITIALIZER;
    OpenNaviBackendPluginV4 prefix;
    fill_v4_query(&prefix);
    std::memset(output, 0, sizeof(*output));
    output->v4 = prefix;
    OpenNaviBackendPluginV1& v1 = output->v4.v3.v2.v1;
    v1.query_abi_version = OPEN_NAVI_PLUGIN_QUERY_ABI_V5;
    v1.struct_size = sizeof(*output);
    v1.backend_abi_version = OPEN_NAVI_BACKEND_ABI_V6;
    std::memcpy(v1.build_id, build_id, sizeof(build_id));
    output->services_v3_size = sizeof(OpenNaviBackendServicesV3);
    output->guidance_state_v1_size = sizeof(OpenNaviBackendGuidanceStateV1);
    output->guidance_stop_v1_size = sizeof(OpenNaviBackendGuidanceStopV1);
    output->backend_v6_size = sizeof(OpenNaviBackendV6);
    output->capabilities_v5 = OPEN_NAVI_PLUGIN_V5_REQUIRED_CAPABILITIES;
    OpenNaviBackendV6& backend = output->backend_v6;
    backend.v5 = prefix.backend_v5;
    backend.v5.v4.v3.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V6;
    backend.v5.v4.v3.v2.v1.struct_size = sizeof(OpenNaviBackendV6);
    backend.start_v3 = backend_start_v3;
    backend.guidance_stop_requested = backend_guidance_stop;
}

}  // namespace

// The loader asks for query V5 first (backend V6), then V4 (backend V5),
// then older queries.
extern "C" OPEN_NAVI_PLUGIN_EXPORT int open_navi_backend_query_v1(
    uint32_t requested_query_abi, uint32_t output_size,
    OpenNaviBackendPluginV1* output)
{
    if (output == nullptr)
        return -1;
    try {
        if (requested_query_abi == OPEN_NAVI_PLUGIN_QUERY_ABI_V5 &&
            output_size == sizeof(OpenNaviBackendPluginV5)) {
            fill_v5_query(reinterpret_cast<OpenNaviBackendPluginV5*>(output));
            return 0;
        }
        if (requested_query_abi == OPEN_NAVI_PLUGIN_QUERY_ABI_V4 &&
            output_size == sizeof(OpenNaviBackendPluginV4)) {
            fill_v4_query(reinterpret_cast<OpenNaviBackendPluginV4*>(output));
            return 0;
        }
        if (requested_query_abi != OPEN_NAVI_PLUGIN_QUERY_ABI_V1 ||
            output_size != sizeof(*output))
            return -1;
        fill_v1_query(output);
        return 0;
    } catch (...) {
        trace("error=exception_query");
        return -1;
    }
}
