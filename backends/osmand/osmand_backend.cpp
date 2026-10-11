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
bool valid(const T* payload, uint32_t version = MOCK_NAV_BACKEND_ABI_V1) noexcept
{
    // The caller must supply an accessible two-word header. Validate it
    // before copying or reading any payload fields; ignore unknown tails.
    return payload && payload->abi_version == version &&
           payload->struct_size >= sizeof(T);
}

bool valid_target(const MockNavBackendRenderTargetV1* target) noexcept
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

int backend_start(void* context, const MockNavBackendServicesV1* services) noexcept
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
                            const MockNavBackendRenderTargetV1* target) noexcept
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

template <typename T, uint32_t Version = MOCK_NAV_BACKEND_ABI_V1>
int accept_event(void* context, const T* event) noexcept
{
    // GPS/camera, input, vehicle and audio remain intentionally unimplemented.
    return context && valid(event, Version) &&
           static_cast<Session*>(context)->accepts_events() ? 0 : -1;
}

int backend_pointer(void* context, const MockNavBackendPointerV1* event) noexcept
{
    if (!context || !valid(event))
        return -1;
    return static_cast<Session*>(context)->pointer(*event);
}

int backend_gps(void* context, const MockNavBackendGpsV1* event) noexcept
{
    if (!context || !valid(event))
        return -1;
    return static_cast<Session*>(context)->gps(*event);
}

int backend_render(void* context, const MockNavBackendRenderTargetV1* target,
                   const MockNavBackendFrameV1* frame) noexcept
{
    if (!context || !valid_target(target) || !valid(frame) ||
        current_context() != target->native_egl_context)
        return -1;
    const auto result = static_cast<Session*>(context)->frame(*target);
    // Query V1 has no no-swap/pending result. Both Pending and Presented must
    // remain zero: nonzero permanently selects the loader's fallback.
    return result.outcome == FrameOutcome::Fault ? -1 : 0;
}

void fill_v1_query(MockNavBackendPluginV1* output)
{
    static const uint8_t build_id[MOCK_NAV_PLUGIN_BUILD_ID_BYTES] =
        MOCK_NAV_PLUGIN_EXPECTED_BUILD_ID_INITIALIZER;
    std::memset(output, 0, sizeof(*output));
    output->magic0 = MOCK_NAV_PLUGIN_MAGIC0;
    output->magic1 = MOCK_NAV_PLUGIN_MAGIC1;
    output->query_abi_version = MOCK_NAV_PLUGIN_QUERY_ABI_V1;
    output->struct_size = sizeof(*output);
    output->endian_tag = MOCK_NAV_PLUGIN_ENDIAN_TAG;
    output->pointer_bytes = sizeof(void*);
    output->backend_abi_version = MOCK_NAV_BACKEND_ABI_V2;
    output->backend_v1_size = sizeof(MockNavBackendV1);
    output->backend_v2_size = sizeof(MockNavBackendV2);
    output->render_target_v1_size = sizeof(MockNavBackendRenderTargetV1);
    output->frame_v1_size = sizeof(MockNavBackendFrameV1);
    output->gps_v1_size = sizeof(MockNavBackendGpsV1);
    output->visibility_v1_size = sizeof(MockNavBackendVisibilityV1);
    output->screen_v1_size = sizeof(MockNavBackendScreenV1);
    output->pointer_v1_size = sizeof(MockNavBackendPointerV1);
    output->services_v1_size = sizeof(MockNavBackendServicesV1);
    output->vehicle_v2_size = sizeof(MockNavBackendVehicleV2);
    output->gps_interval_v2_size = sizeof(MockNavBackendGpsIntervalV2);
    output->audio_v2_size = sizeof(MockNavBackendAudioV2);
    output->capabilities = MOCK_NAV_PLUGIN_REQUIRED_CAPABILITIES;
    std::memcpy(output->build_id, build_id, sizeof(build_id));
    output->backend.v1.abi_version = MOCK_NAV_BACKEND_ABI_V2;
    output->backend.v1.struct_size = sizeof(MockNavBackendV2);
    output->backend.v1.backend_context = &session();
    output->backend.v1.start = backend_start;
    output->backend.v1.stop = backend_stop;
    output->backend.v1.surface_created = backend_surface_created;
    output->backend.v1.surface_destroyed = backend_surface_destroyed;
    output->backend.v1.visibility_changed =
        accept_event<MockNavBackendVisibilityV1>;
    output->backend.v1.screen_changed = accept_event<MockNavBackendScreenV1>;
    output->backend.v1.gps_changed = backend_gps;
    output->backend.v1.pointer_changed = backend_pointer;
    output->backend.v1.render_frame = backend_render;
    output->backend.vehicle_changed = accept_event<MockNavBackendVehicleV2, MOCK_NAV_BACKEND_ABI_V2>;
    output->backend.gps_interval_changed =
        accept_event<MockNavBackendGpsIntervalV2, MOCK_NAV_BACKEND_ABI_V2>;
    output->backend.audio_changed = accept_event<MockNavBackendAudioV2, MOCK_NAV_BACKEND_ABI_V2>;
}

}  // namespace

extern "C" MOCK_NAV_PLUGIN_EXPORT int mock_nav_backend_query_v1(
    uint32_t requested_query_abi, uint32_t output_size,
    MockNavBackendPluginV1* output)
{
    if (requested_query_abi != MOCK_NAV_PLUGIN_QUERY_ABI_V1 ||
        output_size != sizeof(*output) || output == nullptr)
        return -1;
    try {
        fill_v1_query(output);
        return 0;
    } catch (...) {
        trace("error=exception_query");
        return -1;
    }
}
