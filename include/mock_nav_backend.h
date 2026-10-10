#ifndef MOCK_NAV_BACKEND_H
#define MOCK_NAV_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define MOCK_NAV_BACKEND_EXPORT __attribute__((visibility("default")))
#else
#define MOCK_NAV_BACKEND_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MOCK_NAV_BACKEND_ABI_V1 = 1u,
    MOCK_NAV_BACKEND_ABI_V2 = 2u,
    MOCK_NAV_BACKEND_ABI_V3 = 3u,
    MOCK_NAV_BACKEND_ABI_V4 = 4u,
    MOCK_NAV_BACKEND_ABI_V5 = 5u,
    MOCK_NAV_BACKEND_POINTER_PRESS = 1u,
    MOCK_NAV_BACKEND_POINTER_RELEASE = 2u,
    MOCK_NAV_BACKEND_POINTER_MOVE = 3u,
    MOCK_NAV_BACKEND_GPS_SATELLITE_COUNT = 16u,
    MOCK_NAV_BACKEND_GPS_SATELLITE_BYTES = 12u
};

enum {
    MOCK_NAV_BACKEND_PAYLOAD_ABI_V1 = 1u,
    MOCK_NAV_BACKEND_SERVICE_OK = 0u,
    MOCK_NAV_BACKEND_SERVICE_UNAVAILABLE = 1u,
    MOCK_NAV_BACKEND_SERVICE_STALE = 0xffff1001u,
    MOCK_NAV_BACKEND_SERVICE_BAD_ABI = 0xffff1002u,
    MOCK_NAV_BACKEND_SERVICE_BAD_VALUE = 0xffff1003u
};

/* All structures are append-only. Consumers must inspect abi_version and
 * struct_size before reading a field. Unknown tail fields must be ignored. */
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t native_pointer_bytes;
    uint32_t reserved0;
    uintptr_t native_screen_window;
    uintptr_t native_egl_display;
    uintptr_t native_egl_surface;
    uintptr_t native_egl_context;
} MockNavBackendRenderTargetV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t sequence;
    uint64_t monotonic_milliseconds;
} MockNavBackendFrameV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t latitude_e7;
    int32_t longitude_e7;
    uint32_t accuracy_raw;
    uint16_t altitude_raw;
    uint16_t ellipsoid_altitude_raw;
    uint16_t heading_raw;
    uint16_t speed_raw_masked;
    uint16_t hdop_raw;
    uint16_t pdop_raw;
    uint16_t vdop_raw;
    uint16_t unknown_100;
    uint8_t fix_type;
    uint8_t satellites_used;
    uint8_t satellites_in_view;
    uint8_t quality_status;
    uint8_t sync_counter;
    uint8_t reserved0[3];
    uint8_t satellites[MOCK_NAV_BACKEND_GPS_SATELLITE_COUNT]
                      [MOCK_NAV_BACKEND_GPS_SATELLITE_BYTES];
    float travel_distance_raw;
    double altitude_change_raw;
    double noise_raw;
} MockNavBackendGpsV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t display;
    uint32_t visibility;
    uint32_t interactivity;
} MockNavBackendVisibilityV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t display;
    uint32_t screen_type;
    uint32_t animation;
} MockNavBackendScreenV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t action;
    int32_t x;
    int32_t y;
    uint32_t buttons;
} MockNavBackendPointerV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    void *runtime_context;
    int (*request_frame)(void *runtime_context);
} MockNavBackendServicesV1;

/* Pointer-bearing outbound payloads are borrowed only until the synchronous
 * service call returns.  Lengths are fixed-width so this in-process ABI is
 * independent of size_t.  All payload structures are append-only. */
typedef struct {
    const char *bytes;
    uint32_t byte_count;
} MockNavBackendUtf8SpanV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint16_t valid_attributes;
    uint8_t route_guidance_state;
    uint8_t navigation_status;
    uint32_t distance_destination;
    uint32_t distance_maneuver_tenths;
    uint32_t lane_active;
    uint8_t distance_destination_unit;
    uint8_t eta_hour;
    uint8_t eta_minute;
    uint8_t rta_hour;
    uint8_t rta_minute;
    uint8_t distance_maneuver_unit;
    uint8_t bar_percent;
    uint8_t compass;
    uint8_t speed_limit;
    uint8_t speed_unit;
    uint8_t legal_speed_limit;
    uint8_t legal_speed_unit;
    uint8_t reserved0[2];
    MockNavBackendUtf8SpanV1 current_road;
    MockNavBackendUtf8SpanV1 next_road;
} MockNavBackendClusterV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t availability_state;
} MockNavBackendAudioAvailabilityV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t thread_id;
    uint32_t thread_status;
    const char *name_ascii;
    uint32_t name_byte_count;
} MockNavBackendThreadStatusV1;

typedef struct {
    MockNavBackendServicesV1 v1;
    uint32_t (*publish_cluster)(
        void *runtime_context,
        const MockNavBackendClusterV1 *cluster);
    uint32_t (*publish_audio_availability)(
        void *runtime_context,
        const MockNavBackendAudioAvailabilityV1 *audio);
    uint32_t (*publish_thread_status)(
        void *runtime_context,
        const MockNavBackendThreadStatusV1 *thread_status);
} MockNavBackendServicesV2;

/* Backend callbacks are serialized on the renderer thread. The renderer has
 * made target's EGL context current while render_frame runs. No provider,
 * lifecycle, renderer, or event-queue mutex is held during any callback.
 * Payloads and target handles are borrowed only for the duration of the call.
 * The values in services may be copied. request_frame/runtime_context remain
 * memory-safe forever, but requests are accepted only from start until stop
 * begins; calls through a stale copied service deterministically fail.
 * A zero return means accepted. Nonzero start aborts renderer creation;
 * nonzero event/render statuses drop that event/frame but keep the loop live.
 * surface_created and render_frame run with the target EGL context current.
 * Before surface_destroyed the runtime tries to make that context current so
 * GL resources can be released. The callback is guaranteed even if this fails;
 * in that case EGL's current-context query returns NO_CONTEXT, which is the v1
 * context-loss signal and GL calls must be skipped. stop always follows after
 * any current context is released. stop and surface_destroyed cannot fail and
 * may be omitted. */
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    void *backend_context;
    int (*start)(void *backend_context,
                 const MockNavBackendServicesV1 *services);
    void (*stop)(void *backend_context);
    int (*surface_created)(
        void *backend_context,
        const MockNavBackendRenderTargetV1 *target);
    void (*surface_destroyed)(void *backend_context);
    int (*visibility_changed)(
        void *backend_context,
        const MockNavBackendVisibilityV1 *visibility);
    int (*screen_changed)(void *backend_context,
                          const MockNavBackendScreenV1 *screen);
    int (*gps_changed)(void *backend_context,
                       const MockNavBackendGpsV1 *gps);
    int (*pointer_changed)(void *backend_context,
                           const MockNavBackendPointerV1 *pointer);
    int (*render_frame)(void *backend_context,
                        const MockNavBackendRenderTargetV1 *target,
                        const MockNavBackendFrameV1 *frame);
} MockNavBackendV1;

enum MockNavBackendVehicleFieldV2 {
    MOCK_NAV_BACKEND_VEHICLE_DAY_NIGHT_STATUS = 1u << 0,
    MOCK_NAV_BACKEND_VEHICLE_DAY_NIGHT_SETTING = 1u << 1,
    MOCK_NAV_BACKEND_VEHICLE_SPEED = 1u << 2
};

/* V2 semantic payloads are owned snapshots delivered on the renderer thread.
 * source_slot and request_handle preserve which recovered provider entry
 * supplied the newest value; request_handle is zero for notification forms.
 * Vehicle valid_fields accumulates across one provider lifecycle, while
 * changed_fields covers all values coalesced into this delivery. */
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t valid_fields;
    uint32_t changed_fields;
    uint32_t source_slot;
    uint32_t request_handle;
    uint32_t displayed_vehicle_speed_bits;
    uint8_t day_night_status;
    uint8_t day_night_setting;
    uint8_t reserved0[2];
} MockNavBackendVehicleV2;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t source_slot;
    uint32_t interval_ms;
} MockNavBackendGpsIntervalV2;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t source_slot;
    uint32_t request_handle;
    uint32_t audio_source;
    uint8_t volume;
    uint8_t reserved0[3];
} MockNavBackendAudioV2;

/* Recovered stock-to-backend settings.  Raw numeric values are preserved:
 * no locale, date-format, time-format, or enable-state enum labels are
 * asserted by this ABI. */
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t source_slot;
    uint32_t request_handle;
    uint16_t language_raw;
    uint8_t date_format_raw;
    uint8_t time_format_raw;
    uint8_t settings_present;
    uint8_t reserved0[3];
} MockNavBackendInternationalizationV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t source_slot;
    uint32_t request_handle;
    uint8_t enabled_raw;
    uint8_t reserved0[3];
} MockNavBackendClusterDisplayV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t source_slot;
    uint32_t request_handle;
    uint32_t distraction_status_raw;
} MockNavBackendDriverDistractionV1;

/* The first sizeof(MockNavBackendV1) bytes are an exact V1 callback prefix.
 * Set v1.abi_version to MOCK_NAV_BACKEND_ABI_V2 and v1.struct_size to the
 * complete V2 size. V1 registration and layout remain unchanged. */
typedef struct {
    MockNavBackendV1 v1;
    int (*vehicle_changed)(void *backend_context,
                           const MockNavBackendVehicleV2 *vehicle);
    int (*gps_interval_changed)(
        void *backend_context,
        const MockNavBackendGpsIntervalV2 *gps_interval);
    int (*audio_changed)(void *backend_context,
                         const MockNavBackendAudioV2 *audio);
} MockNavBackendV2;

/* V3 preserves the complete V2 prefix.  V1/V2 backends continue to receive
 * an exact MockNavBackendServicesV1.  Only a V3 backend is started through
 * start_v2 and receives the outbound service tail. */
typedef struct {
    MockNavBackendV2 v2;
    int (*start_v2)(void *backend_context,
                    const MockNavBackendServicesV2 *services);
} MockNavBackendV3;

/* V4 preserves the complete V3 prefix and adds two recovered input events.
 * Earlier install functions and descriptor layouts remain unchanged. */
typedef struct {
    MockNavBackendV3 v3;
    int (*internationalization_changed)(
        void *backend_context,
        const MockNavBackendInternationalizationV1 *settings);
    int (*cluster_display_changed)(
        void *backend_context,
        const MockNavBackendClusterDisplayV1 *display);
} MockNavBackendV4;

/* V5 preserves the complete V4 prefix.  request_handle is zero for the
 * subscription form recovered at adapter 13 slot 4. */
typedef struct {
    MockNavBackendV4 v4;
    int (*driver_distraction_changed)(
        void *backend_context,
        const MockNavBackendDriverDistractionV1 *distraction);
} MockNavBackendV5;

/* Installs a copied backend descriptor for the next renderer lifecycle.
 * Registration is rejected while a renderer lifecycle exists. Passing NULL
 * restores the built-in magenta backend. */
MOCK_NAV_BACKEND_EXPORT int mock_navicore_backend_install_v1(
    const MockNavBackendV1 *backend);
MOCK_NAV_BACKEND_EXPORT int mock_navicore_backend_install_v2(
    const MockNavBackendV2 *backend);
MOCK_NAV_BACKEND_EXPORT int mock_navicore_backend_install_v3(
    const MockNavBackendV3 *backend);
MOCK_NAV_BACKEND_EXPORT int mock_navicore_backend_install_v4(
    const MockNavBackendV4 *backend);
MOCK_NAV_BACKEND_EXPORT int mock_navicore_backend_install_v5(
    const MockNavBackendV5 *backend);

#ifdef __cplusplus
}
#endif

#endif
