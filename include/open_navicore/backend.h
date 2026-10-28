#ifndef OPEN_NAVI_BACKEND_H
#define OPEN_NAVI_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define OPEN_NAVI_BACKEND_EXPORT __attribute__((visibility("default")))
#else
#define OPEN_NAVI_BACKEND_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OPEN_NAVI_BACKEND_ABI_V1 = 1u,
    OPEN_NAVI_BACKEND_ABI_V2 = 2u,
    OPEN_NAVI_BACKEND_ABI_V3 = 3u,
    OPEN_NAVI_BACKEND_ABI_V4 = 4u,
    OPEN_NAVI_BACKEND_ABI_V5 = 5u,
    OPEN_NAVI_BACKEND_ABI_V6 = 6u,
    OPEN_NAVI_BACKEND_POINTER_PRESS = 1u,
    OPEN_NAVI_BACKEND_POINTER_RELEASE = 2u,
    OPEN_NAVI_BACKEND_POINTER_MOVE = 3u,
    OPEN_NAVI_BACKEND_GPS_SATELLITE_COUNT = 16u,
    OPEN_NAVI_BACKEND_GPS_SATELLITE_BYTES = 12u
};

enum {
    OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1 = 1u,
    OPEN_NAVI_BACKEND_SERVICE_OK = 0u,
    OPEN_NAVI_BACKEND_SERVICE_UNAVAILABLE = 1u,
    OPEN_NAVI_BACKEND_SERVICE_STALE = 0xffff1001u,
    OPEN_NAVI_BACKEND_SERVICE_BAD_ABI = 0xffff1002u,
    OPEN_NAVI_BACKEND_SERVICE_BAD_VALUE = 0xffff1003u
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
} OpenNaviBackendRenderTargetV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t sequence;
    uint64_t monotonic_milliseconds;
} OpenNaviBackendFrameV1;

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
    uint8_t satellites[OPEN_NAVI_BACKEND_GPS_SATELLITE_COUNT]
                      [OPEN_NAVI_BACKEND_GPS_SATELLITE_BYTES];
    float travel_distance_raw;
    double altitude_change_raw;
    double noise_raw;
} OpenNaviBackendGpsV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t display;
    uint32_t visibility;
    uint32_t interactivity;
} OpenNaviBackendVisibilityV1;

/* Visibility with the part of the window the HMI shows: the exact V1
 * fields, then the bounding box of the four points of the native window
 * attribute (SI_HMIIntegration slot 5, NAV +0x4c). Delivered through
 * visibility_changed (abi_version OPEN_NAVI_BACKEND_VISIBILITY_ABI_V2,
 * struct_size the V2 size) only to a plugin that declares
 * OPEN_NAVI_PLUGIN_CAP_WINDOW_AREA; every other backend receives V1.
 * The area is in window pixels (the attribute's display pixels less the
 * window origin); area_width/area_height are 0 when the HMI gave no
 * attribute. Seen on the bench: VISIBLE_SPLIT_SCREEN (19) on the Media
 * screen with display 129,160 to 894,483, i.e. window 129,88 765x323 for
 * NAV_main at 0,72. */
enum {
    OPEN_NAVI_BACKEND_VISIBILITY_ABI_V2 = 2u,
    OPEN_NAVI_BACKEND_HMI_VISIBLE_SPLIT_SCREEN = 19u
};
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t display;
    uint32_t visibility;
    uint32_t interactivity;
    int32_t area_x;
    int32_t area_y;
    uint32_t area_width;
    uint32_t area_height;
} OpenNaviBackendVisibilityV2;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t display;
    uint32_t screen_type;
    uint32_t animation;
} OpenNaviBackendScreenV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t action;
    int32_t x;
    int32_t y;
    uint32_t buttons;
} OpenNaviBackendPointerV1;

/* Multi-touch pointer: the exact V1 fields, then the contact. Delivered
 * through pointer_changed (abi_version OPEN_NAVI_BACKEND_POINTER_ABI_V2,
 * struct_size the V2 size) only to a plugin that declares
 * OPEN_NAVI_PLUGIN_CAP_POINTER_CONTACTS; every other backend receives V1
 * payloads for the first contact only. contact_id is a small slot number
 * (0 = the first finger down, 1 = a second one); contact_count is the
 * number of contacts down after this event. */
enum { OPEN_NAVI_BACKEND_POINTER_ABI_V2 = 2u, OPEN_NAVI_BACKEND_POINTER_CONTACTS = 2u };
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t action;
    int32_t x;
    int32_t y;
    uint32_t buttons;
    uint32_t contact_id;
    uint32_t contact_count;
} OpenNaviBackendPointerV2;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    void *runtime_context;
    int (*request_frame)(void *runtime_context);
} OpenNaviBackendServicesV1;

/* Pointer-bearing outbound payloads are borrowed only until the synchronous
 * service call returns.  Lengths are fixed-width so this in-process ABI is
 * independent of size_t.  All payload structures are append-only. */
typedef struct {
    const char *bytes;
    uint32_t byte_count;
} OpenNaviBackendUtf8SpanV1;

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
    /* Former reserved byte: route-guidance symbol (cluster.h
     * OpenNaviClusterRouteSymbol), read only when valid bit 2 is set; zero
     * otherwise. The layout and size are unchanged. */
    uint8_t route_guidance_symbol;
    uint8_t reserved0[1];
    OpenNaviBackendUtf8SpanV1 current_road;
    OpenNaviBackendUtf8SpanV1 next_road;
} OpenNaviBackendClusterV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t availability_state;
} OpenNaviBackendAudioAvailabilityV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t thread_id;
    uint32_t thread_status;
    const char *name_ascii;
    uint32_t name_byte_count;
} OpenNaviBackendThreadStatusV1;

typedef struct {
    OpenNaviBackendServicesV1 v1;
    uint32_t (*publish_cluster)(
        void *runtime_context,
        const OpenNaviBackendClusterV1 *cluster);
    uint32_t (*publish_audio_availability)(
        void *runtime_context,
        const OpenNaviBackendAudioAvailabilityV1 *audio);
    uint32_t (*publish_thread_status)(
        void *runtime_context,
        const OpenNaviBackendThreadStatusV1 *thread_status);
} OpenNaviBackendServicesV2;

/* Backend callbacks are serialized on the renderer thread. The renderer has
 * made target's EGL context current while render_frame runs. No provider,
 * lifecycle, renderer, or event-queue mutex is held during any callback.
 * Payloads and target handles are borrowed only for the duration of the call.
 * The values in services may be copied. request_frame/runtime_context remain
 * memory-safe forever, but requests are accepted only from start until stop
 * begins; calls through a stale copied service deterministically fail.
 * A zero return means accepted. Nonzero start aborts renderer creation;
 * nonzero event/render statuses drop that event/frame but keep the loop live.
 * The process-lifetime plugin loader adds a stricter policy: a nonzero plugin
 * render status permanently selects its fallback for that lifecycle. Zero
 * causes the runtime to swap buffers; V1 has no distinct pending/no-swap status.
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
                 const OpenNaviBackendServicesV1 *services);
    void (*stop)(void *backend_context);
    int (*surface_created)(
        void *backend_context,
        const OpenNaviBackendRenderTargetV1 *target);
    void (*surface_destroyed)(void *backend_context);
    int (*visibility_changed)(
        void *backend_context,
        const OpenNaviBackendVisibilityV1 *visibility);
    int (*screen_changed)(void *backend_context,
                          const OpenNaviBackendScreenV1 *screen);
    int (*gps_changed)(void *backend_context,
                       const OpenNaviBackendGpsV1 *gps);
    int (*pointer_changed)(void *backend_context,
                           const OpenNaviBackendPointerV1 *pointer);
    int (*render_frame)(void *backend_context,
                        const OpenNaviBackendRenderTargetV1 *target,
                        const OpenNaviBackendFrameV1 *frame);
} OpenNaviBackendV1;

enum OpenNaviBackendVehicleFieldV2 {
    OPEN_NAVI_BACKEND_VEHICLE_DAY_NIGHT_STATUS = 1u << 0,
    OPEN_NAVI_BACKEND_VEHICLE_DAY_NIGHT_SETTING = 1u << 1,
    OPEN_NAVI_BACKEND_VEHICLE_SPEED = 1u << 2
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
    /* Optional tail (original V2 prefix is 32 bytes). Stock VehicleData
     * slot 8 supplies 1=km/h or 2=mph in r2. This is a retained property,
     * not a periodic sample: valid until replaced or the lifecycle resets. */
    uint32_t displayed_vehicle_speed_unit;
} OpenNaviBackendVehicleV2;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t source_slot;
    uint32_t interval_ms;
} OpenNaviBackendGpsIntervalV2;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t source_slot;
    uint32_t request_handle;
    uint32_t audio_source;
    uint8_t volume;
    uint8_t reserved0[3];
} OpenNaviBackendAudioV2;

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
} OpenNaviBackendInternationalizationV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t source_slot;
    uint32_t request_handle;
    uint8_t enabled_raw;
    uint8_t reserved0[3];
} OpenNaviBackendClusterDisplayV1;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t source_slot;
    uint32_t request_handle;
    uint32_t distraction_status_raw;
} OpenNaviBackendDriverDistractionV1;

/* The first sizeof(OpenNaviBackendV1) bytes are an exact V1 callback prefix.
 * Set v1.abi_version to OPEN_NAVI_BACKEND_ABI_V2 and v1.struct_size to the
 * complete V2 size. V1 registration and layout remain unchanged. */
typedef struct {
    OpenNaviBackendV1 v1;
    int (*vehicle_changed)(void *backend_context,
                           const OpenNaviBackendVehicleV2 *vehicle);
    int (*gps_interval_changed)(
        void *backend_context,
        const OpenNaviBackendGpsIntervalV2 *gps_interval);
    int (*audio_changed)(void *backend_context,
                         const OpenNaviBackendAudioV2 *audio);
} OpenNaviBackendV2;

/* V3 preserves the complete V2 prefix.  V1/V2 backends continue to receive
 * an exact OpenNaviBackendServicesV1.  Only a V3 backend is started through
 * start_v2 and receives the outbound service tail. */
typedef struct {
    OpenNaviBackendV2 v2;
    int (*start_v2)(void *backend_context,
                    const OpenNaviBackendServicesV2 *services);
} OpenNaviBackendV3;

/* V4 preserves the complete V3 prefix and adds two recovered input events.
 * Earlier install functions and descriptor layouts remain unchanged. */
typedef struct {
    OpenNaviBackendV3 v3;
    int (*internationalization_changed)(
        void *backend_context,
        const OpenNaviBackendInternationalizationV1 *settings);
    int (*cluster_display_changed)(
        void *backend_context,
        const OpenNaviBackendClusterDisplayV1 *display);
} OpenNaviBackendV4;

/* V5 preserves the complete V4 prefix.  request_handle is zero for the
 * subscription form recovered at adapter 13 slot 4. */
typedef struct {
    OpenNaviBackendV4 v4;
    int (*driver_distraction_changed)(
        void *backend_context,
        const OpenNaviBackendDriverDistractionV1 *distraction);
} OpenNaviBackendV5;

/* Route guidance, backend to provider: guiding is 1 while the backend is in
 * route guidance (from start until the user, arrival or a stop request ends
 * it) and 0 otherwise.  Publish on every change; the provider assumes 0
 * until the first publication and after each lifecycle.  The provider
 * reports it to nv_navigation in the SI_VoiceRecognition route-guidance
 * status replies (adapter 25 slot 19 / callback 23). */
enum {
    OPEN_NAVI_BACKEND_GUIDANCE_IDLE = 0u,
    OPEN_NAVI_BACKEND_GUIDANCE_ACTIVE = 1u
};
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t guiding;
} OpenNaviBackendGuidanceStateV1;

/* Provider to backend: end route guidance now, as the user's Stop does.
 * Sent when nv_navigation asks the native navigation to stop guidance
 * (SI_VoiceRecognition requestGuidanceStop, adapter 25 slot 13), e.g. when
 * a phone projection takes navigation focus.  The backend publishes the
 * resulting guidance state; a request outside guidance is a no-op. */
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t source_slot;
    uint32_t request_handle;
} OpenNaviBackendGuidanceStopV1;

/* Outbound services for a V6 backend: the exact V2 services (v2.v1
 * abi_version is OPEN_NAVI_BACKEND_ABI_V3, struct_size the V3 size), then
 * the guidance state.  Same borrowing and lifetime rules as V2. */
typedef struct {
    OpenNaviBackendServicesV2 v2;
    uint32_t (*publish_guidance_state)(
        void *runtime_context,
        const OpenNaviBackendGuidanceStateV1 *guidance);
} OpenNaviBackendServicesV3;

/* V6 preserves the complete V5 prefix.  A V6 backend is started through
 * start_v3 (start_v2 is not called) and receives guidance stop requests. */
typedef struct {
    OpenNaviBackendV5 v5;
    int (*start_v3)(void *backend_context,
                    const OpenNaviBackendServicesV3 *services);
    int (*guidance_stop_requested)(
        void *backend_context,
        const OpenNaviBackendGuidanceStopV1 *request);
} OpenNaviBackendV6;

/* Installs a copied backend descriptor for the next renderer lifecycle.
 * Registration is rejected while a renderer lifecycle exists. Passing NULL
 * restores the built-in magenta backend. */
OPEN_NAVI_BACKEND_EXPORT int open_navi_backend_install_v1(
    const OpenNaviBackendV1 *backend);
OPEN_NAVI_BACKEND_EXPORT int open_navi_backend_install_v2(
    const OpenNaviBackendV2 *backend);
OPEN_NAVI_BACKEND_EXPORT int open_navi_backend_install_v3(
    const OpenNaviBackendV3 *backend);
OPEN_NAVI_BACKEND_EXPORT int open_navi_backend_install_v4(
    const OpenNaviBackendV4 *backend);
OPEN_NAVI_BACKEND_EXPORT int open_navi_backend_install_v5(
    const OpenNaviBackendV5 *backend);
OPEN_NAVI_BACKEND_EXPORT int open_navi_backend_install_v6(
    const OpenNaviBackendV6 *backend);

#ifdef __cplusplus
}
#endif

#endif
