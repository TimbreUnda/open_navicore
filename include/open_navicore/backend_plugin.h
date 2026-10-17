#ifndef OPEN_NAVI_BACKEND_PLUGIN_H
#define OPEN_NAVI_BACKEND_PLUGIN_H

#include <stdint.h>

#include <open_navicore/backend.h>

#if defined(__GNUC__)
#define OPEN_NAVI_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define OPEN_NAVI_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OPEN_NAVI_PLUGIN_QUERY_ABI_V1 = 1u,
    OPEN_NAVI_PLUGIN_QUERY_ABI_V2 = 2u,
    OPEN_NAVI_PLUGIN_QUERY_ABI_V3 = 3u,
    OPEN_NAVI_PLUGIN_QUERY_ABI_V4 = 4u,
    OPEN_NAVI_PLUGIN_ENDIAN_TAG = 0x01020304u,
    OPEN_NAVI_PLUGIN_BUILD_ID_BYTES = 32u,
    OPEN_NAVI_PLUGIN_CAP_LIFECYCLE = 1u << 0,
    OPEN_NAVI_PLUGIN_CAP_RENDER = 1u << 1,
    OPEN_NAVI_PLUGIN_CAP_V1_EVENTS = 1u << 2,
    OPEN_NAVI_PLUGIN_CAP_V2_EVENTS = 1u << 3,
    OPEN_NAVI_PLUGIN_CAP_V3_SERVICES = 1u << 4,
    OPEN_NAVI_PLUGIN_CAP_OUTBOUND_CLUSTER = 1u << 5,
    OPEN_NAVI_PLUGIN_CAP_OUTBOUND_AUDIO = 1u << 6,
    OPEN_NAVI_PLUGIN_CAP_OUTBOUND_THREAD = 1u << 7,
    OPEN_NAVI_PLUGIN_CAP_INPUT_INTERNATIONALIZATION = 1u << 8,
    OPEN_NAVI_PLUGIN_CAP_INPUT_CLUSTER_DISPLAY = 1u << 9,
    OPEN_NAVI_PLUGIN_CAP_INPUT_DRIVER_DISTRACTION = 1u << 10,
    /* Optional in every query version: the plugin understands
     * OpenNaviBackendPointerV2 and wants every touch contact. */
    OPEN_NAVI_PLUGIN_CAP_POINTER_CONTACTS = 1u << 11,
    /* Optional in every query version: the plugin understands
     * OpenNaviBackendVisibilityV2 (the window area the HMI shows). */
    OPEN_NAVI_PLUGIN_CAP_WINDOW_AREA = 1u << 12,
    OPEN_NAVI_PLUGIN_OPTIONAL_CAPABILITIES =
        OPEN_NAVI_PLUGIN_CAP_POINTER_CONTACTS | OPEN_NAVI_PLUGIN_CAP_WINDOW_AREA,
    OPEN_NAVI_PLUGIN_REQUIRED_CAPABILITIES =
        OPEN_NAVI_PLUGIN_CAP_LIFECYCLE |
        OPEN_NAVI_PLUGIN_CAP_RENDER |
        OPEN_NAVI_PLUGIN_CAP_V1_EVENTS |
        OPEN_NAVI_PLUGIN_CAP_V2_EVENTS,
    OPEN_NAVI_PLUGIN_KNOWN_CAPABILITIES =
        OPEN_NAVI_PLUGIN_REQUIRED_CAPABILITIES
};

enum {
    OPEN_NAVI_PLUGIN_V2_REQUIRED_CAPABILITIES =
        OPEN_NAVI_PLUGIN_REQUIRED_CAPABILITIES |
        OPEN_NAVI_PLUGIN_CAP_V3_SERVICES |
        OPEN_NAVI_PLUGIN_CAP_OUTBOUND_CLUSTER |
        OPEN_NAVI_PLUGIN_CAP_OUTBOUND_AUDIO |
        OPEN_NAVI_PLUGIN_CAP_OUTBOUND_THREAD,
    OPEN_NAVI_PLUGIN_V2_KNOWN_CAPABILITIES =
        OPEN_NAVI_PLUGIN_V2_REQUIRED_CAPABILITIES
};

enum {
    OPEN_NAVI_PLUGIN_V3_REQUIRED_CAPABILITIES =
        OPEN_NAVI_PLUGIN_V2_REQUIRED_CAPABILITIES |
        OPEN_NAVI_PLUGIN_CAP_INPUT_INTERNATIONALIZATION |
        OPEN_NAVI_PLUGIN_CAP_INPUT_CLUSTER_DISPLAY,
    OPEN_NAVI_PLUGIN_V3_KNOWN_CAPABILITIES =
        OPEN_NAVI_PLUGIN_V3_REQUIRED_CAPABILITIES
};

enum {
    OPEN_NAVI_PLUGIN_V4_REQUIRED_CAPABILITIES =
        OPEN_NAVI_PLUGIN_V3_REQUIRED_CAPABILITIES |
        OPEN_NAVI_PLUGIN_CAP_INPUT_DRIVER_DISTRACTION,
    OPEN_NAVI_PLUGIN_V4_KNOWN_CAPABILITIES =
        OPEN_NAVI_PLUGIN_V4_REQUIRED_CAPABILITIES
};

#define OPEN_NAVI_PLUGIN_MAGIC0 UINT32_C(0x4d4e4250) /* MNBP */
#define OPEN_NAVI_PLUGIN_MAGIC1 UINT32_C(0x514e5831) /* QNX1 */

/* The provider pins one reviewed plugin build. Changing the backend requires
 * rebuilding the provider with a deliberately changed expected identifier. */
#define OPEN_NAVI_PLUGIN_EXPECTED_BUILD_ID_INITIALIZER                         \
    {                                                                        \
        0x52, 0x45, 0x46, 0x2d, 0x42, 0x41, 0x43, 0x4b,                    \
        0x45, 0x4e, 0x44, 0x2d, 0x51, 0x4e, 0x58, 0x2d,                    \
        0x56, 0x31, 0x2d, 0x32, 0x30, 0x32, 0x36, 0x31,                    \
        0x30, 0x30, 0x39, 0x2d, 0x30, 0x30, 0x30, 0x31                     \
    }

#define OPEN_NAVI_PLUGIN_V2_EXPECTED_BUILD_ID_INITIALIZER                      \
    {                                                                        \
        0x52, 0x45, 0x46, 0x2d, 0x42, 0x41, 0x43, 0x4b,                    \
        0x45, 0x4e, 0x44, 0x2d, 0x51, 0x4e, 0x58, 0x2d,                    \
        0x56, 0x32, 0x2d, 0x32, 0x30, 0x32, 0x36, 0x31,                    \
        0x30, 0x30, 0x39, 0x2d, 0x30, 0x30, 0x30, 0x31                     \
    }

#define OPEN_NAVI_PLUGIN_V3_EXPECTED_BUILD_ID_INITIALIZER                      \
    {                                                                        \
        0x52, 0x45, 0x46, 0x2d, 0x42, 0x41, 0x43, 0x4b,                    \
        0x45, 0x4e, 0x44, 0x2d, 0x51, 0x4e, 0x58, 0x2d,                    \
        0x56, 0x33, 0x2d, 0x32, 0x30, 0x32, 0x36, 0x31,                    \
        0x30, 0x30, 0x39, 0x2d, 0x30, 0x30, 0x30, 0x31                     \
    }

#define OPEN_NAVI_PLUGIN_V4_EXPECTED_BUILD_ID_INITIALIZER                      \
    {                                                                        \
        0x52, 0x45, 0x46, 0x2d, 0x42, 0x41, 0x43, 0x4b,                    \
        0x45, 0x4e, 0x44, 0x2d, 0x51, 0x4e, 0x58, 0x2d,                    \
        0x56, 0x34, 0x2d, 0x32, 0x30, 0x32, 0x36, 0x31,                    \
        0x30, 0x30, 0x39, 0x2d, 0x30, 0x30, 0x30, 0x31                     \
    }

/* This is a copied, pointer-bearing in-process handshake. It is deliberately
 * stricter than OpenNaviBackendV2: every size and capability must match before
 * the provider copies the descriptor and invokes any plugin callback. */
typedef struct {
    uint32_t magic0;
    uint32_t magic1;
    uint32_t query_abi_version;
    uint32_t struct_size;
    uint32_t endian_tag;
    uint32_t pointer_bytes;
    uint32_t backend_abi_version;
    uint32_t backend_v1_size;
    uint32_t backend_v2_size;
    uint32_t render_target_v1_size;
    uint32_t frame_v1_size;
    uint32_t gps_v1_size;
    uint32_t visibility_v1_size;
    uint32_t screen_v1_size;
    uint32_t pointer_v1_size;
    uint32_t services_v1_size;
    uint32_t vehicle_v2_size;
    uint32_t gps_interval_v2_size;
    uint32_t audio_v2_size;
    uint32_t capabilities;
    uint8_t build_id[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES];
    OpenNaviBackendV2 backend;
} OpenNaviBackendPluginV1;

/* Query V2 is append-only at the query-contract level: query V1 and its exact
 * output size remain accepted.  The V1 prefix is retained for layout/size
 * auditing; backend_v3 is the descriptor installed for a V2 query. */
typedef struct {
    OpenNaviBackendPluginV1 v1;
    uint32_t services_v2_size;
    uint32_t cluster_v1_size;
    uint32_t utf8_span_v1_size;
    uint32_t audio_availability_v1_size;
    uint32_t thread_status_v1_size;
    uint32_t backend_v3_size;
    uint32_t capabilities_v2;
    OpenNaviBackendV3 backend_v3;
} OpenNaviBackendPluginV2;

/* Query V3 retains the complete query-V2 prefix and selects a V4 backend. */
typedef struct {
    OpenNaviBackendPluginV2 v2;
    uint32_t internationalization_v1_size;
    uint32_t cluster_display_v1_size;
    uint32_t backend_v4_size;
    uint32_t capabilities_v3;
    OpenNaviBackendV4 backend_v4;
} OpenNaviBackendPluginV3;

typedef struct {
    OpenNaviBackendPluginV3 v3;
    uint32_t driver_distraction_v1_size;
    uint32_t backend_v5_size;
    uint32_t capabilities_v4;
    OpenNaviBackendV5 backend_v5;
} OpenNaviBackendPluginV4;

typedef int (*OpenNaviBackendPluginQueryV1)(
    uint32_t requested_query_abi,
    uint32_t output_size,
    OpenNaviBackendPluginV1 *output);

typedef int (*OpenNaviBackendPluginQueryV2)(
    uint32_t requested_query_abi,
    uint32_t output_size,
    OpenNaviBackendPluginV2 *output);

OPEN_NAVI_PLUGIN_EXPORT int open_navi_backend_query_v1(
    uint32_t requested_query_abi,
    uint32_t output_size,
    OpenNaviBackendPluginV1 *output);

#ifdef __cplusplus
}
#endif

#endif
