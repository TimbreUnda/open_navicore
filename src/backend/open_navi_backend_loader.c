#define _POSIX_C_SOURCE 200112L

#include "open_navi_backend_loader.h"

#include "open_navi_default_backend.h"
#include "open_navi_renderer.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef OPEN_NAVI_BACKEND_PLUGIN_PATH
#define OPEN_NAVI_BACKEND_PLUGIN_PATH \
    "/open-navicore/libOpenNaviBackendOsmAnd.so"
#endif

typedef enum {
    WRAPPER_PLUGIN = 1,
    WRAPPER_DEFAULT = 2
} WrapperSelection;

typedef struct {
    pthread_mutex_t lock;
    pthread_once_t once;
    void *pinned_handle;
    OpenNaviBackendV2 plugin;
    OpenNaviBackendV3 plugin_v3;
    OpenNaviBackendV4 plugin_v4;
    OpenNaviBackendV5 plugin_v5;
    OpenNaviBackendV6 plugin_v6;
    OpenNaviBackendV2 wrapper;
    OpenNaviBackendV3 wrapper_v3;
    OpenNaviBackendV4 wrapper_v4;
    OpenNaviBackendV5 wrapper_v5;
    OpenNaviBackendV6 wrapper_v6;
    OpenNaviBackendServicesV2 upstream_services;
    uint32_t (*upstream_publish_guidance_state)(
        void *runtime_context, const OpenNaviBackendGuidanceStateV1 *guidance);
    uintptr_t next_plugin_service_token;
    uintptr_t active_plugin_service_token;
    uint32_t query_abi;
    WrapperSelection selection;
    int plugin_started;
    int plugin_surfaced;
    int default_started;
    int default_surfaced;
    OpenNaviBackendLoaderTestState test;
} LoaderRuntime;

static LoaderRuntime g_loader = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .once = PTHREAD_ONCE_INIT
};

static const uint8_t g_expected_build_id[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
    OPEN_NAVI_PLUGIN_EXPECTED_BUILD_ID_INITIALIZER;
static const uint8_t g_v2_expected_build_id[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
    OPEN_NAVI_PLUGIN_V2_EXPECTED_BUILD_ID_INITIALIZER;
static const uint8_t g_v3_expected_build_id[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
    OPEN_NAVI_PLUGIN_V3_EXPECTED_BUILD_ID_INITIALIZER;
static const uint8_t g_v4_expected_build_id[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
    OPEN_NAVI_PLUGIN_V4_EXPECTED_BUILD_ID_INITIALIZER;
static const uint8_t g_v5_expected_build_id[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
    OPEN_NAVI_PLUGIN_V5_EXPECTED_BUILD_ID_INITIALIZER;

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

static void set_fallback(enum OpenNaviBackendFallbackReason reason)
{
    (void)pthread_mutex_lock(&g_loader.lock);
    if (g_loader.test.fallback_reason == OPEN_NAVI_BACKEND_FALLBACK_NONE)
        g_loader.test.fallback_reason = (uint32_t)reason;
    (void)pthread_mutex_unlock(&g_loader.lock);
}

static void increment_counter(uint32_t *counter)
{
    (void)pthread_mutex_lock(&g_loader.lock);
    if (*counter != UINT32_MAX) ++*counter;
    (void)pthread_mutex_unlock(&g_loader.lock);
}

static void set_flag(uint32_t *flag)
{
    (void)pthread_mutex_lock(&g_loader.lock);
    *flag = 1u;
    (void)pthread_mutex_unlock(&g_loader.lock);
}

int open_navi_backend_plugin_validate(const OpenNaviBackendPluginV1 *plugin,
                                 char *diagnostic,
                                 size_t diagnostic_capacity)
{
#define REQUIRE_EQUAL(field, expected)                                        \
    do {                                                                      \
        if (plugin->field != (uint32_t)(expected)) {                          \
            set_diagnostic(diagnostic, diagnostic_capacity,                   \
                           "plugin %s mismatch got=%u expected=%u",          \
                           #field, (unsigned int)plugin->field,                \
                           (unsigned int)(expected));                          \
            return -1;                                                        \
        }                                                                     \
    } while (0)
    const OpenNaviBackendV1 *backend;
    if (plugin == NULL) {
        set_diagnostic(diagnostic, diagnostic_capacity, "null plugin query");
        return -1;
    }
    REQUIRE_EQUAL(magic0, OPEN_NAVI_PLUGIN_MAGIC0);
    REQUIRE_EQUAL(magic1, OPEN_NAVI_PLUGIN_MAGIC1);
    REQUIRE_EQUAL(query_abi_version, OPEN_NAVI_PLUGIN_QUERY_ABI_V1);
    REQUIRE_EQUAL(struct_size, sizeof(*plugin));
    REQUIRE_EQUAL(endian_tag, OPEN_NAVI_PLUGIN_ENDIAN_TAG);
    REQUIRE_EQUAL(pointer_bytes, sizeof(void *));
    REQUIRE_EQUAL(backend_abi_version, OPEN_NAVI_BACKEND_ABI_V2);
    REQUIRE_EQUAL(backend_v1_size, sizeof(OpenNaviBackendV1));
    REQUIRE_EQUAL(backend_v2_size, sizeof(OpenNaviBackendV2));
    REQUIRE_EQUAL(render_target_v1_size,
                  sizeof(OpenNaviBackendRenderTargetV1));
    REQUIRE_EQUAL(frame_v1_size, sizeof(OpenNaviBackendFrameV1));
    REQUIRE_EQUAL(gps_v1_size, sizeof(OpenNaviBackendGpsV1));
    REQUIRE_EQUAL(visibility_v1_size, sizeof(OpenNaviBackendVisibilityV1));
    REQUIRE_EQUAL(screen_v1_size, sizeof(OpenNaviBackendScreenV1));
    REQUIRE_EQUAL(pointer_v1_size, sizeof(OpenNaviBackendPointerV1));
    REQUIRE_EQUAL(services_v1_size, sizeof(OpenNaviBackendServicesV1));
    REQUIRE_EQUAL(vehicle_v2_size, sizeof(OpenNaviBackendVehicleV2));
    REQUIRE_EQUAL(gps_interval_v2_size,
                  sizeof(OpenNaviBackendGpsIntervalV2));
    REQUIRE_EQUAL(audio_v2_size, sizeof(OpenNaviBackendAudioV2));
    /* Optional capabilities (pointer contacts, window area) may be added to the exact
     * required set in every query version. */
    if ((plugin->capabilities & ~(uint32_t)OPEN_NAVI_PLUGIN_OPTIONAL_CAPABILITIES) !=
        (uint32_t)OPEN_NAVI_PLUGIN_REQUIRED_CAPABILITIES) {
        set_diagnostic(diagnostic, diagnostic_capacity,
                       "plugin capabilities mismatch got=%u expected=%u",
                       (unsigned int)plugin->capabilities,
                       (unsigned int)OPEN_NAVI_PLUGIN_REQUIRED_CAPABILITIES);
        return -1;
    }
    if (memcmp(plugin->build_id, g_expected_build_id,
               sizeof(g_expected_build_id)) != 0) {
        set_diagnostic(diagnostic, diagnostic_capacity,
                       "plugin build id mismatch");
        return -1;
    }
    backend = &plugin->backend.v1;
    if (backend->abi_version != OPEN_NAVI_BACKEND_ABI_V2 ||
        backend->struct_size != sizeof(OpenNaviBackendV2) ||
        backend->start == NULL || backend->stop == NULL ||
        backend->surface_created == NULL ||
        backend->surface_destroyed == NULL ||
        backend->visibility_changed == NULL ||
        backend->screen_changed == NULL || backend->gps_changed == NULL ||
        backend->pointer_changed == NULL || backend->render_frame == NULL ||
        plugin->backend.vehicle_changed == NULL ||
        plugin->backend.gps_interval_changed == NULL ||
        plugin->backend.audio_changed == NULL) {
        set_diagnostic(diagnostic, diagnostic_capacity,
                       "plugin descriptor missing required callback");
        return -1;
    }
    set_diagnostic(diagnostic, diagnostic_capacity, "plugin handshake valid");
    return 0;
#undef REQUIRE_EQUAL
}

int open_navi_backend_plugin_validate_v2(const OpenNaviBackendPluginV2 *plugin,
                                    char *diagnostic,
                                    size_t diagnostic_capacity)
{
#define REQUIRE_V2(field, expected)                                           \
    do {                                                                      \
        if ((field) != (uint32_t)(expected)) {                                \
            set_diagnostic(diagnostic, diagnostic_capacity,                   \
                           "plugin v2 %s mismatch got=%u expected=%u",       \
                           #field, (unsigned int)(field),                      \
                           (unsigned int)(expected));                          \
            return -1;                                                        \
        }                                                                     \
    } while (0)
    const OpenNaviBackendV1 *backend;
    if (plugin == NULL) {
        set_diagnostic(diagnostic, diagnostic_capacity,
                       "null plugin v2 query");
        return -1;
    }
    REQUIRE_V2(plugin->v1.magic0, OPEN_NAVI_PLUGIN_MAGIC0);
    REQUIRE_V2(plugin->v1.magic1, OPEN_NAVI_PLUGIN_MAGIC1);
    REQUIRE_V2(plugin->v1.query_abi_version, OPEN_NAVI_PLUGIN_QUERY_ABI_V2);
    REQUIRE_V2(plugin->v1.struct_size, sizeof(*plugin));
    REQUIRE_V2(plugin->v1.endian_tag, OPEN_NAVI_PLUGIN_ENDIAN_TAG);
    REQUIRE_V2(plugin->v1.pointer_bytes, sizeof(void *));
    REQUIRE_V2(plugin->v1.backend_abi_version, OPEN_NAVI_BACKEND_ABI_V3);
    REQUIRE_V2(plugin->v1.backend_v1_size, sizeof(OpenNaviBackendV1));
    REQUIRE_V2(plugin->v1.backend_v2_size, sizeof(OpenNaviBackendV2));
    REQUIRE_V2(plugin->v1.render_target_v1_size,
               sizeof(OpenNaviBackendRenderTargetV1));
    REQUIRE_V2(plugin->v1.frame_v1_size, sizeof(OpenNaviBackendFrameV1));
    REQUIRE_V2(plugin->v1.gps_v1_size, sizeof(OpenNaviBackendGpsV1));
    REQUIRE_V2(plugin->v1.visibility_v1_size,
               sizeof(OpenNaviBackendVisibilityV1));
    REQUIRE_V2(plugin->v1.screen_v1_size, sizeof(OpenNaviBackendScreenV1));
    REQUIRE_V2(plugin->v1.pointer_v1_size, sizeof(OpenNaviBackendPointerV1));
    REQUIRE_V2(plugin->v1.services_v1_size,
               sizeof(OpenNaviBackendServicesV1));
    REQUIRE_V2(plugin->v1.vehicle_v2_size, sizeof(OpenNaviBackendVehicleV2));
    REQUIRE_V2(plugin->v1.gps_interval_v2_size,
               sizeof(OpenNaviBackendGpsIntervalV2));
    REQUIRE_V2(plugin->v1.audio_v2_size, sizeof(OpenNaviBackendAudioV2));
    REQUIRE_V2(plugin->v1.capabilities & ~(uint32_t)OPEN_NAVI_PLUGIN_OPTIONAL_CAPABILITIES,
               OPEN_NAVI_PLUGIN_REQUIRED_CAPABILITIES);
    REQUIRE_V2(plugin->services_v2_size, sizeof(OpenNaviBackendServicesV2));
    REQUIRE_V2(plugin->cluster_v1_size, sizeof(OpenNaviBackendClusterV1));
    REQUIRE_V2(plugin->utf8_span_v1_size, sizeof(OpenNaviBackendUtf8SpanV1));
    REQUIRE_V2(plugin->audio_availability_v1_size,
               sizeof(OpenNaviBackendAudioAvailabilityV1));
    REQUIRE_V2(plugin->thread_status_v1_size,
               sizeof(OpenNaviBackendThreadStatusV1));
    REQUIRE_V2(plugin->backend_v3_size, sizeof(OpenNaviBackendV3));
    REQUIRE_V2(plugin->capabilities_v2,
               OPEN_NAVI_PLUGIN_V2_REQUIRED_CAPABILITIES);
    if (memcmp(plugin->v1.build_id, g_v2_expected_build_id,
               sizeof(g_v2_expected_build_id)) != 0) {
        set_diagnostic(diagnostic, diagnostic_capacity,
                       "plugin v2 build id mismatch");
        return -1;
    }
    backend = &plugin->backend_v3.v2.v1;
    if (backend->abi_version != OPEN_NAVI_BACKEND_ABI_V3 ||
        backend->struct_size != sizeof(OpenNaviBackendV3) ||
        backend->stop == NULL || backend->surface_created == NULL ||
        backend->surface_destroyed == NULL ||
        backend->visibility_changed == NULL ||
        backend->screen_changed == NULL || backend->gps_changed == NULL ||
        backend->pointer_changed == NULL || backend->render_frame == NULL ||
        plugin->backend_v3.v2.vehicle_changed == NULL ||
        plugin->backend_v3.v2.gps_interval_changed == NULL ||
        plugin->backend_v3.v2.audio_changed == NULL ||
        plugin->backend_v3.start_v2 == NULL) {
        set_diagnostic(diagnostic, diagnostic_capacity,
                       "plugin v2 descriptor missing required callback");
        return -1;
    }
    set_diagnostic(diagnostic, diagnostic_capacity,
                   "plugin v2 handshake valid");
    return 0;
#undef REQUIRE_V2
}

int open_navi_backend_plugin_validate_v3(const OpenNaviBackendPluginV3 *plugin,
                                    char *diagnostic,
                                    size_t diagnostic_capacity)
{
#define REQUIRE_V3(field, expected)                                           \
    do {                                                                      \
        if ((field) != (uint32_t)(expected)) {                                \
            set_diagnostic(diagnostic, diagnostic_capacity,                   \
                           "plugin v3 %s mismatch got=%u expected=%u",       \
                           #field, (unsigned int)(field),                      \
                           (unsigned int)(expected));                          \
            return -1;                                                        \
        }                                                                     \
    } while (0)
    const OpenNaviBackendV1 *backend;
    if (plugin == NULL) return -1;
    REQUIRE_V3(plugin->v2.v1.magic0, OPEN_NAVI_PLUGIN_MAGIC0);
    REQUIRE_V3(plugin->v2.v1.magic1, OPEN_NAVI_PLUGIN_MAGIC1);
    REQUIRE_V3(plugin->v2.v1.query_abi_version,
               OPEN_NAVI_PLUGIN_QUERY_ABI_V3);
    REQUIRE_V3(plugin->v2.v1.struct_size, sizeof(*plugin));
    REQUIRE_V3(plugin->v2.v1.endian_tag, OPEN_NAVI_PLUGIN_ENDIAN_TAG);
    REQUIRE_V3(plugin->v2.v1.pointer_bytes, sizeof(void *));
    REQUIRE_V3(plugin->v2.v1.backend_abi_version, OPEN_NAVI_BACKEND_ABI_V4);
    REQUIRE_V3(plugin->v2.v1.backend_v1_size, sizeof(OpenNaviBackendV1));
    REQUIRE_V3(plugin->v2.v1.backend_v2_size, sizeof(OpenNaviBackendV2));
    REQUIRE_V3(plugin->v2.v1.render_target_v1_size,
               sizeof(OpenNaviBackendRenderTargetV1));
    REQUIRE_V3(plugin->v2.v1.frame_v1_size, sizeof(OpenNaviBackendFrameV1));
    REQUIRE_V3(plugin->v2.v1.gps_v1_size, sizeof(OpenNaviBackendGpsV1));
    REQUIRE_V3(plugin->v2.v1.visibility_v1_size,
               sizeof(OpenNaviBackendVisibilityV1));
    REQUIRE_V3(plugin->v2.v1.screen_v1_size,
               sizeof(OpenNaviBackendScreenV1));
    REQUIRE_V3(plugin->v2.v1.pointer_v1_size,
               sizeof(OpenNaviBackendPointerV1));
    REQUIRE_V3(plugin->v2.v1.services_v1_size,
               sizeof(OpenNaviBackendServicesV1));
    REQUIRE_V3(plugin->v2.v1.vehicle_v2_size,
               sizeof(OpenNaviBackendVehicleV2));
    REQUIRE_V3(plugin->v2.v1.gps_interval_v2_size,
               sizeof(OpenNaviBackendGpsIntervalV2));
    REQUIRE_V3(plugin->v2.v1.audio_v2_size, sizeof(OpenNaviBackendAudioV2));
    REQUIRE_V3(plugin->v2.v1.capabilities & ~(uint32_t)OPEN_NAVI_PLUGIN_OPTIONAL_CAPABILITIES,
               OPEN_NAVI_PLUGIN_REQUIRED_CAPABILITIES);
    REQUIRE_V3(plugin->v2.services_v2_size,
               sizeof(OpenNaviBackendServicesV2));
    REQUIRE_V3(plugin->v2.cluster_v1_size, sizeof(OpenNaviBackendClusterV1));
    REQUIRE_V3(plugin->v2.utf8_span_v1_size,
               sizeof(OpenNaviBackendUtf8SpanV1));
    REQUIRE_V3(plugin->v2.audio_availability_v1_size,
               sizeof(OpenNaviBackendAudioAvailabilityV1));
    REQUIRE_V3(plugin->v2.thread_status_v1_size,
               sizeof(OpenNaviBackendThreadStatusV1));
    REQUIRE_V3(plugin->v2.backend_v3_size, sizeof(OpenNaviBackendV3));
    REQUIRE_V3(plugin->v2.capabilities_v2,
               OPEN_NAVI_PLUGIN_V2_REQUIRED_CAPABILITIES);
    REQUIRE_V3(plugin->internationalization_v1_size,
               sizeof(OpenNaviBackendInternationalizationV1));
    REQUIRE_V3(plugin->cluster_display_v1_size,
               sizeof(OpenNaviBackendClusterDisplayV1));
    REQUIRE_V3(plugin->backend_v4_size, sizeof(OpenNaviBackendV4));
    REQUIRE_V3(plugin->capabilities_v3,
               OPEN_NAVI_PLUGIN_V3_REQUIRED_CAPABILITIES);
    if (memcmp(plugin->v2.v1.build_id, g_v3_expected_build_id,
               sizeof(g_v3_expected_build_id)) != 0) return -1;
    backend = &plugin->backend_v4.v3.v2.v1;
    if (backend->abi_version != OPEN_NAVI_BACKEND_ABI_V4 ||
        backend->struct_size != sizeof(OpenNaviBackendV4) ||
        backend->stop == NULL || backend->surface_created == NULL ||
        backend->surface_destroyed == NULL ||
        backend->visibility_changed == NULL ||
        backend->screen_changed == NULL || backend->gps_changed == NULL ||
        backend->pointer_changed == NULL || backend->render_frame == NULL ||
        plugin->backend_v4.v3.v2.vehicle_changed == NULL ||
        plugin->backend_v4.v3.v2.gps_interval_changed == NULL ||
        plugin->backend_v4.v3.v2.audio_changed == NULL ||
        plugin->backend_v4.v3.start_v2 == NULL ||
        plugin->backend_v4.internationalization_changed == NULL ||
        plugin->backend_v4.cluster_display_changed == NULL) return -1;
    set_diagnostic(diagnostic, diagnostic_capacity,
                   "plugin v3 handshake valid");
    return 0;
#undef REQUIRE_V3
}

int open_navi_backend_plugin_validate_v4(const OpenNaviBackendPluginV4 *plugin,
                                    char *diagnostic,
                                    size_t diagnostic_capacity)
{
#define REQUIRE_V4(field, expected)                                           \
    do {                                                                      \
        if ((field) != (uint32_t)(expected)) {                                \
            set_diagnostic(diagnostic, diagnostic_capacity,                   \
                           "plugin v4 %s mismatch got=%u expected=%u",       \
                           #field, (unsigned int)(field),                      \
                           (unsigned int)(expected));                          \
            return -1;                                                        \
        }                                                                     \
    } while (0)
    const OpenNaviBackendV1 *backend;
    if (plugin == NULL) return -1;
    REQUIRE_V4(plugin->v3.v2.v1.magic0, OPEN_NAVI_PLUGIN_MAGIC0);
    REQUIRE_V4(plugin->v3.v2.v1.magic1, OPEN_NAVI_PLUGIN_MAGIC1);
    REQUIRE_V4(plugin->v3.v2.v1.query_abi_version,
               OPEN_NAVI_PLUGIN_QUERY_ABI_V4);
    REQUIRE_V4(plugin->v3.v2.v1.struct_size, sizeof(*plugin));
    REQUIRE_V4(plugin->v3.v2.v1.endian_tag, OPEN_NAVI_PLUGIN_ENDIAN_TAG);
    REQUIRE_V4(plugin->v3.v2.v1.pointer_bytes, sizeof(void *));
    REQUIRE_V4(plugin->v3.v2.v1.backend_abi_version,
               OPEN_NAVI_BACKEND_ABI_V5);
    REQUIRE_V4(plugin->v3.v2.v1.backend_v1_size, sizeof(OpenNaviBackendV1));
    REQUIRE_V4(plugin->v3.v2.v1.backend_v2_size, sizeof(OpenNaviBackendV2));
    REQUIRE_V4(plugin->v3.v2.v1.render_target_v1_size,
               sizeof(OpenNaviBackendRenderTargetV1));
    REQUIRE_V4(plugin->v3.v2.v1.frame_v1_size,
               sizeof(OpenNaviBackendFrameV1));
    REQUIRE_V4(plugin->v3.v2.v1.gps_v1_size, sizeof(OpenNaviBackendGpsV1));
    REQUIRE_V4(plugin->v3.v2.v1.visibility_v1_size,
               sizeof(OpenNaviBackendVisibilityV1));
    REQUIRE_V4(plugin->v3.v2.v1.screen_v1_size,
               sizeof(OpenNaviBackendScreenV1));
    REQUIRE_V4(plugin->v3.v2.v1.pointer_v1_size,
               sizeof(OpenNaviBackendPointerV1));
    REQUIRE_V4(plugin->v3.v2.v1.services_v1_size,
               sizeof(OpenNaviBackendServicesV1));
    REQUIRE_V4(plugin->v3.v2.v1.vehicle_v2_size,
               sizeof(OpenNaviBackendVehicleV2));
    REQUIRE_V4(plugin->v3.v2.v1.gps_interval_v2_size,
               sizeof(OpenNaviBackendGpsIntervalV2));
    REQUIRE_V4(plugin->v3.v2.v1.audio_v2_size,
               sizeof(OpenNaviBackendAudioV2));
    REQUIRE_V4(plugin->v3.v2.v1.capabilities & ~(uint32_t)OPEN_NAVI_PLUGIN_OPTIONAL_CAPABILITIES,
               OPEN_NAVI_PLUGIN_REQUIRED_CAPABILITIES);
    REQUIRE_V4(plugin->v3.v2.services_v2_size,
               sizeof(OpenNaviBackendServicesV2));
    REQUIRE_V4(plugin->v3.v2.cluster_v1_size,
               sizeof(OpenNaviBackendClusterV1));
    REQUIRE_V4(plugin->v3.v2.utf8_span_v1_size,
               sizeof(OpenNaviBackendUtf8SpanV1));
    REQUIRE_V4(plugin->v3.v2.audio_availability_v1_size,
               sizeof(OpenNaviBackendAudioAvailabilityV1));
    REQUIRE_V4(plugin->v3.v2.thread_status_v1_size,
               sizeof(OpenNaviBackendThreadStatusV1));
    REQUIRE_V4(plugin->v3.v2.backend_v3_size, sizeof(OpenNaviBackendV3));
    REQUIRE_V4(plugin->v3.v2.capabilities_v2,
               OPEN_NAVI_PLUGIN_V2_REQUIRED_CAPABILITIES);
    REQUIRE_V4(plugin->v3.internationalization_v1_size,
               sizeof(OpenNaviBackendInternationalizationV1));
    REQUIRE_V4(plugin->v3.cluster_display_v1_size,
               sizeof(OpenNaviBackendClusterDisplayV1));
    REQUIRE_V4(plugin->v3.backend_v4_size, sizeof(OpenNaviBackendV4));
    REQUIRE_V4(plugin->v3.capabilities_v3,
               OPEN_NAVI_PLUGIN_V3_REQUIRED_CAPABILITIES);
    REQUIRE_V4(plugin->driver_distraction_v1_size,
               sizeof(OpenNaviBackendDriverDistractionV1));
    REQUIRE_V4(plugin->backend_v5_size, sizeof(OpenNaviBackendV5));
    REQUIRE_V4(plugin->capabilities_v4,
               OPEN_NAVI_PLUGIN_V4_REQUIRED_CAPABILITIES);
    if (memcmp(plugin->v3.v2.v1.build_id, g_v4_expected_build_id,
               sizeof(g_v4_expected_build_id)) != 0) return -1;
    backend = &plugin->backend_v5.v4.v3.v2.v1;
    if (backend->abi_version != OPEN_NAVI_BACKEND_ABI_V5 ||
        backend->struct_size != sizeof(OpenNaviBackendV5) ||
        backend->stop == NULL || backend->surface_created == NULL ||
        backend->surface_destroyed == NULL ||
        backend->visibility_changed == NULL ||
        backend->screen_changed == NULL || backend->gps_changed == NULL ||
        backend->pointer_changed == NULL || backend->render_frame == NULL ||
        plugin->backend_v5.v4.v3.v2.vehicle_changed == NULL ||
        plugin->backend_v5.v4.v3.v2.gps_interval_changed == NULL ||
        plugin->backend_v5.v4.v3.v2.audio_changed == NULL ||
        plugin->backend_v5.v4.v3.start_v2 == NULL ||
        plugin->backend_v5.v4.internationalization_changed == NULL ||
        plugin->backend_v5.v4.cluster_display_changed == NULL ||
        plugin->backend_v5.driver_distraction_changed == NULL) return -1;
    set_diagnostic(diagnostic, diagnostic_capacity,
                   "plugin v4 handshake valid");
    return 0;
#undef REQUIRE_V4
}

/* Query V5: the complete query-V4 handshake of the prefix (with the V5
 * query version, size, backend ABI V6 and build id), then the V6 tail. */
int open_navi_backend_plugin_validate_v5(const OpenNaviBackendPluginV5 *plugin,
                                    char *diagnostic,
                                    size_t diagnostic_capacity)
{
#define REQUIRE_V5(field, expected)                                           \
    do {                                                                      \
        if ((field) != (uint32_t)(expected)) {                                \
            set_diagnostic(diagnostic, diagnostic_capacity,                   \
                           "plugin v5 %s mismatch got=%u expected=%u",       \
                           #field, (unsigned int)(field),                      \
                           (unsigned int)(expected));                          \
            return -1;                                                        \
        }                                                                     \
    } while (0)
    OpenNaviBackendPluginV4 prefix;
    const OpenNaviBackendV6 *backend;
    if (plugin == NULL) return -1;
    REQUIRE_V5(plugin->v4.v3.v2.v1.query_abi_version,
               OPEN_NAVI_PLUGIN_QUERY_ABI_V5);
    REQUIRE_V5(plugin->v4.v3.v2.v1.struct_size, sizeof(*plugin));
    REQUIRE_V5(plugin->v4.v3.v2.v1.backend_abi_version,
               OPEN_NAVI_BACKEND_ABI_V6);
    if (memcmp(plugin->v4.v3.v2.v1.build_id, g_v5_expected_build_id,
               sizeof(g_v5_expected_build_id)) != 0) {
        set_diagnostic(diagnostic, diagnostic_capacity,
                       "plugin v5 build id mismatch");
        return -1;
    }
    /* Every query-V4 size and capability rule applies to the prefix. */
    prefix = plugin->v4;
    prefix.v3.v2.v1.query_abi_version = OPEN_NAVI_PLUGIN_QUERY_ABI_V4;
    prefix.v3.v2.v1.struct_size = sizeof(prefix);
    prefix.v3.v2.v1.backend_abi_version = OPEN_NAVI_BACKEND_ABI_V5;
    memcpy(prefix.v3.v2.v1.build_id, g_v4_expected_build_id,
           sizeof(g_v4_expected_build_id));
    prefix.backend_v5 = plugin->backend_v6.v5;
    prefix.backend_v5.v4.v3.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V5;
    prefix.backend_v5.v4.v3.v2.v1.struct_size = sizeof(OpenNaviBackendV5);
    if (open_navi_backend_plugin_validate_v4(&prefix, diagnostic,
                                             diagnostic_capacity) != 0)
        return -1;
    REQUIRE_V5(plugin->services_v3_size, sizeof(OpenNaviBackendServicesV3));
    REQUIRE_V5(plugin->guidance_state_v1_size,
               sizeof(OpenNaviBackendGuidanceStateV1));
    REQUIRE_V5(plugin->guidance_stop_v1_size,
               sizeof(OpenNaviBackendGuidanceStopV1));
    REQUIRE_V5(plugin->backend_v6_size, sizeof(OpenNaviBackendV6));
    REQUIRE_V5(plugin->capabilities_v5,
               OPEN_NAVI_PLUGIN_V5_REQUIRED_CAPABILITIES);
    backend = &plugin->backend_v6;
    if (backend->v5.v4.v3.v2.v1.abi_version != OPEN_NAVI_BACKEND_ABI_V6 ||
        backend->v5.v4.v3.v2.v1.struct_size != sizeof(OpenNaviBackendV6) ||
        backend->start_v3 == NULL ||
        backend->guidance_stop_requested == NULL) {
        set_diagnostic(diagnostic, diagnostic_capacity,
                       "plugin v5 descriptor missing required callback");
        return -1;
    }
    set_diagnostic(diagnostic, diagnostic_capacity,
                   "plugin v5 handshake valid");
    return 0;
#undef REQUIRE_V5
}

static int call_default_start(const OpenNaviBackendServicesV1 *services)
{
    const OpenNaviBackendV1 *backend = open_navi_default_backend_v1();
    int result = backend->start != NULL
        ? backend->start(backend->backend_context, services) : 0;
    if (result == 0) {
        g_loader.default_started = 1;
        increment_counter(&g_loader.test.default_starts);
    }
    return result;
}

static uintptr_t next_plugin_service_token_locked(void)
{
    if (g_loader.next_plugin_service_token == UINTPTR_MAX)
        return (uintptr_t)0u;
    ++g_loader.next_plugin_service_token;
    return g_loader.next_plugin_service_token;
}

static void revoke_plugin_services_locked(void)
{
    g_loader.active_plugin_service_token = (uintptr_t)0u;
}

static int acquire_upstream_services(
    void *runtime_context, OpenNaviBackendServicesV2 *services_out)
{
    uintptr_t token = (uintptr_t)runtime_context;
    int accepted;

    (void)pthread_mutex_lock(&g_loader.lock);
    accepted = token != (uintptr_t)0u &&
               token == g_loader.active_plugin_service_token &&
               g_loader.selection == WRAPPER_PLUGIN &&
               g_loader.plugin_started != 0;
    if (accepted != 0) {
        *services_out = g_loader.upstream_services;
        if (g_loader.test.outbound_accepted != UINT32_MAX)
            ++g_loader.test.outbound_accepted;
    } else if (g_loader.test.outbound_rejected != UINT32_MAX) {
        ++g_loader.test.outbound_rejected;
    }
    (void)pthread_mutex_unlock(&g_loader.lock);
    return accepted;
}

static uint32_t wrapper_publish_cluster(
    void *runtime_context, const OpenNaviBackendClusterV1 *cluster)
{
    OpenNaviBackendServicesV2 upstream;
    if (!acquire_upstream_services(runtime_context, &upstream))
        return OPEN_NAVI_BACKEND_SERVICE_STALE;
    return upstream.publish_cluster != NULL
        ? upstream.publish_cluster(upstream.v1.runtime_context, cluster)
        : OPEN_NAVI_BACKEND_SERVICE_UNAVAILABLE;
}

static int wrapper_request_frame(void *runtime_context)
{
    uintptr_t token = (uintptr_t)runtime_context;
    OpenNaviBackendServicesV2 upstream;
    int accepted;

    (void)pthread_mutex_lock(&g_loader.lock);
    accepted = token != (uintptr_t)0u &&
               token == g_loader.active_plugin_service_token &&
               g_loader.selection == WRAPPER_PLUGIN;
    upstream = g_loader.upstream_services;
    (void)pthread_mutex_unlock(&g_loader.lock);
    if (!accepted || upstream.v1.request_frame == NULL) return -1;
    return upstream.v1.request_frame(upstream.v1.runtime_context);
}

static uint32_t wrapper_publish_audio_availability(
    void *runtime_context, const OpenNaviBackendAudioAvailabilityV1 *audio)
{
    OpenNaviBackendServicesV2 upstream;
    if (!acquire_upstream_services(runtime_context, &upstream))
        return OPEN_NAVI_BACKEND_SERVICE_STALE;
    return upstream.publish_audio_availability != NULL
        ? upstream.publish_audio_availability(
              upstream.v1.runtime_context, audio)
        : OPEN_NAVI_BACKEND_SERVICE_UNAVAILABLE;
}

static uint32_t wrapper_publish_thread_status(
    void *runtime_context, const OpenNaviBackendThreadStatusV1 *thread_status)
{
    OpenNaviBackendServicesV2 upstream;
    if (!acquire_upstream_services(runtime_context, &upstream))
        return OPEN_NAVI_BACKEND_SERVICE_STALE;
    return upstream.publish_thread_status != NULL
        ? upstream.publish_thread_status(
              upstream.v1.runtime_context, thread_status)
        : OPEN_NAVI_BACKEND_SERVICE_UNAVAILABLE;
}

static int wrapper_start(void *context,
                         const OpenNaviBackendServicesV1 *services)
{
    int result;
    (void)context;
    g_loader.selection = WRAPPER_PLUGIN;
    g_loader.plugin_started = 0;
    g_loader.plugin_surfaced = 0;
    g_loader.default_started = 0;
    g_loader.default_surfaced = 0;
    increment_counter(&g_loader.test.wrapper_starts);
    increment_counter(&g_loader.test.plugin_starts);
    result = g_loader.plugin.v1.start(g_loader.plugin.v1.backend_context,
                                      services);
    if (result == 0) {
        g_loader.plugin_started = 1;
        return 0;
    }
    set_fallback(OPEN_NAVI_BACKEND_FALLBACK_START);
    g_loader.selection = WRAPPER_DEFAULT;
    return call_default_start(services);
}

static uint32_t wrapper_publish_guidance_state(
    void *runtime_context, const OpenNaviBackendGuidanceStateV1 *guidance)
{
    OpenNaviBackendServicesV2 upstream;
    uint32_t (*publish)(void *, const OpenNaviBackendGuidanceStateV1 *);
    if (!acquire_upstream_services(runtime_context, &upstream))
        return OPEN_NAVI_BACKEND_SERVICE_STALE;
    (void)pthread_mutex_lock(&g_loader.lock);
    publish = g_loader.upstream_publish_guidance_state;
    (void)pthread_mutex_unlock(&g_loader.lock);
    return publish != NULL
        ? publish(upstream.v1.runtime_context, guidance)
        : OPEN_NAVI_BACKEND_SERVICE_UNAVAILABLE;
}

/* Starts the plugin with scoped outbound services: through start_v2 (V3-V5
 * backends) or, when services_v3 is given, through start_v3 (V6). */
static int start_plugin_scoped(const OpenNaviBackendServicesV2 *services,
                               const OpenNaviBackendServicesV3 *services_v3)
{
    OpenNaviBackendServicesV3 scoped;
    OpenNaviBackendServicesV1 fallback_services;
    uintptr_t token;
    int result;

    if (services == NULL ||
        services->v1.struct_size < sizeof(*services) ||
        services->publish_cluster == NULL ||
        services->publish_audio_availability == NULL ||
        services->publish_thread_status == NULL) {
        return -1;
    }
    (void)pthread_mutex_lock(&g_loader.lock);
    g_loader.selection = WRAPPER_PLUGIN;
    g_loader.plugin_started = 0;
    g_loader.plugin_surfaced = 0;
    g_loader.default_started = 0;
    g_loader.default_surfaced = 0;
    g_loader.upstream_services = *services;
    g_loader.upstream_publish_guidance_state =
        services_v3 != NULL ? services_v3->publish_guidance_state : NULL;
    token = next_plugin_service_token_locked();
    g_loader.active_plugin_service_token = token;
    (void)pthread_mutex_unlock(&g_loader.lock);
    if (token == (uintptr_t)0u) return -1;

    memset(&scoped, 0, sizeof(scoped));
    scoped.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V2;
    scoped.v2.v1.struct_size = sizeof(scoped.v2);
    scoped.v2.v1.runtime_context = (void *)token;
    scoped.v2.v1.request_frame = wrapper_request_frame;
    scoped.v2.publish_cluster = wrapper_publish_cluster;
    scoped.v2.publish_audio_availability =
        wrapper_publish_audio_availability;
    scoped.v2.publish_thread_status = wrapper_publish_thread_status;

    increment_counter(&g_loader.test.wrapper_starts);
    increment_counter(&g_loader.test.plugin_starts);
    if (services_v3 != NULL) {
        scoped.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V3;
        scoped.v2.v1.struct_size = sizeof(scoped);
        scoped.publish_guidance_state = wrapper_publish_guidance_state;
        result = g_loader.plugin_v6.start_v3(
            g_loader.plugin_v6.v5.v4.v3.v2.v1.backend_context, &scoped);
    } else {
        result = g_loader.plugin_v3.start_v2(
            g_loader.plugin_v3.v2.v1.backend_context, &scoped.v2);
    }
    (void)pthread_mutex_lock(&g_loader.lock);
    if (result == 0) {
        g_loader.plugin_started = 1;
        (void)pthread_mutex_unlock(&g_loader.lock);
        return 0;
    }
    revoke_plugin_services_locked();
    g_loader.selection = WRAPPER_DEFAULT;
    (void)pthread_mutex_unlock(&g_loader.lock);
    set_fallback(OPEN_NAVI_BACKEND_FALLBACK_START);
    fallback_services = services->v1;
    fallback_services.abi_version = OPEN_NAVI_BACKEND_ABI_V1;
    fallback_services.struct_size = sizeof(fallback_services);
    return call_default_start(&fallback_services);
}

static int wrapper_start_v2(void *context,
                            const OpenNaviBackendServicesV2 *services)
{
    (void)context;
    if (services == NULL ||
        services->v1.abi_version != OPEN_NAVI_BACKEND_ABI_V2)
        return -1;
    return start_plugin_scoped(services, NULL);
}

static int wrapper_start_v3(void *context,
                            const OpenNaviBackendServicesV3 *services)
{
    (void)context;
    if (services == NULL ||
        services->v2.v1.abi_version != OPEN_NAVI_BACKEND_ABI_V3 ||
        services->v2.v1.struct_size < sizeof(*services) ||
        services->publish_guidance_state == NULL)
        return -1;
    return start_plugin_scoped(&services->v2, services);
}

static void wrapper_stop(void *context)
{
    const OpenNaviBackendV1 *fallback = open_navi_default_backend_v1();
    (void)context;
    (void)pthread_mutex_lock(&g_loader.lock);
    revoke_plugin_services_locked();
    (void)pthread_mutex_unlock(&g_loader.lock);
    if (g_loader.plugin_started != 0) {
        if (g_loader.query_abi == OPEN_NAVI_PLUGIN_QUERY_ABI_V2)
            g_loader.plugin_v3.v2.v1.stop(
                g_loader.plugin_v3.v2.v1.backend_context);
        else
            g_loader.plugin.v1.stop(g_loader.plugin.v1.backend_context);
        g_loader.plugin_started = 0;
        increment_counter(&g_loader.test.plugin_stops);
    }
    if (g_loader.default_started != 0) {
        if (fallback->stop != NULL)
            fallback->stop(fallback->backend_context);
        g_loader.default_started = 0;
        increment_counter(&g_loader.test.default_stops);
    }
    increment_counter(&g_loader.test.wrapper_stops);
}

static int wrapper_surface_created(
    void *context, const OpenNaviBackendRenderTargetV1 *target)
{
    const OpenNaviBackendV1 *fallback = open_navi_default_backend_v1();
    int result;
    (void)context;
    if (g_loader.selection == WRAPPER_PLUGIN) {
        result = g_loader.plugin.v1.surface_created(
            g_loader.plugin.v1.backend_context, target);
        if (result == 0) {
            g_loader.plugin_surfaced = 1;
            return 0;
        }
        set_fallback(OPEN_NAVI_BACKEND_FALLBACK_SURFACE);
        (void)pthread_mutex_lock(&g_loader.lock);
        revoke_plugin_services_locked();
        g_loader.selection = WRAPPER_DEFAULT;
        (void)pthread_mutex_unlock(&g_loader.lock);
        if (g_loader.default_started == 0 && call_default_start(NULL) != 0)
            return -1;
    }
    result = fallback->surface_created != NULL
        ? fallback->surface_created(fallback->backend_context, target) : 0;
    if (result == 0) g_loader.default_surfaced = 1;
    return result;
}

static void wrapper_surface_destroyed(void *context)
{
    const OpenNaviBackendV1 *fallback = open_navi_default_backend_v1();
    (void)context;
    if (g_loader.plugin_surfaced != 0) {
        g_loader.plugin.v1.surface_destroyed(
            g_loader.plugin.v1.backend_context);
        g_loader.plugin_surfaced = 0;
        increment_counter(&g_loader.test.plugin_surface_destroys);
    }
    if (g_loader.default_surfaced != 0) {
        if (fallback->surface_destroyed != NULL)
            fallback->surface_destroyed(fallback->backend_context);
        g_loader.default_surfaced = 0;
        increment_counter(&g_loader.test.default_surface_destroys);
    }
}

#define WRAP_V1_EVENT(name, type)                                             \
    static int wrapper_##name(void *context, const type *event)               \
    {                                                                         \
        const OpenNaviBackendV1 *fallback = open_navi_default_backend_v1();         \
        (void)context;                                                        \
        if (g_loader.selection == WRAPPER_PLUGIN)                             \
            return g_loader.plugin.v1.name(                                  \
                g_loader.plugin.v1.backend_context, event);                   \
        return fallback->name != NULL                                         \
            ? fallback->name(fallback->backend_context, event) : 0;           \
    }

WRAP_V1_EVENT(visibility_changed, OpenNaviBackendVisibilityV1)
WRAP_V1_EVENT(screen_changed, OpenNaviBackendScreenV1)
WRAP_V1_EVENT(gps_changed, OpenNaviBackendGpsV1)
WRAP_V1_EVENT(pointer_changed, OpenNaviBackendPointerV1)

static int wrapper_vehicle_changed(void *context,
                                   const OpenNaviBackendVehicleV2 *event)
{
    (void)context;
    return g_loader.selection == WRAPPER_PLUGIN
        ? g_loader.plugin.vehicle_changed(
              g_loader.plugin.v1.backend_context, event) : 0;
}

static int wrapper_gps_interval_changed(
    void *context, const OpenNaviBackendGpsIntervalV2 *event)
{
    (void)context;
    return g_loader.selection == WRAPPER_PLUGIN
        ? g_loader.plugin.gps_interval_changed(
              g_loader.plugin.v1.backend_context, event) : 0;
}

static int wrapper_audio_changed(void *context,
                                 const OpenNaviBackendAudioV2 *event)
{
    (void)context;
    return g_loader.selection == WRAPPER_PLUGIN
        ? g_loader.plugin.audio_changed(
              g_loader.plugin.v1.backend_context, event) : 0;
}

static int wrapper_internationalization_changed(
    void *context, const OpenNaviBackendInternationalizationV1 *event)
{
    (void)context;
    return g_loader.selection == WRAPPER_PLUGIN
        ? g_loader.plugin_v4.internationalization_changed(
              g_loader.plugin_v4.v3.v2.v1.backend_context, event) : 0;
}

static int wrapper_cluster_display_changed(
    void *context, const OpenNaviBackendClusterDisplayV1 *event)
{
    (void)context;
    return g_loader.selection == WRAPPER_PLUGIN
        ? g_loader.plugin_v4.cluster_display_changed(
              g_loader.plugin_v4.v3.v2.v1.backend_context, event) : 0;
}

static int wrapper_driver_distraction_changed(
    void *context, const OpenNaviBackendDriverDistractionV1 *event)
{
    (void)context;
    return g_loader.selection == WRAPPER_PLUGIN
        ? g_loader.plugin_v5.driver_distraction_changed(
              g_loader.plugin_v5.v4.v3.v2.v1.backend_context, event) : 0;
}

static int wrapper_guidance_stop_requested(
    void *context, const OpenNaviBackendGuidanceStopV1 *event)
{
    (void)context;
    return g_loader.selection == WRAPPER_PLUGIN
        ? g_loader.plugin_v6.guidance_stop_requested(
              g_loader.plugin_v6.v5.v4.v3.v2.v1.backend_context, event) : 0;
}

static int wrapper_render_frame(void *context,
                                const OpenNaviBackendRenderTargetV1 *target,
                                const OpenNaviBackendFrameV1 *frame)
{
    const OpenNaviBackendV1 *fallback = open_navi_default_backend_v1();
    int result;
    (void)context;
    if (g_loader.selection == WRAPPER_PLUGIN) {
        result = g_loader.plugin.v1.render_frame(
            g_loader.plugin.v1.backend_context, target, frame);
        if (result == 0) return 0;
        /* GL teardown is unsafe inside render_frame. Stop dispatching to the
         * plugin, initialize the trivial fallback in the still-current
         * context, draw magenta now, and defer plugin surface destruction and
         * stop to the normal surface_destroyed/stop ordering. */
        set_fallback(OPEN_NAVI_BACKEND_FALLBACK_RENDER);
        (void)pthread_mutex_lock(&g_loader.lock);
        revoke_plugin_services_locked();
        g_loader.selection = WRAPPER_DEFAULT;
        (void)pthread_mutex_unlock(&g_loader.lock);
        if (g_loader.default_started == 0 && call_default_start(NULL) != 0)
            return result;
        if (fallback->surface_created != NULL &&
            fallback->surface_created(fallback->backend_context, target) != 0)
            return result;
        g_loader.default_surfaced = 1;
    }
    return fallback->render_frame != NULL
        ? fallback->render_frame(fallback->backend_context, target, frame) : 0;
}

static void initialize_wrapper(void)
{
    memset(&g_loader.wrapper, 0, sizeof(g_loader.wrapper));
    g_loader.wrapper.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V2;
    g_loader.wrapper.v1.struct_size = sizeof(g_loader.wrapper);
    g_loader.wrapper.v1.backend_context = &g_loader;
    g_loader.wrapper.v1.start = wrapper_start;
    g_loader.wrapper.v1.stop = wrapper_stop;
    g_loader.wrapper.v1.surface_created = wrapper_surface_created;
    g_loader.wrapper.v1.surface_destroyed = wrapper_surface_destroyed;
    g_loader.wrapper.v1.visibility_changed = wrapper_visibility_changed;
    g_loader.wrapper.v1.screen_changed = wrapper_screen_changed;
    g_loader.wrapper.v1.gps_changed = wrapper_gps_changed;
    g_loader.wrapper.v1.pointer_changed = wrapper_pointer_changed;
    g_loader.wrapper.v1.render_frame = wrapper_render_frame;
    g_loader.wrapper.vehicle_changed = wrapper_vehicle_changed;
    g_loader.wrapper.gps_interval_changed = wrapper_gps_interval_changed;
    g_loader.wrapper.audio_changed = wrapper_audio_changed;
}

static void initialize_wrapper_v3(void)
{
    initialize_wrapper();
    memset(&g_loader.wrapper_v3, 0, sizeof(g_loader.wrapper_v3));
    g_loader.wrapper_v3.v2 = g_loader.wrapper;
    g_loader.wrapper_v3.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V3;
    g_loader.wrapper_v3.v2.v1.struct_size = sizeof(g_loader.wrapper_v3);
    g_loader.wrapper_v3.start_v2 = wrapper_start_v2;
}

static void initialize_wrapper_v4(void)
{
    initialize_wrapper_v3();
    memset(&g_loader.wrapper_v4, 0, sizeof(g_loader.wrapper_v4));
    g_loader.wrapper_v4.v3 = g_loader.wrapper_v3;
    g_loader.wrapper_v4.v3.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V4;
    g_loader.wrapper_v4.v3.v2.v1.struct_size = sizeof(g_loader.wrapper_v4);
    g_loader.wrapper_v4.internationalization_changed =
        wrapper_internationalization_changed;
    g_loader.wrapper_v4.cluster_display_changed =
        wrapper_cluster_display_changed;
}

/* Optional capabilities switch renderer payload versions before start. */
static void apply_optional_capabilities(uint32_t capabilities)
{
    (void)open_navi_renderer_set_pointer_contacts(
        (capabilities & OPEN_NAVI_PLUGIN_CAP_POINTER_CONTACTS) != 0u);
    (void)open_navi_renderer_set_window_area(
        (capabilities & OPEN_NAVI_PLUGIN_CAP_WINDOW_AREA) != 0u);
}

static void initialize_wrapper_v5(void)
{
    initialize_wrapper_v4();
    memset(&g_loader.wrapper_v5, 0, sizeof(g_loader.wrapper_v5));
    g_loader.wrapper_v5.v4 = g_loader.wrapper_v4;
    g_loader.wrapper_v5.v4.v3.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V5;
    g_loader.wrapper_v5.v4.v3.v2.v1.struct_size = sizeof(g_loader.wrapper_v5);
    g_loader.wrapper_v5.driver_distraction_changed =
        wrapper_driver_distraction_changed;
}

static void initialize_wrapper_v6(void)
{
    initialize_wrapper_v5();
    memset(&g_loader.wrapper_v6, 0, sizeof(g_loader.wrapper_v6));
    g_loader.wrapper_v6.v5 = g_loader.wrapper_v5;
    g_loader.wrapper_v6.v5.v4.v3.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V6;
    g_loader.wrapper_v6.v5.v4.v3.v2.v1.struct_size =
        sizeof(g_loader.wrapper_v6);
    g_loader.wrapper_v6.start_v3 = wrapper_start_v3;
    g_loader.wrapper_v6.guidance_stop_requested =
        wrapper_guidance_stop_requested;
}

static void bootstrap_once(void)
{
    OpenNaviBackendPluginV1 query_result;
    OpenNaviBackendPluginV2 query_result_v2;
    OpenNaviBackendPluginV3 query_result_v3;
    OpenNaviBackendPluginV4 query_result_v4;
    OpenNaviBackendPluginV5 query_result_v5;
    OpenNaviBackendPluginQueryV1 query;
    union {
        void *object;
        OpenNaviBackendPluginQueryV1 function;
    } conversion;
    char diagnostic[257];
    const char *error;
    int status;

    increment_counter(&g_loader.test.load_attempts);
    g_loader.pinned_handle = dlopen(OPEN_NAVI_BACKEND_PLUGIN_PATH,
                                    RTLD_NOW | RTLD_LOCAL);
    if (g_loader.pinned_handle == NULL) {
        set_fallback(OPEN_NAVI_BACKEND_FALLBACK_LOAD);
        return;
    }
    set_flag(&g_loader.test.handle_pinned);
    (void)dlerror();
    conversion.object = dlsym(g_loader.pinned_handle,
                              "open_navi_backend_query_v1");
    error = dlerror();
    if (error != NULL || conversion.object == NULL) {
        set_fallback(OPEN_NAVI_BACKEND_FALLBACK_SYMBOL);
        return;
    }
    query = conversion.function;
    memset(&query_result_v5, 0, sizeof(query_result_v5));
    increment_counter(&g_loader.test.query_calls);
    status = query(OPEN_NAVI_PLUGIN_QUERY_ABI_V5,
                   sizeof(query_result_v5),
                   (OpenNaviBackendPluginV1 *)&query_result_v5);
    if (status == 0) {
        diagnostic[0] = '\0';
        if (open_navi_backend_plugin_validate_v5(&query_result_v5, diagnostic,
                                            sizeof(diagnostic)) != 0) {
            set_fallback(OPEN_NAVI_BACKEND_FALLBACK_HANDSHAKE);
            return;
        }
        g_loader.plugin_v6 = query_result_v5.backend_v6;
        g_loader.plugin_v5 = query_result_v5.backend_v6.v5;
        g_loader.plugin_v4 = query_result_v5.backend_v6.v5.v4;
        g_loader.plugin_v3 = query_result_v5.backend_v6.v5.v4.v3;
        g_loader.plugin = query_result_v5.backend_v6.v5.v4.v3.v2;
        g_loader.query_abi = OPEN_NAVI_PLUGIN_QUERY_ABI_V5;
        g_loader.test.query_abi = OPEN_NAVI_PLUGIN_QUERY_ABI_V5;
        initialize_wrapper_v6();
        apply_optional_capabilities(
            query_result_v5.v4.v3.v2.v1.capabilities);
        status = open_navi_backend_install_v6(&g_loader.wrapper_v6);
    } else {
        memset(&query_result_v4, 0, sizeof(query_result_v4));
        increment_counter(&g_loader.test.query_calls);
        status = query(OPEN_NAVI_PLUGIN_QUERY_ABI_V4,
                       sizeof(query_result_v4),
                       (OpenNaviBackendPluginV1 *)&query_result_v4);
        if (status == 0) {
            diagnostic[0] = '\0';
            if (open_navi_backend_plugin_validate_v4(&query_result_v4, diagnostic,
                                                sizeof(diagnostic)) != 0) {
                set_fallback(OPEN_NAVI_BACKEND_FALLBACK_HANDSHAKE);
                return;
            }
            g_loader.plugin_v5 = query_result_v4.backend_v5;
            g_loader.plugin_v4 = query_result_v4.backend_v5.v4;
            g_loader.plugin_v3 = query_result_v4.backend_v5.v4.v3;
            g_loader.plugin = query_result_v4.backend_v5.v4.v3.v2;
            g_loader.query_abi = OPEN_NAVI_PLUGIN_QUERY_ABI_V4;
            g_loader.test.query_abi = OPEN_NAVI_PLUGIN_QUERY_ABI_V4;
            initialize_wrapper_v5();
            apply_optional_capabilities(query_result_v4.v3.v2.v1.capabilities);
            status = open_navi_backend_install_v5(&g_loader.wrapper_v5);
        } else {
            memset(&query_result_v3, 0, sizeof(query_result_v3));
            increment_counter(&g_loader.test.query_calls);
            status = query(OPEN_NAVI_PLUGIN_QUERY_ABI_V3,
                           sizeof(query_result_v3),
                           (OpenNaviBackendPluginV1 *)&query_result_v3);
            if (status == 0) {
                diagnostic[0] = '\0';
                if (open_navi_backend_plugin_validate_v3(&query_result_v3, diagnostic,
                                                    sizeof(diagnostic)) != 0) {
                    set_fallback(OPEN_NAVI_BACKEND_FALLBACK_HANDSHAKE);
                    return;
                }
                g_loader.plugin_v4 = query_result_v3.backend_v4;
                g_loader.plugin_v3 = query_result_v3.backend_v4.v3;
                g_loader.plugin = query_result_v3.backend_v4.v3.v2;
                g_loader.query_abi = OPEN_NAVI_PLUGIN_QUERY_ABI_V3;
                g_loader.test.query_abi = OPEN_NAVI_PLUGIN_QUERY_ABI_V3;
                initialize_wrapper_v4();
                apply_optional_capabilities(query_result_v3.v2.v1.capabilities);
                status = open_navi_backend_install_v4(&g_loader.wrapper_v4);
            } else {
                memset(&query_result_v2, 0, sizeof(query_result_v2));
                increment_counter(&g_loader.test.query_calls);
                status = query(OPEN_NAVI_PLUGIN_QUERY_ABI_V2,
                               sizeof(query_result_v2),
                               (OpenNaviBackendPluginV1 *)&query_result_v2);
                if (status == 0) {
                    diagnostic[0] = '\0';
                    if (open_navi_backend_plugin_validate_v2(
                            &query_result_v2, diagnostic,
                            sizeof(diagnostic)) != 0) {
                        set_fallback(OPEN_NAVI_BACKEND_FALLBACK_HANDSHAKE);
                        return;
                    }
                    g_loader.plugin_v3 = query_result_v2.backend_v3;
                    g_loader.plugin = query_result_v2.backend_v3.v2;
                    g_loader.query_abi = OPEN_NAVI_PLUGIN_QUERY_ABI_V2;
                    g_loader.test.query_abi = OPEN_NAVI_PLUGIN_QUERY_ABI_V2;
                    initialize_wrapper_v3();
                    apply_optional_capabilities(query_result_v2.v1.capabilities);
                    status = open_navi_backend_install_v3(
                        &g_loader.wrapper_v3);
                } else {
                    memset(&query_result, 0, sizeof(query_result));
                    increment_counter(&g_loader.test.query_calls);
                    status = query(OPEN_NAVI_PLUGIN_QUERY_ABI_V1,
                                   sizeof(query_result), &query_result);
                    if (status != 0) {
                        set_fallback(OPEN_NAVI_BACKEND_FALLBACK_QUERY);
                        return;
                    }
                    diagnostic[0] = '\0';
                    if (open_navi_backend_plugin_validate(
                            &query_result, diagnostic,
                            sizeof(diagnostic)) != 0) {
                        set_fallback(OPEN_NAVI_BACKEND_FALLBACK_HANDSHAKE);
                        return;
                    }
                    g_loader.plugin = query_result.backend;
                    g_loader.query_abi = OPEN_NAVI_PLUGIN_QUERY_ABI_V1;
                    g_loader.test.query_abi = OPEN_NAVI_PLUGIN_QUERY_ABI_V1;
                    initialize_wrapper();
                    apply_optional_capabilities(query_result.capabilities);
                    status = open_navi_backend_install_v2(
                        &g_loader.wrapper);
                }
            }
        }
    }
    if (status != 0) {
        set_fallback(OPEN_NAVI_BACKEND_FALLBACK_INSTALL);
        return;
    }
    set_flag(&g_loader.test.plugin_valid);
}

void open_navi_backend_loader_bootstrap(void)
{
    increment_counter(&g_loader.test.bootstrap_calls);
    (void)pthread_once(&g_loader.once, bootstrap_once);
}

#if defined(OPEN_NAVI_TESTING)
void open_navi_backend_loader_test_state(OpenNaviBackendLoaderTestState *output)
{
    if (output == NULL) return;
    (void)pthread_mutex_lock(&g_loader.lock);
    *output = g_loader.test;
    (void)pthread_mutex_unlock(&g_loader.lock);
}
#endif
