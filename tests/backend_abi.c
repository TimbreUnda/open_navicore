#include <open_navicore/backend.h>
#include <open_navicore/backend_plugin.h>

#include <stddef.h>
#include <stdint.h>

#if UINTPTR_MAX == 0xffffffffu
typedef char render_target_v1_must_be_40[
    sizeof(OpenNaviBackendRenderTargetV1) == 40u ? 1 : -1];
typedef char frame_v1_must_be_24[
    sizeof(OpenNaviBackendFrameV1) == 24u ? 1 : -1];
typedef char gps_v1_must_be_256[
    sizeof(OpenNaviBackendGpsV1) == 256u ? 1 : -1];
typedef char visibility_v1_must_be_20[
    sizeof(OpenNaviBackendVisibilityV1) == 20u ? 1 : -1];
typedef char screen_v1_must_be_20[
    sizeof(OpenNaviBackendScreenV1) == 20u ? 1 : -1];
typedef char pointer_v1_must_be_24[
    sizeof(OpenNaviBackendPointerV1) == 24u ? 1 : -1];
typedef char services_v1_must_be_16[
    sizeof(OpenNaviBackendServicesV1) == 16u ? 1 : -1];
typedef char backend_v1_must_be_48[
    sizeof(OpenNaviBackendV1) == 48u ? 1 : -1];
typedef char vehicle_v2_must_be_36[
    sizeof(OpenNaviBackendVehicleV2) == 36u ? 1 : -1];
typedef char vehicle_v2_original_prefix_must_be_32[
    offsetof(OpenNaviBackendVehicleV2, displayed_vehicle_speed_unit) == 32u ? 1 : -1];
typedef char gps_interval_v2_must_be_16[
    sizeof(OpenNaviBackendGpsIntervalV2) == 16u ? 1 : -1];
typedef char audio_v2_must_be_24[
    sizeof(OpenNaviBackendAudioV2) == 24u ? 1 : -1];
typedef char backend_v2_must_be_60[
    sizeof(OpenNaviBackendV2) == 60u ? 1 : -1];
typedef char backend_v2_prefix_must_start_at_zero[
    offsetof(OpenNaviBackendV2, v1) == 0u ? 1 : -1];
typedef char backend_v2_vehicle_callback_must_follow_v1[
    offsetof(OpenNaviBackendV2, vehicle_changed) == 48u ? 1 : -1];
typedef char backend_v2_gps_interval_callback_offset[
    offsetof(OpenNaviBackendV2, gps_interval_changed) == 52u ? 1 : -1];
typedef char backend_v2_audio_callback_offset[
    offsetof(OpenNaviBackendV2, audio_changed) == 56u ? 1 : -1];
typedef char plugin_v1_must_be_172[
    sizeof(OpenNaviBackendPluginV1) == 172u ? 1 : -1];
typedef char plugin_v1_backend_offset[
    offsetof(OpenNaviBackendPluginV1, backend) == 112u ? 1 : -1];
typedef char services_v2_must_be_28[
    sizeof(OpenNaviBackendServicesV2) == 28u ? 1 : -1];
typedef char services_v2_prefix_must_start_at_zero[
    offsetof(OpenNaviBackendServicesV2, v1) == 0u ? 1 : -1];
typedef char utf8_span_v1_must_be_8[
    sizeof(OpenNaviBackendUtf8SpanV1) == 8u ? 1 : -1];
typedef char cluster_v1_must_be_56[
    sizeof(OpenNaviBackendClusterV1) == 56u ? 1 : -1];
typedef char audio_availability_v1_must_be_12[
    sizeof(OpenNaviBackendAudioAvailabilityV1) == 12u ? 1 : -1];
typedef char thread_status_v1_must_be_24[
    sizeof(OpenNaviBackendThreadStatusV1) == 24u ? 1 : -1];
typedef char backend_v3_must_be_64[
    sizeof(OpenNaviBackendV3) == 64u ? 1 : -1];
typedef char backend_v3_prefix_must_start_at_zero[
    offsetof(OpenNaviBackendV3, v2) == 0u ? 1 : -1];
typedef char backend_v3_start_v2_offset[
    offsetof(OpenNaviBackendV3, start_v2) == 60u ? 1 : -1];
typedef char plugin_v2_must_be_264[
    sizeof(OpenNaviBackendPluginV2) == 264u ? 1 : -1];
typedef char plugin_v2_prefix_must_start_at_zero[
    offsetof(OpenNaviBackendPluginV2, v1) == 0u ? 1 : -1];
typedef char plugin_v2_backend_v3_offset[
    offsetof(OpenNaviBackendPluginV2, backend_v3) == 200u ? 1 : -1];
typedef char internationalization_v1_must_be_24[
    sizeof(OpenNaviBackendInternationalizationV1) == 24u ? 1 : -1];
typedef char cluster_display_v1_must_be_20[
    sizeof(OpenNaviBackendClusterDisplayV1) == 20u ? 1 : -1];
typedef char backend_v4_must_be_72[
    sizeof(OpenNaviBackendV4) == 72u ? 1 : -1];
typedef char backend_v4_prefix_must_start_at_zero[
    offsetof(OpenNaviBackendV4, v3) == 0u ? 1 : -1];
typedef char backend_v4_internationalization_offset[
    offsetof(OpenNaviBackendV4, internationalization_changed) == 64u ? 1 : -1];
typedef char backend_v4_cluster_display_offset[
    offsetof(OpenNaviBackendV4, cluster_display_changed) == 68u ? 1 : -1];
typedef char plugin_v3_must_be_352[
    sizeof(OpenNaviBackendPluginV3) == 352u ? 1 : -1];
typedef char plugin_v3_prefix_must_start_at_zero[
    offsetof(OpenNaviBackendPluginV3, v2) == 0u ? 1 : -1];
typedef char plugin_v3_backend_v4_offset[
    offsetof(OpenNaviBackendPluginV3, backend_v4) == 280u ? 1 : -1];
typedef char driver_distraction_v1_must_be_20[
    sizeof(OpenNaviBackendDriverDistractionV1) == 20u ? 1 : -1];
typedef char backend_v5_must_be_76[
    sizeof(OpenNaviBackendV5) == 76u ? 1 : -1];
typedef char backend_v5_prefix_must_start_at_zero[
    offsetof(OpenNaviBackendV5, v4) == 0u ? 1 : -1];
typedef char backend_v5_distraction_offset[
    offsetof(OpenNaviBackendV5, driver_distraction_changed) == 72u ? 1 : -1];
typedef char plugin_v4_must_be_440[
    sizeof(OpenNaviBackendPluginV4) == 440u ? 1 : -1];
typedef char plugin_v4_prefix_must_start_at_zero[
    offsetof(OpenNaviBackendPluginV4, v3) == 0u ? 1 : -1];
typedef char plugin_v4_backend_v5_offset[
    offsetof(OpenNaviBackendPluginV4, backend_v5) == 364u ? 1 : -1];
typedef char guidance_state_v1_must_be_12[
    sizeof(OpenNaviBackendGuidanceStateV1) == 12u ? 1 : -1];
typedef char guidance_stop_v1_must_be_16[
    sizeof(OpenNaviBackendGuidanceStopV1) == 16u ? 1 : -1];
typedef char services_v3_must_be_32[
    sizeof(OpenNaviBackendServicesV3) == 32u ? 1 : -1];
typedef char services_v3_guidance_offset[
    offsetof(OpenNaviBackendServicesV3, publish_guidance_state) == 28u ? 1 : -1];
typedef char backend_v6_must_be_84[
    sizeof(OpenNaviBackendV6) == 84u ? 1 : -1];
typedef char backend_v6_prefix_must_start_at_zero[
    offsetof(OpenNaviBackendV6, v5) == 0u ? 1 : -1];
typedef char backend_v6_start_v3_offset[
    offsetof(OpenNaviBackendV6, start_v3) == 76u ? 1 : -1];
typedef char backend_v6_guidance_stop_offset[
    offsetof(OpenNaviBackendV6, guidance_stop_requested) == 80u ? 1 : -1];
typedef char plugin_v5_must_be_544[
    sizeof(OpenNaviBackendPluginV5) == 544u ? 1 : -1];
typedef char plugin_v5_prefix_must_start_at_zero[
    offsetof(OpenNaviBackendPluginV5, v4) == 0u ? 1 : -1];
typedef char plugin_v5_backend_v6_offset[
    offsetof(OpenNaviBackendPluginV5, backend_v6) == 460u ? 1 : -1];
#endif

int open_navi_backend_abi_compile_probe(void)
{
    return OPEN_NAVI_BACKEND_ABI_V1 == 1u &&
           OPEN_NAVI_BACKEND_ABI_V2 == 2u &&
           OPEN_NAVI_BACKEND_ABI_V3 == 3u &&
           OPEN_NAVI_BACKEND_ABI_V4 == 4u &&
           OPEN_NAVI_BACKEND_ABI_V5 == 5u &&
           OPEN_NAVI_BACKEND_ABI_V6 == 6u &&
           OPEN_NAVI_BACKEND_POINTER_MOVE == 3u ? 0 : 1;
}
