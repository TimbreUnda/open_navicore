#include "mock_nav_backend.h"
#include "mock_nav_backend_plugin.h"

#include <stddef.h>
#include <stdint.h>

#if UINTPTR_MAX == 0xffffffffu
typedef char render_target_v1_must_be_40[
    sizeof(MockNavBackendRenderTargetV1) == 40u ? 1 : -1];
typedef char frame_v1_must_be_24[
    sizeof(MockNavBackendFrameV1) == 24u ? 1 : -1];
typedef char gps_v1_must_be_256[
    sizeof(MockNavBackendGpsV1) == 256u ? 1 : -1];
typedef char visibility_v1_must_be_20[
    sizeof(MockNavBackendVisibilityV1) == 20u ? 1 : -1];
typedef char screen_v1_must_be_20[
    sizeof(MockNavBackendScreenV1) == 20u ? 1 : -1];
typedef char pointer_v1_must_be_24[
    sizeof(MockNavBackendPointerV1) == 24u ? 1 : -1];
typedef char services_v1_must_be_16[
    sizeof(MockNavBackendServicesV1) == 16u ? 1 : -1];
typedef char backend_v1_must_be_48[
    sizeof(MockNavBackendV1) == 48u ? 1 : -1];
typedef char vehicle_v2_must_be_32[
    sizeof(MockNavBackendVehicleV2) == 32u ? 1 : -1];
typedef char gps_interval_v2_must_be_16[
    sizeof(MockNavBackendGpsIntervalV2) == 16u ? 1 : -1];
typedef char audio_v2_must_be_24[
    sizeof(MockNavBackendAudioV2) == 24u ? 1 : -1];
typedef char backend_v2_must_be_60[
    sizeof(MockNavBackendV2) == 60u ? 1 : -1];
typedef char backend_v2_prefix_must_start_at_zero[
    offsetof(MockNavBackendV2, v1) == 0u ? 1 : -1];
typedef char backend_v2_vehicle_callback_must_follow_v1[
    offsetof(MockNavBackendV2, vehicle_changed) == 48u ? 1 : -1];
typedef char backend_v2_gps_interval_callback_offset[
    offsetof(MockNavBackendV2, gps_interval_changed) == 52u ? 1 : -1];
typedef char backend_v2_audio_callback_offset[
    offsetof(MockNavBackendV2, audio_changed) == 56u ? 1 : -1];
typedef char plugin_v1_must_be_172[
    sizeof(MockNavBackendPluginV1) == 172u ? 1 : -1];
typedef char plugin_v1_backend_offset[
    offsetof(MockNavBackendPluginV1, backend) == 112u ? 1 : -1];
typedef char services_v2_must_be_28[
    sizeof(MockNavBackendServicesV2) == 28u ? 1 : -1];
typedef char services_v2_prefix_must_start_at_zero[
    offsetof(MockNavBackendServicesV2, v1) == 0u ? 1 : -1];
typedef char utf8_span_v1_must_be_8[
    sizeof(MockNavBackendUtf8SpanV1) == 8u ? 1 : -1];
typedef char cluster_v1_must_be_56[
    sizeof(MockNavBackendClusterV1) == 56u ? 1 : -1];
typedef char audio_availability_v1_must_be_12[
    sizeof(MockNavBackendAudioAvailabilityV1) == 12u ? 1 : -1];
typedef char thread_status_v1_must_be_24[
    sizeof(MockNavBackendThreadStatusV1) == 24u ? 1 : -1];
typedef char backend_v3_must_be_64[
    sizeof(MockNavBackendV3) == 64u ? 1 : -1];
typedef char backend_v3_prefix_must_start_at_zero[
    offsetof(MockNavBackendV3, v2) == 0u ? 1 : -1];
typedef char backend_v3_start_v2_offset[
    offsetof(MockNavBackendV3, start_v2) == 60u ? 1 : -1];
typedef char plugin_v2_must_be_264[
    sizeof(MockNavBackendPluginV2) == 264u ? 1 : -1];
typedef char plugin_v2_prefix_must_start_at_zero[
    offsetof(MockNavBackendPluginV2, v1) == 0u ? 1 : -1];
typedef char plugin_v2_backend_v3_offset[
    offsetof(MockNavBackendPluginV2, backend_v3) == 200u ? 1 : -1];
typedef char internationalization_v1_must_be_24[
    sizeof(MockNavBackendInternationalizationV1) == 24u ? 1 : -1];
typedef char cluster_display_v1_must_be_20[
    sizeof(MockNavBackendClusterDisplayV1) == 20u ? 1 : -1];
typedef char backend_v4_must_be_72[
    sizeof(MockNavBackendV4) == 72u ? 1 : -1];
typedef char backend_v4_prefix_must_start_at_zero[
    offsetof(MockNavBackendV4, v3) == 0u ? 1 : -1];
typedef char backend_v4_internationalization_offset[
    offsetof(MockNavBackendV4, internationalization_changed) == 64u ? 1 : -1];
typedef char backend_v4_cluster_display_offset[
    offsetof(MockNavBackendV4, cluster_display_changed) == 68u ? 1 : -1];
typedef char plugin_v3_must_be_352[
    sizeof(MockNavBackendPluginV3) == 352u ? 1 : -1];
typedef char plugin_v3_prefix_must_start_at_zero[
    offsetof(MockNavBackendPluginV3, v2) == 0u ? 1 : -1];
typedef char plugin_v3_backend_v4_offset[
    offsetof(MockNavBackendPluginV3, backend_v4) == 280u ? 1 : -1];
typedef char driver_distraction_v1_must_be_20[
    sizeof(MockNavBackendDriverDistractionV1) == 20u ? 1 : -1];
typedef char backend_v5_must_be_76[
    sizeof(MockNavBackendV5) == 76u ? 1 : -1];
typedef char backend_v5_prefix_must_start_at_zero[
    offsetof(MockNavBackendV5, v4) == 0u ? 1 : -1];
typedef char backend_v5_distraction_offset[
    offsetof(MockNavBackendV5, driver_distraction_changed) == 72u ? 1 : -1];
typedef char plugin_v4_must_be_440[
    sizeof(MockNavBackendPluginV4) == 440u ? 1 : -1];
typedef char plugin_v4_prefix_must_start_at_zero[
    offsetof(MockNavBackendPluginV4, v3) == 0u ? 1 : -1];
typedef char plugin_v4_backend_v5_offset[
    offsetof(MockNavBackendPluginV4, backend_v5) == 364u ? 1 : -1];
#endif

int mock_backend_abi_compile_probe(void)
{
    return MOCK_NAV_BACKEND_ABI_V1 == 1u &&
           MOCK_NAV_BACKEND_ABI_V2 == 2u &&
           MOCK_NAV_BACKEND_ABI_V3 == 3u &&
           MOCK_NAV_BACKEND_ABI_V4 == 4u &&
           MOCK_NAV_BACKEND_ABI_V5 == 5u &&
           MOCK_NAV_BACKEND_POINTER_MOVE == 3u ? 0 : 1;
}
