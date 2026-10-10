#ifndef MOCK_NAV_BACKEND_PLUGIN_H
#define MOCK_NAV_BACKEND_PLUGIN_H

#include <stdint.h>

#include "mock_nav_backend.h"

#if defined(__GNUC__)
#define MOCK_NAV_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define MOCK_NAV_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MOCK_NAV_PLUGIN_QUERY_ABI_V1 = 1u,
    MOCK_NAV_PLUGIN_QUERY_ABI_V2 = 2u,
    MOCK_NAV_PLUGIN_QUERY_ABI_V3 = 3u,
    MOCK_NAV_PLUGIN_QUERY_ABI_V4 = 4u,
    MOCK_NAV_PLUGIN_ENDIAN_TAG = 0x01020304u,
    MOCK_NAV_PLUGIN_BUILD_ID_BYTES = 32u,
    MOCK_NAV_PLUGIN_CAP_LIFECYCLE = 1u << 0,
    MOCK_NAV_PLUGIN_CAP_RENDER = 1u << 1,
    MOCK_NAV_PLUGIN_CAP_V1_EVENTS = 1u << 2,
    MOCK_NAV_PLUGIN_CAP_V2_EVENTS = 1u << 3,
    MOCK_NAV_PLUGIN_CAP_V3_SERVICES = 1u << 4,
    MOCK_NAV_PLUGIN_CAP_OUTBOUND_CLUSTER = 1u << 5,
    MOCK_NAV_PLUGIN_CAP_OUTBOUND_AUDIO = 1u << 6,
    MOCK_NAV_PLUGIN_CAP_OUTBOUND_THREAD = 1u << 7,
    MOCK_NAV_PLUGIN_CAP_INPUT_INTERNATIONALIZATION = 1u << 8,
    MOCK_NAV_PLUGIN_CAP_INPUT_CLUSTER_DISPLAY = 1u << 9,
    MOCK_NAV_PLUGIN_CAP_INPUT_DRIVER_DISTRACTION = 1u << 10,
    MOCK_NAV_PLUGIN_REQUIRED_CAPABILITIES =
        MOCK_NAV_PLUGIN_CAP_LIFECYCLE |
        MOCK_NAV_PLUGIN_CAP_RENDER |
        MOCK_NAV_PLUGIN_CAP_V1_EVENTS |
        MOCK_NAV_PLUGIN_CAP_V2_EVENTS,
    MOCK_NAV_PLUGIN_KNOWN_CAPABILITIES =
        MOCK_NAV_PLUGIN_REQUIRED_CAPABILITIES
};

enum {
    MOCK_NAV_PLUGIN_V2_REQUIRED_CAPABILITIES =
        MOCK_NAV_PLUGIN_REQUIRED_CAPABILITIES |
        MOCK_NAV_PLUGIN_CAP_V3_SERVICES |
        MOCK_NAV_PLUGIN_CAP_OUTBOUND_CLUSTER |
        MOCK_NAV_PLUGIN_CAP_OUTBOUND_AUDIO |
        MOCK_NAV_PLUGIN_CAP_OUTBOUND_THREAD,
    MOCK_NAV_PLUGIN_V2_KNOWN_CAPABILITIES =
        MOCK_NAV_PLUGIN_V2_REQUIRED_CAPABILITIES
};

enum {
    MOCK_NAV_PLUGIN_V3_REQUIRED_CAPABILITIES =
        MOCK_NAV_PLUGIN_V2_REQUIRED_CAPABILITIES |
        MOCK_NAV_PLUGIN_CAP_INPUT_INTERNATIONALIZATION |
        MOCK_NAV_PLUGIN_CAP_INPUT_CLUSTER_DISPLAY,
    MOCK_NAV_PLUGIN_V3_KNOWN_CAPABILITIES =
        MOCK_NAV_PLUGIN_V3_REQUIRED_CAPABILITIES
};

enum {
    MOCK_NAV_PLUGIN_V4_REQUIRED_CAPABILITIES =
        MOCK_NAV_PLUGIN_V3_REQUIRED_CAPABILITIES |
        MOCK_NAV_PLUGIN_CAP_INPUT_DRIVER_DISTRACTION,
    MOCK_NAV_PLUGIN_V4_KNOWN_CAPABILITIES =
        MOCK_NAV_PLUGIN_V4_REQUIRED_CAPABILITIES
};

#define MOCK_NAV_PLUGIN_MAGIC0 UINT32_C(0x4d4e4250) /* MNBP */
#define MOCK_NAV_PLUGIN_MAGIC1 UINT32_C(0x514e5831) /* QNX1 */

/* The provider pins one reviewed plugin build. Changing the backend requires
 * rebuilding the provider with a deliberately changed expected identifier. */
#define MOCK_NAV_PLUGIN_EXPECTED_BUILD_ID_INITIALIZER                         \
    {                                                                        \
        0x52, 0x45, 0x46, 0x2d, 0x42, 0x41, 0x43, 0x4b,                    \
        0x45, 0x4e, 0x44, 0x2d, 0x51, 0x4e, 0x58, 0x2d,                    \
        0x56, 0x31, 0x2d, 0x32, 0x30, 0x32, 0x36, 0x31,                    \
        0x30, 0x30, 0x39, 0x2d, 0x30, 0x30, 0x30, 0x31                     \
    }

#define MOCK_NAV_PLUGIN_V2_EXPECTED_BUILD_ID_INITIALIZER                      \
    {                                                                        \
        0x52, 0x45, 0x46, 0x2d, 0x42, 0x41, 0x43, 0x4b,                    \
        0x45, 0x4e, 0x44, 0x2d, 0x51, 0x4e, 0x58, 0x2d,                    \
        0x56, 0x32, 0x2d, 0x32, 0x30, 0x32, 0x36, 0x31,                    \
        0x30, 0x30, 0x39, 0x2d, 0x30, 0x30, 0x30, 0x31                     \
    }

#define MOCK_NAV_PLUGIN_V3_EXPECTED_BUILD_ID_INITIALIZER                      \
    {                                                                        \
        0x52, 0x45, 0x46, 0x2d, 0x42, 0x41, 0x43, 0x4b,                    \
        0x45, 0x4e, 0x44, 0x2d, 0x51, 0x4e, 0x58, 0x2d,                    \
        0x56, 0x33, 0x2d, 0x32, 0x30, 0x32, 0x36, 0x31,                    \
        0x30, 0x30, 0x39, 0x2d, 0x30, 0x30, 0x30, 0x31                     \
    }

#define MOCK_NAV_PLUGIN_V4_EXPECTED_BUILD_ID_INITIALIZER                      \
    {                                                                        \
        0x52, 0x45, 0x46, 0x2d, 0x42, 0x41, 0x43, 0x4b,                    \
        0x45, 0x4e, 0x44, 0x2d, 0x51, 0x4e, 0x58, 0x2d,                    \
        0x56, 0x34, 0x2d, 0x32, 0x30, 0x32, 0x36, 0x31,                    \
        0x30, 0x30, 0x39, 0x2d, 0x30, 0x30, 0x30, 0x31                     \
    }

/* This is a copied, pointer-bearing in-process handshake. It is deliberately
 * stricter than MockNavBackendV2: every size and capability must match before
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
    uint8_t build_id[MOCK_NAV_PLUGIN_BUILD_ID_BYTES];
    MockNavBackendV2 backend;
} MockNavBackendPluginV1;

/* Query V2 is append-only at the query-contract level: query V1 and its exact
 * output size remain accepted.  The V1 prefix is retained for layout/size
 * auditing; backend_v3 is the descriptor installed for a V2 query. */
typedef struct {
    MockNavBackendPluginV1 v1;
    uint32_t services_v2_size;
    uint32_t cluster_v1_size;
    uint32_t utf8_span_v1_size;
    uint32_t audio_availability_v1_size;
    uint32_t thread_status_v1_size;
    uint32_t backend_v3_size;
    uint32_t capabilities_v2;
    MockNavBackendV3 backend_v3;
} MockNavBackendPluginV2;

/* Query V3 retains the complete query-V2 prefix and selects a V4 backend. */
typedef struct {
    MockNavBackendPluginV2 v2;
    uint32_t internationalization_v1_size;
    uint32_t cluster_display_v1_size;
    uint32_t backend_v4_size;
    uint32_t capabilities_v3;
    MockNavBackendV4 backend_v4;
} MockNavBackendPluginV3;

typedef struct {
    MockNavBackendPluginV3 v3;
    uint32_t driver_distraction_v1_size;
    uint32_t backend_v5_size;
    uint32_t capabilities_v4;
    MockNavBackendV5 backend_v5;
} MockNavBackendPluginV4;

typedef int (*MockNavBackendPluginQueryV1)(
    uint32_t requested_query_abi,
    uint32_t output_size,
    MockNavBackendPluginV1 *output);

typedef int (*MockNavBackendPluginQueryV2)(
    uint32_t requested_query_abi,
    uint32_t output_size,
    MockNavBackendPluginV2 *output);

MOCK_NAV_PLUGIN_EXPORT int mock_nav_backend_query_v1(
    uint32_t requested_query_abi,
    uint32_t output_size,
    MockNavBackendPluginV1 *output);

#ifdef __cplusplus
}
#endif

#endif
