#include "mock_backend_loader.h"

#include <assert.h>
#include <string.h>

static int callback(void *context, const void *event)
{
    (void)context;
    (void)event;
    return 0;
}

static int render_callback(void *context,
                           const MockNavBackendRenderTargetV1 *target,
                           const MockNavBackendFrameV1 *frame)
{
    (void)frame;
    return callback(context, target);
}

static int start_callback(void *context,
                          const MockNavBackendServicesV1 *services)
{
    return callback(context, services);
}

static int start_v2_callback(void *context,
                             const MockNavBackendServicesV2 *services)
{
    return callback(context, services);
}

static void void_callback(void *context)
{
    (void)context;
}

static void make_valid(MockNavBackendPluginV1 *plugin)
{
    static const uint8_t build_id[MOCK_NAV_PLUGIN_BUILD_ID_BYTES] =
        MOCK_NAV_PLUGIN_EXPECTED_BUILD_ID_INITIALIZER;
    memset(plugin, 0, sizeof(*plugin));
    plugin->magic0 = MOCK_NAV_PLUGIN_MAGIC0;
    plugin->magic1 = MOCK_NAV_PLUGIN_MAGIC1;
    plugin->query_abi_version = MOCK_NAV_PLUGIN_QUERY_ABI_V1;
    plugin->struct_size = sizeof(*plugin);
    plugin->endian_tag = MOCK_NAV_PLUGIN_ENDIAN_TAG;
    plugin->pointer_bytes = sizeof(void *);
    plugin->backend_abi_version = MOCK_NAV_BACKEND_ABI_V2;
    plugin->backend_v1_size = sizeof(MockNavBackendV1);
    plugin->backend_v2_size = sizeof(MockNavBackendV2);
    plugin->render_target_v1_size = sizeof(MockNavBackendRenderTargetV1);
    plugin->frame_v1_size = sizeof(MockNavBackendFrameV1);
    plugin->gps_v1_size = sizeof(MockNavBackendGpsV1);
    plugin->visibility_v1_size = sizeof(MockNavBackendVisibilityV1);
    plugin->screen_v1_size = sizeof(MockNavBackendScreenV1);
    plugin->pointer_v1_size = sizeof(MockNavBackendPointerV1);
    plugin->services_v1_size = sizeof(MockNavBackendServicesV1);
    plugin->vehicle_v2_size = sizeof(MockNavBackendVehicleV2);
    plugin->gps_interval_v2_size = sizeof(MockNavBackendGpsIntervalV2);
    plugin->audio_v2_size = sizeof(MockNavBackendAudioV2);
    plugin->capabilities = MOCK_NAV_PLUGIN_REQUIRED_CAPABILITIES;
    memcpy(plugin->build_id, build_id, sizeof(build_id));
    plugin->backend.v1.abi_version = MOCK_NAV_BACKEND_ABI_V2;
    plugin->backend.v1.struct_size = sizeof(MockNavBackendV2);
    plugin->backend.v1.start = start_callback;
    plugin->backend.v1.stop = void_callback;
    plugin->backend.v1.surface_created =
        (int (*)(void *, const MockNavBackendRenderTargetV1 *))callback;
    plugin->backend.v1.surface_destroyed = void_callback;
    plugin->backend.v1.visibility_changed =
        (int (*)(void *, const MockNavBackendVisibilityV1 *))callback;
    plugin->backend.v1.screen_changed =
        (int (*)(void *, const MockNavBackendScreenV1 *))callback;
    plugin->backend.v1.gps_changed =
        (int (*)(void *, const MockNavBackendGpsV1 *))callback;
    plugin->backend.v1.pointer_changed =
        (int (*)(void *, const MockNavBackendPointerV1 *))callback;
    plugin->backend.v1.render_frame = render_callback;
    plugin->backend.vehicle_changed =
        (int (*)(void *, const MockNavBackendVehicleV2 *))callback;
    plugin->backend.gps_interval_changed =
        (int (*)(void *, const MockNavBackendGpsIntervalV2 *))callback;
    plugin->backend.audio_changed =
        (int (*)(void *, const MockNavBackendAudioV2 *))callback;
}

static void make_valid_v2(MockNavBackendPluginV2 *plugin)
{
    static const uint8_t build_id[MOCK_NAV_PLUGIN_BUILD_ID_BYTES] =
        MOCK_NAV_PLUGIN_V2_EXPECTED_BUILD_ID_INITIALIZER;
    MockNavBackendPluginV1 legacy;
    make_valid(&legacy);
    memset(plugin, 0, sizeof(*plugin));
    plugin->v1 = legacy;
    plugin->v1.query_abi_version = MOCK_NAV_PLUGIN_QUERY_ABI_V2;
    plugin->v1.struct_size = sizeof(*plugin);
    plugin->v1.backend_abi_version = MOCK_NAV_BACKEND_ABI_V3;
    memcpy(plugin->v1.build_id, build_id, sizeof(build_id));
    plugin->services_v2_size = sizeof(MockNavBackendServicesV2);
    plugin->cluster_v1_size = sizeof(MockNavBackendClusterV1);
    plugin->utf8_span_v1_size = sizeof(MockNavBackendUtf8SpanV1);
    plugin->audio_availability_v1_size =
        sizeof(MockNavBackendAudioAvailabilityV1);
    plugin->thread_status_v1_size = sizeof(MockNavBackendThreadStatusV1);
    plugin->backend_v3_size = sizeof(MockNavBackendV3);
    plugin->capabilities_v2 = MOCK_NAV_PLUGIN_V2_REQUIRED_CAPABILITIES;
    plugin->backend_v3.v2 = legacy.backend;
    plugin->backend_v3.v2.v1.abi_version = MOCK_NAV_BACKEND_ABI_V3;
    plugin->backend_v3.v2.v1.struct_size = sizeof(MockNavBackendV3);
    plugin->backend_v3.start_v2 = start_v2_callback;
}

static void make_valid_v3(MockNavBackendPluginV3 *plugin)
{
    static const uint8_t build_id[MOCK_NAV_PLUGIN_BUILD_ID_BYTES] =
        MOCK_NAV_PLUGIN_V3_EXPECTED_BUILD_ID_INITIALIZER;
    MockNavBackendPluginV2 legacy;
    make_valid_v2(&legacy);
    memset(plugin, 0, sizeof(*plugin));
    plugin->v2 = legacy;
    plugin->v2.v1.query_abi_version = MOCK_NAV_PLUGIN_QUERY_ABI_V3;
    plugin->v2.v1.struct_size = sizeof(*plugin);
    plugin->v2.v1.backend_abi_version = MOCK_NAV_BACKEND_ABI_V4;
    memcpy(plugin->v2.v1.build_id, build_id, sizeof(build_id));
    plugin->internationalization_v1_size =
        sizeof(MockNavBackendInternationalizationV1);
    plugin->cluster_display_v1_size = sizeof(MockNavBackendClusterDisplayV1);
    plugin->backend_v4_size = sizeof(MockNavBackendV4);
    plugin->capabilities_v3 = MOCK_NAV_PLUGIN_V3_REQUIRED_CAPABILITIES;
    plugin->backend_v4.v3 = legacy.backend_v3;
    plugin->backend_v4.v3.v2.v1.abi_version = MOCK_NAV_BACKEND_ABI_V4;
    plugin->backend_v4.v3.v2.v1.struct_size = sizeof(MockNavBackendV4);
    plugin->backend_v4.internationalization_changed =
        (int (*)(void *, const MockNavBackendInternationalizationV1 *))callback;
    plugin->backend_v4.cluster_display_changed =
        (int (*)(void *, const MockNavBackendClusterDisplayV1 *))callback;
}

static void make_valid_v4(MockNavBackendPluginV4 *plugin)
{
    static const uint8_t build_id[MOCK_NAV_PLUGIN_BUILD_ID_BYTES] =
        MOCK_NAV_PLUGIN_V4_EXPECTED_BUILD_ID_INITIALIZER;
    MockNavBackendPluginV3 legacy;
    make_valid_v3(&legacy);
    memset(plugin, 0, sizeof(*plugin));
    plugin->v3 = legacy;
    plugin->v3.v2.v1.query_abi_version = MOCK_NAV_PLUGIN_QUERY_ABI_V4;
    plugin->v3.v2.v1.struct_size = sizeof(*plugin);
    plugin->v3.v2.v1.backend_abi_version = MOCK_NAV_BACKEND_ABI_V5;
    memcpy(plugin->v3.v2.v1.build_id, build_id, sizeof(build_id));
    plugin->driver_distraction_v1_size =
        sizeof(MockNavBackendDriverDistractionV1);
    plugin->backend_v5_size = sizeof(MockNavBackendV5);
    plugin->capabilities_v4 = MOCK_NAV_PLUGIN_V4_REQUIRED_CAPABILITIES;
    plugin->backend_v5.v4 = legacy.backend_v4;
    plugin->backend_v5.v4.v3.v2.v1.abi_version = MOCK_NAV_BACKEND_ABI_V5;
    plugin->backend_v5.v4.v3.v2.v1.struct_size = sizeof(MockNavBackendV5);
    plugin->backend_v5.driver_distraction_changed =
        (int (*)(void *, const MockNavBackendDriverDistractionV1 *))callback;
}

int main(void)
{
    MockNavBackendPluginV1 plugin;
    MockNavBackendPluginV2 plugin_v2;
    MockNavBackendPluginV3 plugin_v3;
    MockNavBackendPluginV4 plugin_v4;
    char diagnostic[128];

#define REJECT(statement)                                                     \
    do { make_valid(&plugin); statement;                                     \
         assert(mock_backend_plugin_validate(&plugin, diagnostic,            \
                                              sizeof(diagnostic)) != 0); }    \
    while (0)
    make_valid(&plugin);
    assert(mock_backend_plugin_validate(&plugin, diagnostic,
                                        sizeof(diagnostic)) == 0);
    /* Optional capabilities (pointer contacts) may be added. */
    plugin.capabilities |= MOCK_NAV_PLUGIN_CAP_POINTER_CONTACTS;
    assert(mock_backend_plugin_validate(&plugin, diagnostic,
                                        sizeof(diagnostic)) == 0);
    REJECT(plugin.capabilities = MOCK_NAV_PLUGIN_CAP_POINTER_CONTACTS);
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
    REJECT(plugin.capabilities &= ~MOCK_NAV_PLUGIN_CAP_RENDER);
    REJECT(plugin.capabilities |= UINT32_C(0x80000000));
    REJECT(plugin.build_id[0] ^= 1u);
    REJECT(plugin.backend.v1.abi_version = MOCK_NAV_BACKEND_ABI_V1);
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
         assert(mock_backend_plugin_validate_v2(                             \
                    &plugin_v2, diagnostic, sizeof(diagnostic)) != 0); }     \
    while (0)
    make_valid_v2(&plugin_v2);
    assert(mock_backend_plugin_validate_v2(
               &plugin_v2, diagnostic, sizeof(diagnostic)) == 0);
    REJECT_V2(--plugin_v2.v1.struct_size);
    REJECT_V2(--plugin_v2.services_v2_size);
    REJECT_V2(--plugin_v2.cluster_v1_size);
    REJECT_V2(--plugin_v2.utf8_span_v1_size);
    REJECT_V2(--plugin_v2.audio_availability_v1_size);
    REJECT_V2(--plugin_v2.thread_status_v1_size);
    REJECT_V2(--plugin_v2.backend_v3_size);
    REJECT_V2(plugin_v2.capabilities_v2 &=
              ~MOCK_NAV_PLUGIN_CAP_OUTBOUND_CLUSTER);
    REJECT_V2(plugin_v2.capabilities_v2 |= UINT32_C(0x80000000));
    REJECT_V2(plugin_v2.backend_v3.v2.v1.abi_version =
              MOCK_NAV_BACKEND_ABI_V2);
    REJECT_V2(--plugin_v2.backend_v3.v2.v1.struct_size);
    REJECT_V2(plugin_v2.backend_v3.start_v2 = NULL);
#undef REJECT_V2

#define REJECT_V3(statement)                                                  \
    do { make_valid_v3(&plugin_v3); statement;                               \
         assert(mock_backend_plugin_validate_v3(                             \
                    &plugin_v3, diagnostic, sizeof(diagnostic)) != 0); }     \
    while (0)
    make_valid_v3(&plugin_v3);
    assert(mock_backend_plugin_validate_v3(
               &plugin_v3, diagnostic, sizeof(diagnostic)) == 0);
    REJECT_V3(--plugin_v3.v2.v1.struct_size);
    REJECT_V3(--plugin_v3.internationalization_v1_size);
    REJECT_V3(--plugin_v3.cluster_display_v1_size);
    REJECT_V3(--plugin_v3.backend_v4_size);
    REJECT_V3(plugin_v3.capabilities_v3 &=
              ~MOCK_NAV_PLUGIN_CAP_INPUT_CLUSTER_DISPLAY);
    REJECT_V3(plugin_v3.capabilities_v3 |= UINT32_C(0x80000000));
    REJECT_V3(plugin_v3.backend_v4.v3.v2.v1.abi_version =
              MOCK_NAV_BACKEND_ABI_V3);
    REJECT_V3(--plugin_v3.backend_v4.v3.v2.v1.struct_size);
    REJECT_V3(plugin_v3.backend_v4.internationalization_changed = NULL);
    REJECT_V3(plugin_v3.backend_v4.cluster_display_changed = NULL);
#undef REJECT_V3

#define REJECT_V4(statement)                                                  \
    do { make_valid_v4(&plugin_v4); statement;                               \
         assert(mock_backend_plugin_validate_v4(                             \
                    &plugin_v4, diagnostic, sizeof(diagnostic)) != 0); }     \
    while (0)
    make_valid_v4(&plugin_v4);
    assert(mock_backend_plugin_validate_v4(
               &plugin_v4, diagnostic, sizeof(diagnostic)) == 0);
    REJECT_V4(--plugin_v4.v3.v2.v1.struct_size);
    REJECT_V4(--plugin_v4.driver_distraction_v1_size);
    REJECT_V4(--plugin_v4.backend_v5_size);
    REJECT_V4(plugin_v4.capabilities_v4 &=
              ~MOCK_NAV_PLUGIN_CAP_INPUT_DRIVER_DISTRACTION);
    REJECT_V4(plugin_v4.capabilities_v4 |= UINT32_C(0x80000000));
    REJECT_V4(plugin_v4.backend_v5.v4.v3.v2.v1.abi_version =
              MOCK_NAV_BACKEND_ABI_V4);
    REJECT_V4(--plugin_v4.backend_v5.v4.v3.v2.v1.struct_size);
    REJECT_V4(plugin_v4.backend_v5.driver_distraction_changed = NULL);
#undef REJECT_V4
    return 0;
}
