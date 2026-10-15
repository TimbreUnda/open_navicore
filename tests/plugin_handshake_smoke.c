#include "open_navi_backend_loader.h"

#include <assert.h>
#include <string.h>

static int callback(void *context, const void *event)
{
    (void)context;
    (void)event;
    return 0;
}

static int render_callback(void *context,
                           const OpenNaviBackendRenderTargetV1 *target,
                           const OpenNaviBackendFrameV1 *frame)
{
    (void)frame;
    return callback(context, target);
}

static int start_callback(void *context,
                          const OpenNaviBackendServicesV1 *services)
{
    return callback(context, services);
}

static int start_v2_callback(void *context,
                             const OpenNaviBackendServicesV2 *services)
{
    return callback(context, services);
}

static void void_callback(void *context)
{
    (void)context;
}

static void make_valid(OpenNaviBackendPluginV1 *plugin)
{
    static const uint8_t build_id[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
        OPEN_NAVI_PLUGIN_EXPECTED_BUILD_ID_INITIALIZER;
    memset(plugin, 0, sizeof(*plugin));
    plugin->magic0 = OPEN_NAVI_PLUGIN_MAGIC0;
    plugin->magic1 = OPEN_NAVI_PLUGIN_MAGIC1;
    plugin->query_abi_version = OPEN_NAVI_PLUGIN_QUERY_ABI_V1;
    plugin->struct_size = sizeof(*plugin);
    plugin->endian_tag = OPEN_NAVI_PLUGIN_ENDIAN_TAG;
    plugin->pointer_bytes = sizeof(void *);
    plugin->backend_abi_version = OPEN_NAVI_BACKEND_ABI_V2;
    plugin->backend_v1_size = sizeof(OpenNaviBackendV1);
    plugin->backend_v2_size = sizeof(OpenNaviBackendV2);
    plugin->render_target_v1_size = sizeof(OpenNaviBackendRenderTargetV1);
    plugin->frame_v1_size = sizeof(OpenNaviBackendFrameV1);
    plugin->gps_v1_size = sizeof(OpenNaviBackendGpsV1);
    plugin->visibility_v1_size = sizeof(OpenNaviBackendVisibilityV1);
    plugin->screen_v1_size = sizeof(OpenNaviBackendScreenV1);
    plugin->pointer_v1_size = sizeof(OpenNaviBackendPointerV1);
    plugin->services_v1_size = sizeof(OpenNaviBackendServicesV1);
    plugin->vehicle_v2_size = sizeof(OpenNaviBackendVehicleV2);
    plugin->gps_interval_v2_size = sizeof(OpenNaviBackendGpsIntervalV2);
    plugin->audio_v2_size = sizeof(OpenNaviBackendAudioV2);
    plugin->capabilities = OPEN_NAVI_PLUGIN_REQUIRED_CAPABILITIES;
    memcpy(plugin->build_id, build_id, sizeof(build_id));
    plugin->backend.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V2;
    plugin->backend.v1.struct_size = sizeof(OpenNaviBackendV2);
    plugin->backend.v1.start = start_callback;
    plugin->backend.v1.stop = void_callback;
    plugin->backend.v1.surface_created =
        (int (*)(void *, const OpenNaviBackendRenderTargetV1 *))callback;
    plugin->backend.v1.surface_destroyed = void_callback;
    plugin->backend.v1.visibility_changed =
        (int (*)(void *, const OpenNaviBackendVisibilityV1 *))callback;
    plugin->backend.v1.screen_changed =
        (int (*)(void *, const OpenNaviBackendScreenV1 *))callback;
    plugin->backend.v1.gps_changed =
        (int (*)(void *, const OpenNaviBackendGpsV1 *))callback;
    plugin->backend.v1.pointer_changed =
        (int (*)(void *, const OpenNaviBackendPointerV1 *))callback;
    plugin->backend.v1.render_frame = render_callback;
    plugin->backend.vehicle_changed =
        (int (*)(void *, const OpenNaviBackendVehicleV2 *))callback;
    plugin->backend.gps_interval_changed =
        (int (*)(void *, const OpenNaviBackendGpsIntervalV2 *))callback;
    plugin->backend.audio_changed =
        (int (*)(void *, const OpenNaviBackendAudioV2 *))callback;
}

static void make_valid_v2(OpenNaviBackendPluginV2 *plugin)
{
    static const uint8_t build_id[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
        OPEN_NAVI_PLUGIN_V2_EXPECTED_BUILD_ID_INITIALIZER;
    OpenNaviBackendPluginV1 legacy;
    make_valid(&legacy);
    memset(plugin, 0, sizeof(*plugin));
    plugin->v1 = legacy;
    plugin->v1.query_abi_version = OPEN_NAVI_PLUGIN_QUERY_ABI_V2;
    plugin->v1.struct_size = sizeof(*plugin);
    plugin->v1.backend_abi_version = OPEN_NAVI_BACKEND_ABI_V3;
    memcpy(plugin->v1.build_id, build_id, sizeof(build_id));
    plugin->services_v2_size = sizeof(OpenNaviBackendServicesV2);
    plugin->cluster_v1_size = sizeof(OpenNaviBackendClusterV1);
    plugin->utf8_span_v1_size = sizeof(OpenNaviBackendUtf8SpanV1);
    plugin->audio_availability_v1_size =
        sizeof(OpenNaviBackendAudioAvailabilityV1);
    plugin->thread_status_v1_size = sizeof(OpenNaviBackendThreadStatusV1);
    plugin->backend_v3_size = sizeof(OpenNaviBackendV3);
    plugin->capabilities_v2 = OPEN_NAVI_PLUGIN_V2_REQUIRED_CAPABILITIES;
    plugin->backend_v3.v2 = legacy.backend;
    plugin->backend_v3.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V3;
    plugin->backend_v3.v2.v1.struct_size = sizeof(OpenNaviBackendV3);
    plugin->backend_v3.start_v2 = start_v2_callback;
}

static void make_valid_v3(OpenNaviBackendPluginV3 *plugin)
{
    static const uint8_t build_id[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
        OPEN_NAVI_PLUGIN_V3_EXPECTED_BUILD_ID_INITIALIZER;
    OpenNaviBackendPluginV2 legacy;
    make_valid_v2(&legacy);
    memset(plugin, 0, sizeof(*plugin));
    plugin->v2 = legacy;
    plugin->v2.v1.query_abi_version = OPEN_NAVI_PLUGIN_QUERY_ABI_V3;
    plugin->v2.v1.struct_size = sizeof(*plugin);
    plugin->v2.v1.backend_abi_version = OPEN_NAVI_BACKEND_ABI_V4;
    memcpy(plugin->v2.v1.build_id, build_id, sizeof(build_id));
    plugin->internationalization_v1_size =
        sizeof(OpenNaviBackendInternationalizationV1);
    plugin->cluster_display_v1_size = sizeof(OpenNaviBackendClusterDisplayV1);
    plugin->backend_v4_size = sizeof(OpenNaviBackendV4);
    plugin->capabilities_v3 = OPEN_NAVI_PLUGIN_V3_REQUIRED_CAPABILITIES;
    plugin->backend_v4.v3 = legacy.backend_v3;
    plugin->backend_v4.v3.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V4;
    plugin->backend_v4.v3.v2.v1.struct_size = sizeof(OpenNaviBackendV4);
    plugin->backend_v4.internationalization_changed =
        (int (*)(void *, const OpenNaviBackendInternationalizationV1 *))callback;
    plugin->backend_v4.cluster_display_changed =
        (int (*)(void *, const OpenNaviBackendClusterDisplayV1 *))callback;
}

static void make_valid_v4(OpenNaviBackendPluginV4 *plugin)
{
    static const uint8_t build_id[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
        OPEN_NAVI_PLUGIN_V4_EXPECTED_BUILD_ID_INITIALIZER;
    OpenNaviBackendPluginV3 legacy;
    make_valid_v3(&legacy);
    memset(plugin, 0, sizeof(*plugin));
    plugin->v3 = legacy;
    plugin->v3.v2.v1.query_abi_version = OPEN_NAVI_PLUGIN_QUERY_ABI_V4;
    plugin->v3.v2.v1.struct_size = sizeof(*plugin);
    plugin->v3.v2.v1.backend_abi_version = OPEN_NAVI_BACKEND_ABI_V5;
    memcpy(plugin->v3.v2.v1.build_id, build_id, sizeof(build_id));
    plugin->driver_distraction_v1_size =
        sizeof(OpenNaviBackendDriverDistractionV1);
    plugin->backend_v5_size = sizeof(OpenNaviBackendV5);
    plugin->capabilities_v4 = OPEN_NAVI_PLUGIN_V4_REQUIRED_CAPABILITIES;
    plugin->backend_v5.v4 = legacy.backend_v4;
    plugin->backend_v5.v4.v3.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V5;
    plugin->backend_v5.v4.v3.v2.v1.struct_size = sizeof(OpenNaviBackendV5);
    plugin->backend_v5.driver_distraction_changed =
        (int (*)(void *, const OpenNaviBackendDriverDistractionV1 *))callback;
}

int main(void)
{
    OpenNaviBackendPluginV1 plugin;
    OpenNaviBackendPluginV2 plugin_v2;
    OpenNaviBackendPluginV3 plugin_v3;
    OpenNaviBackendPluginV4 plugin_v4;
    char diagnostic[128];

#define REJECT(statement)                                                     \
    do { make_valid(&plugin); statement;                                     \
         assert(open_navi_backend_plugin_validate(&plugin, diagnostic,            \
                                              sizeof(diagnostic)) != 0); }    \
    while (0)
    make_valid(&plugin);
    assert(open_navi_backend_plugin_validate(&plugin, diagnostic,
                                        sizeof(diagnostic)) == 0);
    /* Optional capabilities (pointer contacts) may be added. */
    plugin.capabilities |= OPEN_NAVI_PLUGIN_CAP_POINTER_CONTACTS;
    assert(open_navi_backend_plugin_validate(&plugin, diagnostic,
                                        sizeof(diagnostic)) == 0);
    REJECT(plugin.capabilities = OPEN_NAVI_PLUGIN_CAP_POINTER_CONTACTS);
    REJECT(plugin.magic0 ^= 1u);
    REJECT(++plugin.magic1);
    REJECT(++plugin.query_abi_version);
    REJECT(--plugin.struct_size);
    REJECT(++plugin.endian_tag);
    REJECT(++plugin.pointer_bytes);
    REJECT(--plugin.backend_abi_version);
    REJECT(--plugin.backend_v1_size);
    REJECT(--plugin.backend_v2_size);
    REJECT(--plugin.render_target_v1_size);
    REJECT(--plugin.frame_v1_size);
    REJECT(--plugin.gps_v1_size);
    REJECT(--plugin.visibility_v1_size);
    REJECT(--plugin.screen_v1_size);
    REJECT(--plugin.pointer_v1_size);
    REJECT(--plugin.services_v1_size);
    REJECT(--plugin.vehicle_v2_size);
    REJECT(--plugin.gps_interval_v2_size);
    REJECT(--plugin.audio_v2_size);
    REJECT(plugin.capabilities &= ~OPEN_NAVI_PLUGIN_CAP_RENDER);
    REJECT(plugin.capabilities |= UINT32_C(0x80000000));
    REJECT(plugin.build_id[0] ^= 1u);
    REJECT(plugin.backend.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V1);
    REJECT(--plugin.backend.v1.struct_size);
    REJECT(plugin.backend.v1.start = NULL);
    REJECT(plugin.backend.v1.stop = NULL);
    REJECT(plugin.backend.v1.surface_created = NULL);
    REJECT(plugin.backend.v1.surface_destroyed = NULL);
    REJECT(plugin.backend.v1.visibility_changed = NULL);
    REJECT(plugin.backend.v1.screen_changed = NULL);
    REJECT(plugin.backend.v1.gps_changed = NULL);
    REJECT(plugin.backend.v1.pointer_changed = NULL);
    REJECT(plugin.backend.v1.render_frame = NULL);
    REJECT(plugin.backend.vehicle_changed = NULL);
    REJECT(plugin.backend.gps_interval_changed = NULL);
    REJECT(plugin.backend.audio_changed = NULL);
#undef REJECT

#define REJECT_V2(statement)                                                  \
    do { make_valid_v2(&plugin_v2); statement;                              \
         assert(open_navi_backend_plugin_validate_v2(                             \
                    &plugin_v2, diagnostic, sizeof(diagnostic)) != 0); }     \
    while (0)
    make_valid_v2(&plugin_v2);
    assert(open_navi_backend_plugin_validate_v2(
               &plugin_v2, diagnostic, sizeof(diagnostic)) == 0);
    REJECT_V2(--plugin_v2.v1.struct_size);
    REJECT_V2(--plugin_v2.services_v2_size);
    REJECT_V2(--plugin_v2.cluster_v1_size);
    REJECT_V2(--plugin_v2.utf8_span_v1_size);
    REJECT_V2(--plugin_v2.audio_availability_v1_size);
    REJECT_V2(--plugin_v2.thread_status_v1_size);
    REJECT_V2(--plugin_v2.backend_v3_size);
    REJECT_V2(plugin_v2.capabilities_v2 &=
              ~OPEN_NAVI_PLUGIN_CAP_OUTBOUND_CLUSTER);
    REJECT_V2(plugin_v2.capabilities_v2 |= UINT32_C(0x80000000));
    REJECT_V2(plugin_v2.backend_v3.v2.v1.abi_version =
              OPEN_NAVI_BACKEND_ABI_V2);
    REJECT_V2(--plugin_v2.backend_v3.v2.v1.struct_size);
    REJECT_V2(plugin_v2.backend_v3.start_v2 = NULL);
#undef REJECT_V2

#define REJECT_V3(statement)                                                  \
    do { make_valid_v3(&plugin_v3); statement;                               \
         assert(open_navi_backend_plugin_validate_v3(                             \
                    &plugin_v3, diagnostic, sizeof(diagnostic)) != 0); }     \
    while (0)
    make_valid_v3(&plugin_v3);
    assert(open_navi_backend_plugin_validate_v3(
               &plugin_v3, diagnostic, sizeof(diagnostic)) == 0);
    REJECT_V3(--plugin_v3.v2.v1.struct_size);
    REJECT_V3(--plugin_v3.internationalization_v1_size);
    REJECT_V3(--plugin_v3.cluster_display_v1_size);
    REJECT_V3(--plugin_v3.backend_v4_size);
    REJECT_V3(plugin_v3.capabilities_v3 &=
              ~OPEN_NAVI_PLUGIN_CAP_INPUT_CLUSTER_DISPLAY);
    REJECT_V3(plugin_v3.capabilities_v3 |= UINT32_C(0x80000000));
    REJECT_V3(plugin_v3.backend_v4.v3.v2.v1.abi_version =
              OPEN_NAVI_BACKEND_ABI_V3);
    REJECT_V3(--plugin_v3.backend_v4.v3.v2.v1.struct_size);
    REJECT_V3(plugin_v3.backend_v4.internationalization_changed = NULL);
    REJECT_V3(plugin_v3.backend_v4.cluster_display_changed = NULL);
#undef REJECT_V3

#define REJECT_V4(statement)                                                  \
    do { make_valid_v4(&plugin_v4); statement;                               \
         assert(open_navi_backend_plugin_validate_v4(                             \
                    &plugin_v4, diagnostic, sizeof(diagnostic)) != 0); }     \
    while (0)
    make_valid_v4(&plugin_v4);
    assert(open_navi_backend_plugin_validate_v4(
               &plugin_v4, diagnostic, sizeof(diagnostic)) == 0);
    REJECT_V4(--plugin_v4.v3.v2.v1.struct_size);
    REJECT_V4(--plugin_v4.driver_distraction_v1_size);
    REJECT_V4(--plugin_v4.backend_v5_size);
    REJECT_V4(plugin_v4.capabilities_v4 &=
              ~OPEN_NAVI_PLUGIN_CAP_INPUT_DRIVER_DISTRACTION);
    REJECT_V4(plugin_v4.capabilities_v4 |= UINT32_C(0x80000000));
    REJECT_V4(plugin_v4.backend_v5.v4.v3.v2.v1.abi_version =
              OPEN_NAVI_BACKEND_ABI_V4);
    REJECT_V4(--plugin_v4.backend_v5.v4.v3.v2.v1.struct_size);
    REJECT_V4(plugin_v4.backend_v5.driver_distraction_changed = NULL);
#undef REJECT_V4
    return 0;
}
