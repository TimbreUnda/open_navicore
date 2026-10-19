#define _POSIX_C_SOURCE 200112L

#include "open_navi.h"
#include "open_navi_api_telemetry.h"
#include "open_navi_callback_backend.h"
#include "open_navi_backend_loader.h"
#include <open_navicore/cluster.h>
#include "host_callbacks.h"
#include "open_navi_gps_backend.h"
#include "open_navi_pointer_backend.h"
#include "open_navi_renderer.h"
#include "open_navi_scalar_backend.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#define PANA_API_MAJOR 24u
#define PANA_API_MINOR 7u
#define OPEN_NAVI_ADAPTER_COUNT 256u
#define OPEN_NAVI_DUMMY_VTABLE_SLOTS 128u
#define OPEN_NAVI_HMI_ADAPTER 1u
#define OPEN_NAVI_VEHICLE_ADAPTER 8u
#define OPEN_NAVI_GPS_ADAPTER 9u
#define OPEN_NAVI_AUDIO_ADAPTER 10u
#define OPEN_NAVI_UNAVAILABLE_ADAPTER_21 21u
#define OPEN_NAVI_UNAVAILABLE_ADAPTER_27 27u
#define OPEN_NAVI_UNAVAILABLE_ADAPTER_28 28u
#define OPEN_NAVI_TRANSITION_QUEUE_CAPACITY 8u
#define OPEN_NAVI_HMI_EVENT_QUEUE_CAPACITY 16u
#ifndef OPEN_NAVI_TRACE_PATH
#define OPEN_NAVI_TRACE_PATH "/tmp/open-navi.log"
#endif
#if defined(__QNXNTO__)
/*
 * The QR39 consumer starts five proxy-worker singletons in parallel with the
 * provider.  After panaGetSoftwareInfrastructure() it waits only 200 ms for
 * those singletons before abandoning the provider for the lifetime of the
 * process.  The stock provider's initialization naturally covers that race;
 * this lightweight replacement must preserve the same timing boundary.
 */
#define OPEN_NAVI_QR39_CONSUMER_SETTLE_MS 500u
#else
#define OPEN_NAVI_QR39_CONSUMER_SETTLE_MS 0u
#endif

typedef struct {
    const PanaGenericVirtual *vptr;
    void *host_proxy;
    uint32_t adapter_id;
} OpenNaviInterfaceObject;

enum open_navi_state {
    OPEN_NAVI_COLD = 0,
    OPEN_NAVI_STARTING = 1,
    OPEN_NAVI_ACTIVE = 2,
    OPEN_NAVI_STOPPING = 3
};

typedef struct {
    uint64_t epoch;
    int held;
} OpenNaviLifecycleLease;

static uint32_t open_navi_generic_virtual(void *self, ...);
static uint32_t nc_provider_get_adapter(void *self,
                                 uint32_t adapter,
                                 void **interface_out);
static uint32_t open_navi_signal_start(void *self);
static uint32_t open_navi_signal_start_watchdog(void *self, uint16_t interval);
static uint32_t open_navi_hmi_state_transition(void *self,
                                          uint32_t handle,
                                          uint32_t display,
                                          uint32_t visibility,
                                          uint32_t interactivity,
                                          const void *window_attribute);
static uint32_t open_navi_hmi_screen_change(void *self,
                                       uint32_t handle,
                                       uint32_t display,
                                       uint32_t screen_type,
                                       uint32_t animation,
                                       uint32_t reserved);
static uint32_t open_navi_hmi_screen_theme_change(void *self,
                                             uint32_t handle,
                                             const void *theme_name);
static uint32_t nc_provider_gps_data(void *self, const void *gps_record);
static uint32_t open_navi_vehicle_day_night_status(void *self, uint8_t value);
static uint32_t open_navi_vehicle_day_night_setting(void *self, uint8_t value);
static uint32_t open_navi_vehicle_speed(void *self, float value);
static uint32_t open_navi_gps_interval(void *self, uint32_t interval_ms);
static uint32_t open_navi_audio_volume(void *self,
                                  uint32_t audio_source,
                                  uint8_t volume);
static uint32_t nc_provider_vehicle_day_night_status_reply(void *, uint32_t, uint8_t);
static uint32_t nc_provider_vehicle_day_night_setting_reply(void *, uint32_t, uint8_t);
static uint32_t open_navi_10_17(void *, uint32_t, uint32_t, uint8_t);
static uint32_t open_navi_12_5(void *, uint32_t);
static uint32_t open_navi_12_6(void *, uint32_t);
static uint32_t open_navi_12_7(void *, uint32_t, uint32_t, uint32_t);
static uint32_t open_navi_13_3(void *, uint32_t, uint32_t);
static uint32_t open_navi_13_4(void *, uint32_t);
static uint32_t open_navi_13_12(void *, uint32_t);
static uint32_t open_navi_13_13(void *, uint32_t);
static uint32_t open_navi_13_14(void *, uint32_t);
static uint32_t open_navi_17_6(void *, uint32_t, uint8_t);
static uint32_t open_navi_20_3(void *, uint32_t, uint32_t, uint8_t, uint32_t);
static uint32_t open_navi_20_4(void *, uint32_t, uint8_t, uint32_t);
static uint32_t open_navi_20_7(void *, uint32_t, uint32_t, uint8_t);
static uint32_t open_navi_20_8(void *, uint32_t, uint32_t, uint8_t);
static uint32_t open_navi_25_13(void *, uintptr_t);
static uint32_t open_navi_25_19(void *, uint32_t);
static uint32_t open_navi_26_5(void *, uint32_t, uint8_t);
static uint32_t open_navi_29_3(void *, uint32_t);
static uint32_t open_navi_29_4(void *);
static uint32_t open_navi_30_3(void *, uint32_t);
static uint32_t open_navi_30_4(void *, uint32_t);
static uint32_t open_navi_31_3(void *, uint8_t);
static uint32_t open_navi_31_5(void *, uint8_t);
static uint32_t nc_provider_logging_config(void *, uint32_t, uint32_t,
                         const uint8_t *, const uint32_t *);
static uint32_t nc_provider_i18n_settings_reply(void *, uint32_t, const OpenNaviIntlSettingsInput *);
static uint32_t open_navi_8_12(void *, uint32_t, const char *);
static uint32_t open_navi_19_3(void *, uint32_t, uint32_t,
                          const OpenNaviTransportMeansObservedInput *);
static uint32_t open_navi_20_6(void *, uint32_t, uint32_t,
                          const OpenNaviConnectionDeviceInfoInput *);
static uint32_t open_navi_25_21(void *, uint32_t, uint32_t, const char *);
static void open_navi_finish_deinitialize(void);
static void open_navi_lifecycle_release(OpenNaviLifecycleLease *lease);

/* Statically reachable but not yet semantically interpreted provider calls
 * use exact AAPCS word counts.  uintptr_t is one core-register word on the
 * target ARM ABI and safely represents either an opaque scalar or pointer;
 * these stubs never dereference an unknown aggregate. */
#define DECLARE_STATIC_0(name) static uint32_t name(void *)
#define DECLARE_STATIC_1(name) static uint32_t name(void *, uintptr_t)
#define DECLARE_STATIC_2(name) static uint32_t name(void *, uintptr_t, uintptr_t)
#define DECLARE_STATIC_3(name) \
    static uint32_t name(void *, uintptr_t, uintptr_t, uintptr_t)
#define DECLARE_STATIC_4(name) \
    static uint32_t name(void *, uintptr_t, uintptr_t, uintptr_t, uintptr_t)
#define DECLARE_STATIC_5(name) \
    static uint32_t name(void *, uintptr_t, uintptr_t, uintptr_t, uintptr_t, \
                         uintptr_t)
#define DECLARE_STATIC_6(name) \
    static uint32_t name(void *, uintptr_t, uintptr_t, uintptr_t, uintptr_t, \
                         uintptr_t, uintptr_t)
#define DECLARE_STATIC_7(name) \
    static uint32_t name(void *, uintptr_t, uintptr_t, uintptr_t, uintptr_t, \
                         uintptr_t, uintptr_t, uintptr_t)
#define OPEN_NAVI_STATIC_SLOT(adapter, slot, words) \
    DECLARE_STATIC_##words(open_navi_static_##adapter##_##slot);
#include "provider_slots.def"
#undef OPEN_NAVI_STATIC_SLOT
#undef DECLARE_STATIC_0
#undef DECLARE_STATIC_1
#undef DECLARE_STATIC_2
#undef DECLARE_STATIC_3
#undef DECLARE_STATIC_4
#undef DECLARE_STATIC_5
#undef DECLARE_STATIC_6
#undef DECLARE_STATIC_7

/* Keep the recovered provider ABI catalog tied to compiler-checked function
 * types. tests/check_provider_coverage.py cross-checks these rows against the
 * authoritative manifest and the actual vtable entries below. */
typedef uint32_t (*OpenNaviProviderSelf)(void *);
typedef uint32_t (*OpenNaviProviderSelfU8)(void *, uint8_t);
typedef uint32_t (*OpenNaviProviderSelfU32)(void *, uint32_t);
typedef uint32_t (*OpenNaviProviderSelfFloat)(void *, float);
typedef uint32_t (*OpenNaviProviderSelfConstVoid)(void *, const void *);
typedef uint32_t (*OpenNaviProviderSelfU32U8)(void *, uint32_t, uint8_t);
typedef uint32_t (*OpenNaviProviderSelfU32U32)(void *, uint32_t, uint32_t);
typedef uint32_t (*OpenNaviProviderSelfU32U8U32)(void *, uint32_t, uint8_t,
                                             uint32_t);
typedef uint32_t (*OpenNaviProviderSelfU32U32U8)(void *, uint32_t, uint32_t,
                                             uint8_t);
typedef uint32_t (*OpenNaviProviderSelfU32U32U32)(void *, uint32_t, uint32_t,
                                              uint32_t);
typedef uint32_t (*OpenNaviProviderSelfU32U32U8U32)(void *, uint32_t, uint32_t,
                                                uint8_t, uint32_t);
typedef uint32_t (*OpenNaviProviderSelf5U32)(void *, uint32_t, uint32_t, uint32_t,
                                         uint32_t, uint32_t);
typedef uint32_t (*OpenNaviProviderLogging)(void *, uint32_t, uint32_t,
                                        const uint8_t *, const uint32_t *);
typedef uint32_t (*OpenNaviProviderIntl)(void *, uint32_t,
                                     const OpenNaviIntlSettingsInput *);
typedef uint32_t (*OpenNaviProviderU32ConstChar)(void *, uint32_t, const char *);
typedef uint32_t (*OpenNaviProviderTransport)(
    void *, uint32_t, uint32_t, const OpenNaviTransportMeansObservedInput *);
typedef uint32_t (*OpenNaviProviderConnection)(
    void *, uint32_t, uint32_t, const OpenNaviConnectionDeviceInfoInput *);
typedef uint32_t (*OpenNaviProviderTef)(void *, uint32_t, uint32_t, const char *);
typedef uint32_t (*OpenNaviProviderWord0)(void *);
typedef uint32_t (*OpenNaviProviderWord1)(void *, uintptr_t);
typedef uint32_t (*OpenNaviProviderWord2)(void *, uintptr_t, uintptr_t);
typedef uint32_t (*OpenNaviProviderWord3)(void *, uintptr_t, uintptr_t, uintptr_t);
typedef uint32_t (*OpenNaviProviderWord4)(void *, uintptr_t, uintptr_t, uintptr_t,
                                      uintptr_t);
typedef uint32_t (*OpenNaviProviderWord5)(void *, uintptr_t, uintptr_t, uintptr_t,
                                      uintptr_t, uintptr_t);
typedef uint32_t (*OpenNaviProviderWord6)(void *, uintptr_t, uintptr_t, uintptr_t,
                                      uintptr_t, uintptr_t, uintptr_t);
typedef uint32_t (*OpenNaviProviderWord7)(void *, uintptr_t, uintptr_t, uintptr_t,
                                      uintptr_t, uintptr_t, uintptr_t,
                                      uintptr_t);

#define OPEN_NAVI_PROVIDER_TYPECHECK(adapter, slot, symbol, type)                  \
    typedef char open_navi_provider_typecheck_##adapter##_##slot[                 \
        __builtin_types_compatible_p(__typeof__(&(symbol)), type) ? 1 : -1]

OPEN_NAVI_PROVIDER_TYPECHECK(1, 9, open_navi_hmi_screen_change, OpenNaviProviderSelf5U32);
OPEN_NAVI_PROVIDER_TYPECHECK(6, 3, nc_provider_logging_config, OpenNaviProviderLogging);
OPEN_NAVI_PROVIDER_TYPECHECK(7, 3, nc_provider_i18n_settings_reply, OpenNaviProviderIntl);
OPEN_NAVI_PROVIDER_TYPECHECK(8, 3, nc_provider_vehicle_day_night_status_reply, OpenNaviProviderSelfU32U8);
OPEN_NAVI_PROVIDER_TYPECHECK(8, 4, open_navi_vehicle_day_night_status,
                        OpenNaviProviderSelfU8);
OPEN_NAVI_PROVIDER_TYPECHECK(8, 5, nc_provider_vehicle_day_night_setting_reply, OpenNaviProviderSelfU32U8);
OPEN_NAVI_PROVIDER_TYPECHECK(8, 6, open_navi_vehicle_day_night_setting,
                        OpenNaviProviderSelfU8);
OPEN_NAVI_PROVIDER_TYPECHECK(8, 8, open_navi_vehicle_speed, OpenNaviProviderSelfFloat);
OPEN_NAVI_PROVIDER_TYPECHECK(8, 12, open_navi_8_12, OpenNaviProviderU32ConstChar);
OPEN_NAVI_PROVIDER_TYPECHECK(9, 4, nc_provider_gps_data, OpenNaviProviderSelfConstVoid);
OPEN_NAVI_PROVIDER_TYPECHECK(9, 6, open_navi_gps_interval, OpenNaviProviderSelfU32);
OPEN_NAVI_PROVIDER_TYPECHECK(10, 17, open_navi_10_17, OpenNaviProviderSelfU32U32U8);
OPEN_NAVI_PROVIDER_TYPECHECK(10, 19, open_navi_audio_volume, OpenNaviProviderSelfU32U8);
OPEN_NAVI_PROVIDER_TYPECHECK(12, 5, open_navi_12_5, OpenNaviProviderSelfU32);
OPEN_NAVI_PROVIDER_TYPECHECK(12, 6, open_navi_12_6, OpenNaviProviderSelfU32);
OPEN_NAVI_PROVIDER_TYPECHECK(12, 7, open_navi_12_7, OpenNaviProviderSelfU32U32U32);
OPEN_NAVI_PROVIDER_TYPECHECK(13, 3, open_navi_13_3, OpenNaviProviderSelfU32U32);
OPEN_NAVI_PROVIDER_TYPECHECK(13, 4, open_navi_13_4, OpenNaviProviderSelfU32);
OPEN_NAVI_PROVIDER_TYPECHECK(13, 12, open_navi_13_12, OpenNaviProviderSelfU32);
OPEN_NAVI_PROVIDER_TYPECHECK(13, 13, open_navi_13_13, OpenNaviProviderSelfU32);
OPEN_NAVI_PROVIDER_TYPECHECK(13, 14, open_navi_13_14, OpenNaviProviderSelfU32);
OPEN_NAVI_PROVIDER_TYPECHECK(17, 6, open_navi_17_6, OpenNaviProviderSelfU32U8);
OPEN_NAVI_PROVIDER_TYPECHECK(19, 3, open_navi_19_3, OpenNaviProviderTransport);
OPEN_NAVI_PROVIDER_TYPECHECK(20, 3, open_navi_20_3, OpenNaviProviderSelfU32U32U8U32);
OPEN_NAVI_PROVIDER_TYPECHECK(20, 4, open_navi_20_4, OpenNaviProviderSelfU32U8U32);
OPEN_NAVI_PROVIDER_TYPECHECK(20, 6, open_navi_20_6, OpenNaviProviderConnection);
OPEN_NAVI_PROVIDER_TYPECHECK(20, 7, open_navi_20_7, OpenNaviProviderSelfU32U32U8);
OPEN_NAVI_PROVIDER_TYPECHECK(20, 8, open_navi_20_8, OpenNaviProviderSelfU32U32U8);
OPEN_NAVI_PROVIDER_TYPECHECK(25, 19, open_navi_25_19, OpenNaviProviderSelfU32);
OPEN_NAVI_PROVIDER_TYPECHECK(25, 21, open_navi_25_21, OpenNaviProviderTef);
OPEN_NAVI_PROVIDER_TYPECHECK(26, 5, open_navi_26_5, OpenNaviProviderSelfU32U8);
OPEN_NAVI_PROVIDER_TYPECHECK(29, 3, open_navi_29_3, OpenNaviProviderSelfU32);
OPEN_NAVI_PROVIDER_TYPECHECK(29, 4, open_navi_29_4, OpenNaviProviderSelf);
OPEN_NAVI_PROVIDER_TYPECHECK(30, 3, open_navi_30_3, OpenNaviProviderSelfU32);
OPEN_NAVI_PROVIDER_TYPECHECK(30, 4, open_navi_30_4, OpenNaviProviderSelfU32);
OPEN_NAVI_PROVIDER_TYPECHECK(31, 3, open_navi_31_3, OpenNaviProviderSelfU8);
OPEN_NAVI_PROVIDER_TYPECHECK(31, 5, open_navi_31_5, OpenNaviProviderSelfU8);

#undef OPEN_NAVI_PROVIDER_TYPECHECK

#define OPEN_NAVI_STATIC_TYPECHECK(adapter, slot, words)                           \
    typedef char open_navi_static_typecheck_##adapter##_##slot[                   \
        __builtin_types_compatible_p(                                        \
            __typeof__(&open_navi_static_##adapter##_##slot),                     \
            OpenNaviProviderWord##words) ? 1 : -1]
#define OPEN_NAVI_STATIC_SLOT(adapter, slot, words) \
    OPEN_NAVI_STATIC_TYPECHECK(adapter, slot, words);
#include "provider_slots.def"
#undef OPEN_NAVI_STATIC_SLOT
#undef OPEN_NAVI_STATIC_TYPECHECK
/* requestGuidanceStop keeps the exact one-word static ABI it had. */
typedef char open_navi_guidance_stop_typecheck_25_13[
    __builtin_types_compatible_p(__typeof__(&open_navi_25_13),
                                 OpenNaviProviderWord1) ? 1 : -1];

#define V1  open_navi_generic_virtual
#define V2  V1, V1
#define V4  V2, V2
#define V8  V4, V4
#define V16 V8, V8
#define V32 V16, V16
#define V64 V32, V32

static const PanaGenericVirtual g_dummy_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V64, V64
};

/* Adapter 1 is HMIIntegration. Slot 5 receives the stock HMI's requested
 * state/window transition, including the first render-start boundary. */
static const PanaGenericVirtual g_hmi_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_static_1_3,
    V1,
    (PanaGenericVirtual)open_navi_hmi_state_transition,
    (PanaGenericVirtual)open_navi_static_1_6,
    (PanaGenericVirtual)open_navi_static_1_7,
    (PanaGenericVirtual)open_navi_static_1_8,
    (PanaGenericVirtual)open_navi_hmi_screen_change,
    (PanaGenericVirtual)open_navi_hmi_screen_theme_change,
    V1,
    (PanaGenericVirtual)open_navi_static_1_12,
    V64, V32, V16, V2, V1
};

static const PanaGenericVirtual g_hardkey_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_static_4_3,
    (PanaGenericVirtual)open_navi_static_4_4,
    V64, V32, V16, V8, V2, V1
};

static const PanaGenericVirtual g_logging_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)nc_provider_logging_config,
    V64, V32, V16, V8, V4
};

static const PanaGenericVirtual g_internationalization_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)nc_provider_i18n_settings_reply,
    (PanaGenericVirtual)open_navi_static_7_4,
    (PanaGenericVirtual)open_navi_static_7_5,
    V64, V32, V16, V8, V2
};

static const PanaGenericVirtual g_vehicle_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)nc_provider_vehicle_day_night_status_reply,
    (PanaGenericVirtual)open_navi_vehicle_day_night_status,
    (PanaGenericVirtual)nc_provider_vehicle_day_night_setting_reply,
    (PanaGenericVirtual)open_navi_vehicle_day_night_setting,
    (PanaGenericVirtual)open_navi_static_8_7,
    (PanaGenericVirtual)open_navi_vehicle_speed,
    (PanaGenericVirtual)open_navi_static_8_9,
    V2,
    (PanaGenericVirtual)open_navi_8_12,
    (PanaGenericVirtual)open_navi_static_8_13,
    (PanaGenericVirtual)open_navi_static_8_14,
    V64, V32, V16, V1
};

/* Adapter 9 is SI_GPSData. Stock offset 0x2d7440 is provider slot 4 and
 * receives exactly self plus an opaque GPS record pointer. */
static const PanaGenericVirtual g_gps_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_static_9_3,
    (PanaGenericVirtual)nc_provider_gps_data,
    (PanaGenericVirtual)open_navi_static_9_5,
    (PanaGenericVirtual)open_navi_gps_interval,
    V64, V32, V16, V8, V1
};

static const PanaGenericVirtual g_audio_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_static_10_3,
    V1,
    (PanaGenericVirtual)open_navi_static_10_5,
    V1,
    (PanaGenericVirtual)open_navi_static_10_7,
    (PanaGenericVirtual)open_navi_static_10_8,
    V8,
    (PanaGenericVirtual)open_navi_10_17,
    (PanaGenericVirtual)open_navi_static_10_18,
    (PanaGenericVirtual)open_navi_audio_volume,
    V64, V32, V8, V4
};

static const PanaGenericVirtual g_diagnosis_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_static_12_3,
    (PanaGenericVirtual)open_navi_static_12_4,
    (PanaGenericVirtual)open_navi_12_5,
    (PanaGenericVirtual)open_navi_12_6,
    (PanaGenericVirtual)open_navi_12_7,
    V64, V32, V16, V8
};

static const PanaGenericVirtual g_diagnosis_navigation_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_13_3,
    (PanaGenericVirtual)open_navi_13_4,
    (PanaGenericVirtual)open_navi_static_13_5,
    (PanaGenericVirtual)open_navi_static_13_6,
    (PanaGenericVirtual)open_navi_static_13_7,
    (PanaGenericVirtual)open_navi_static_13_8,
    (PanaGenericVirtual)open_navi_static_13_9,
    (PanaGenericVirtual)open_navi_static_13_10,
    (PanaGenericVirtual)open_navi_static_13_11,
    (PanaGenericVirtual)open_navi_13_12,
    (PanaGenericVirtual)open_navi_13_13,
    (PanaGenericVirtual)open_navi_13_14,
    V64, V32, V16, V1
};

static const PanaGenericVirtual g_rds_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_static_14_3,
    (PanaGenericVirtual)open_navi_static_14_4,
    (PanaGenericVirtual)open_navi_static_14_5,
    (PanaGenericVirtual)open_navi_static_14_6,
    (PanaGenericVirtual)open_navi_static_14_7,
    (PanaGenericVirtual)open_navi_static_14_8,
    (PanaGenericVirtual)open_navi_static_14_9,
    V64, V32, V16, V4, V2
};

static const PanaGenericVirtual g_cluster_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1, V1,
    (PanaGenericVirtual)open_navi_static_17_4,
    V1,
    (PanaGenericVirtual)open_navi_17_6,
    V64, V32, V16, V8, V1
};

static const PanaGenericVirtual g_data_exchange_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_19_3,
    (PanaGenericVirtual)open_navi_static_19_4,
    (PanaGenericVirtual)open_navi_static_19_5,
    (PanaGenericVirtual)open_navi_static_19_6,
    (PanaGenericVirtual)open_navi_static_19_7,
    (PanaGenericVirtual)open_navi_static_19_8,
    (PanaGenericVirtual)open_navi_static_19_9,
    V64, V32, V16, V4, V2
};

static const PanaGenericVirtual g_connectivity_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_20_3,
    (PanaGenericVirtual)open_navi_20_4,
    (PanaGenericVirtual)open_navi_static_20_5,
    (PanaGenericVirtual)open_navi_20_6,
    (PanaGenericVirtual)open_navi_20_7,
    (PanaGenericVirtual)open_navi_20_8,
    V64, V32, V16, V4, V2, V1
};

static const PanaGenericVirtual g_rotary_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_static_23_3,
    V64, V32, V16, V8, V4
};

static const PanaGenericVirtual g_tts_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_static_24_3,
    V64, V32, V16, V8, V4
};

static const PanaGenericVirtual g_vr_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_static_25_3,
    (PanaGenericVirtual)open_navi_static_25_4,
    (PanaGenericVirtual)open_navi_static_25_5,
    V1,
    (PanaGenericVirtual)open_navi_static_25_7,
    (PanaGenericVirtual)open_navi_static_25_8,
    (PanaGenericVirtual)open_navi_static_25_9,
    (PanaGenericVirtual)open_navi_static_25_10,
    (PanaGenericVirtual)open_navi_static_25_11,
    (PanaGenericVirtual)open_navi_static_25_12,
    (PanaGenericVirtual)open_navi_25_13,
    (PanaGenericVirtual)open_navi_static_25_14,
    (PanaGenericVirtual)open_navi_static_25_15,
    (PanaGenericVirtual)open_navi_static_25_16,
    (PanaGenericVirtual)open_navi_static_25_17,
    (PanaGenericVirtual)open_navi_static_25_18,
    (PanaGenericVirtual)open_navi_25_19,
    (PanaGenericVirtual)open_navi_static_25_20,
    (PanaGenericVirtual)open_navi_25_21,
    (PanaGenericVirtual)open_navi_static_25_22,
    (PanaGenericVirtual)open_navi_static_25_23,
    (PanaGenericVirtual)open_navi_static_25_24,
    V1,
    (PanaGenericVirtual)open_navi_static_25_26,
    (PanaGenericVirtual)open_navi_static_25_27,
    (PanaGenericVirtual)open_navi_static_25_28,
    (PanaGenericVirtual)open_navi_static_25_29,
    (PanaGenericVirtual)open_navi_static_25_30,
    V64, V32, V1
};

static const PanaGenericVirtual g_phone_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_static_26_3,
    (PanaGenericVirtual)open_navi_static_26_4,
    (PanaGenericVirtual)open_navi_26_5,
    V64, V32, V16, V8, V2
};

static const PanaGenericVirtual g_parking_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_29_3,
    (PanaGenericVirtual)open_navi_29_4,
    (PanaGenericVirtual)open_navi_static_29_5,
    (PanaGenericVirtual)open_navi_static_29_6,
    V64, V32, V16, V8, V1
};

static const PanaGenericVirtual g_apogee_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_30_3,
    (PanaGenericVirtual)open_navi_30_4,
    (PanaGenericVirtual)open_navi_static_30_5,
    (PanaGenericVirtual)open_navi_static_30_6,
    (PanaGenericVirtual)open_navi_static_30_7,
    (PanaGenericVirtual)open_navi_static_30_8,
    (PanaGenericVirtual)open_navi_static_30_9,
    (PanaGenericVirtual)open_navi_static_30_10,
    (PanaGenericVirtual)open_navi_static_30_11,
    V64, V32, V16, V4
};

static const PanaGenericVirtual g_agw_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_31_3,
    (PanaGenericVirtual)open_navi_static_31_4,
    (PanaGenericVirtual)open_navi_31_5,
    (PanaGenericVirtual)open_navi_static_31_6,
    (PanaGenericVirtual)open_navi_static_31_7,
    (PanaGenericVirtual)open_navi_static_31_8,
    (PanaGenericVirtual)open_navi_static_31_9,
    (PanaGenericVirtual)open_navi_static_31_10,
    (PanaGenericVirtual)open_navi_static_31_11,
    (PanaGenericVirtual)open_navi_static_31_12,
    (PanaGenericVirtual)open_navi_static_31_13,
    (PanaGenericVirtual)open_navi_static_31_14,
    (PanaGenericVirtual)open_navi_static_31_15,
    (PanaGenericVirtual)open_navi_static_31_16,
    V64, V32, V8, V4, V2, V1
};

static const PanaGenericVirtual g_tpeg_ews_vtable[OPEN_NAVI_DUMMY_VTABLE_SLOTS] = {
    V2, V1,
    (PanaGenericVirtual)open_navi_static_32_3,
    V64, V32, V16, V8, V4
};

static const PanaRootVtable g_root_vtable = {
    {
        open_navi_generic_virtual,
        open_navi_generic_virtual,
        open_navi_generic_virtual,
        (PanaGenericVirtual)nc_provider_get_adapter,
        (PanaGenericVirtual)open_navi_signal_start,
        (PanaGenericVirtual)open_navi_signal_start_watchdog,
        (PanaGenericVirtual)open_navi_static_root_6,
        (PanaGenericVirtual)open_navi_static_root_7,
        (PanaGenericVirtual)open_navi_static_root_8,
        V1,
        (PanaGenericVirtual)open_navi_static_root_10,
        V16, V4, V1
    }
};

static PanaRootObject g_root = { &g_root_vtable, NULL };
static OpenNaviInterfaceObject g_adapters[OPEN_NAVI_ADAPTER_COUNT];
static OpenNaviInterfaceObject g_overflow_adapter;
static volatile int g_objects_ready = 0;
static int g_state = OPEN_NAVI_COLD;
static pthread_mutex_t g_worker_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_lifecycle_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_lifecycle_condition = PTHREAD_COND_INITIALIZER;
static uint32_t g_lifecycle_inflight;
static uint64_t g_lifecycle_epoch;
static int g_deinitialize_deferred;
static int g_deinitialize_finishing;
static __thread uint32_t g_lifecycle_depth;
static __thread uint64_t g_lifecycle_thread_epoch;
static __thread int g_readiness_worker_thread;
static pthread_cond_t g_worker_condition = PTHREAD_COND_INITIALIZER;
static pthread_t g_worker;
static int g_worker_created = 0;
static int g_worker_running = 0;
static uint64_t g_worker_epoch = 0u;
static int g_worker_stop = 0;
static int g_si_ready_sent = 0;
static int g_hmi_ready_sent = 0;
typedef struct {
    uint32_t handle;
    uint32_t display;
    uint32_t visibility;
    uint32_t interactivity;
} OpenNaviTransitionRequest;

typedef enum {
    OPEN_NAVI_HMI_EVENT_TRANSITION = 1,
    OPEN_NAVI_HMI_EVENT_SCREEN_CHANGE = 2,
    OPEN_NAVI_HMI_EVENT_THEME_CHANGE = 3,
    /* SI_VoiceRecognition requestGuidanceStop: replied (adapter 25 callback
     * 16) from the same FIFO so it stays asynchronous and ordered. */
    OPEN_NAVI_HMI_EVENT_GUIDANCE_STOP = 4
} OpenNaviHMIEventType;

typedef struct {
    OpenNaviHMIEventType type;
    union {
        OpenNaviTransitionRequest transition;
        struct {
            uint32_t handle;
            uint32_t display;
            uint32_t screen_type;
            uint32_t animation;
            uint32_t reserved;
        } screen_change;
        struct {
            uint32_t handle;
        } theme_change;
        struct {
            uint32_t handle;
        } guidance_stop;
    } request;
} OpenNaviHMIEvent;

static OpenNaviHMIEvent g_hmi_event_queue[OPEN_NAVI_HMI_EVENT_QUEUE_CAPACITY];
static unsigned int g_hmi_event_queue_head = 0u;
static unsigned int g_hmi_event_queue_count = 0u;
static unsigned int g_transition_queue_count = 0u;
static uint32_t g_transition_reply_count = 0u;
static uint32_t g_gps_call_count = 0u;
static uintptr_t g_gps_last_record_address = (uintptr_t)0u;
static const uint8_t g_pre_hmi_ready_adapters[] = {
    8u, 12u, 24u, 6u, 10u, 13u, 20u, 29u,
    7u, 26u, 31u, 14u, 4u, 19u, 23u, 17u
};
static const uint8_t g_post_hmi_ready_adapters[] = {
    9u, 25u, 32u, 30u
};
/* The captured stock startup allocates 0x14, 0x15 and 0x16 for displays
 * 0, 1 and 2 respectively.  Keep subsequent allocations monotonic. */
static const char g_version_text[] = "open-navi/adapter-bootstrap";

typedef char server_version_must_be_24_bytes[
    sizeof(PanaServerVersionHeadUnit) == 24u ? 1 : -1];

#if UINTPTR_MAX == 0xffffffffu
typedef char client_version_must_be_16_bytes[
    sizeof(PanaClientVersionNavigationSoftwareModule) == 16u ? 1 : -1];
typedef char initialization_data_must_be_44_bytes[
    sizeof(PanaInitializationData) == 44u ? 1 : -1];
typedef char root_slot_3_must_be_at_offset_12[
    offsetof(PanaRootVtable, slot[3]) == 12u ? 1 : -1];
typedef char root_host_proxy_must_be_at_offset_4[
    offsetof(PanaRootObject, host_proxy) == 4u ? 1 : -1];
typedef char adapter_host_proxy_must_be_at_offset_4[
    offsetof(OpenNaviInterfaceObject, host_proxy) == 4u ? 1 : -1];
typedef char adapter_id_must_be_at_offset_8[
    offsetof(OpenNaviInterfaceObject, adapter_id) == 8u ? 1 : -1];
#endif

static void open_navi_log(int priority, const char *format, ...)
{
#if defined(OPEN_NAVI_DISABLE_LOG)
    (void)priority;
    (void)format;
#else
    char message[257];
    size_t length;
    int trace_fd;
    int trace_flags = O_WRONLY | O_CREAT | O_APPEND;
    int close_result;
    ssize_t write_result;
    int saved_errno = errno;
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(message, sizeof(message) - 1u, format, arguments);
    va_end(arguments);

    message[sizeof(message) - 2u] = '\0';
    length = strlen(message);
    message[length++] = '\n';
    message[length] = '\0';

    /* On-target proof that does not depend on the product's syslog routing. */
#if defined(O_NOFOLLOW)
    trace_flags |= O_NOFOLLOW;
#endif
    trace_fd = open(OPEN_NAVI_TRACE_PATH, trace_flags, 0600);
#if defined(O_NOFOLLOW)
    /* QNX 6.6 headers expose O_NOFOLLOW on builds whose io-pkt/libc may
     * reject it at runtime. Retry only for an unsupported-flag result; never
     * follow a path after ELOOP, which still indicates a symlink. */
    if (trace_fd < 0 && (errno == EINVAL || errno == ENOTSUP)) {
        trace_fd = open(OPEN_NAVI_TRACE_PATH,
                        trace_flags & ~O_NOFOLLOW, 0600);
    }
#endif
    if (trace_fd >= 0) {
        write_result = write(trace_fd, message, length);
        close_result = close(trace_fd);
        (void)write_result;
        (void)close_result;
    }

#if !defined(OPEN_NAVI_DISABLE_SYSLOG)
    openlog("open_navi", LOG_PID | LOG_NDELAY, LOG_USER);
    syslog(priority, "%.*s", (int)(length - 1u), message);
    closelog();
#else
    (void)priority;
#endif
    errno = saved_errno;
#endif
}

static void prepare_objects(void)
{
    uint32_t adapter;

    if (g_objects_ready != 0) {
        return;
    }

    for (adapter = 0u; adapter < OPEN_NAVI_ADAPTER_COUNT; ++adapter) {
        if (adapter == OPEN_NAVI_HMI_ADAPTER) {
            g_adapters[adapter].vptr = g_hmi_vtable;
        } else if (adapter == 4u) {
            g_adapters[adapter].vptr = g_hardkey_vtable;
        } else if (adapter == 6u) {
            g_adapters[adapter].vptr = g_logging_vtable;
        } else if (adapter == 7u) {
            g_adapters[adapter].vptr = g_internationalization_vtable;
        } else if (adapter == OPEN_NAVI_VEHICLE_ADAPTER) {
            g_adapters[adapter].vptr = g_vehicle_vtable;
        } else if (adapter == OPEN_NAVI_GPS_ADAPTER) {
            g_adapters[adapter].vptr = g_gps_vtable;
        } else if (adapter == OPEN_NAVI_AUDIO_ADAPTER) {
            g_adapters[adapter].vptr = g_audio_vtable;
        } else if (adapter == 12u) {
            g_adapters[adapter].vptr = g_diagnosis_vtable;
        } else if (adapter == 13u) {
            g_adapters[adapter].vptr = g_diagnosis_navigation_vtable;
        } else if (adapter == 14u) {
            g_adapters[adapter].vptr = g_rds_vtable;
        } else if (adapter == 17u) {
            g_adapters[adapter].vptr = g_cluster_vtable;
        } else if (adapter == 19u) {
            g_adapters[adapter].vptr = g_data_exchange_vtable;
        } else if (adapter == 20u) {
            g_adapters[adapter].vptr = g_connectivity_vtable;
        } else if (adapter == 23u) {
            g_adapters[adapter].vptr = g_rotary_vtable;
        } else if (adapter == 24u) {
            g_adapters[adapter].vptr = g_tts_vtable;
        } else if (adapter == 25u) {
            g_adapters[adapter].vptr = g_vr_vtable;
        } else if (adapter == 26u) {
            g_adapters[adapter].vptr = g_phone_vtable;
        } else if (adapter == 29u) {
            g_adapters[adapter].vptr = g_parking_vtable;
        } else if (adapter == 30u) {
            g_adapters[adapter].vptr = g_apogee_vtable;
        } else if (adapter == 31u) {
            g_adapters[adapter].vptr = g_agw_vtable;
        } else if (adapter == 32u) {
            g_adapters[adapter].vptr = g_tpeg_ews_vtable;
        } else {
            g_adapters[adapter].vptr = g_dummy_vtable;
        }
        g_adapters[adapter].host_proxy = NULL;
        g_adapters[adapter].adapter_id = adapter;
    }
    g_overflow_adapter.vptr = g_dummy_vtable;
    g_overflow_adapter.host_proxy = NULL;
    g_overflow_adapter.adapter_id = UINT32_MAX;
    g_objects_ready = 1;
}

static void open_navi_clear_host_proxies_locked(void)
{
    uint32_t adapter;

    g_root.host_proxy = NULL;
    for (adapter = 0u; adapter < OPEN_NAVI_ADAPTER_COUNT; ++adapter) {
        g_adapters[adapter].host_proxy = NULL;
    }
    g_overflow_adapter.host_proxy = NULL;
}

static int open_navi_lifecycle_acquire(OpenNaviLifecycleLease *lease)
{
    int acquired = 0;

    lease->held = 0;
    lease->epoch = 0u;
    (void)pthread_mutex_lock(&g_lifecycle_lock);
    if (g_state == OPEN_NAVI_ACTIVE) {
        if (g_lifecycle_depth == 0u) {
            g_lifecycle_thread_epoch = g_lifecycle_epoch;
        }
        ++g_lifecycle_inflight;
        ++g_lifecycle_depth;
        lease->epoch = g_lifecycle_epoch;
        lease->held = 1;
        acquired = 1;
    }
    (void)pthread_mutex_unlock(&g_lifecycle_lock);
    return acquired;
}

static int open_navi_lifecycle_acquire_proxy(uint32_t adapter, uint32_t slot,
                                        OpenNaviLifecycleLease *lease,
                                        void **proxy_out)
{
    void *proxy;
    const PanaGenericVirtual *vtable;
    int acquired = 0;

    lease->held = 0;
    lease->epoch = 0u;
    *proxy_out = NULL;
    (void)pthread_mutex_lock(&g_lifecycle_lock);
    if (g_state == OPEN_NAVI_ACTIVE) {
        proxy = adapter == UINT32_MAX
                    ? g_root.host_proxy
                    : g_adapters[adapter].host_proxy;
        vtable = proxy != NULL
                     ? *(const PanaGenericVirtual *const *)proxy
                     : NULL;
        if (vtable != NULL && vtable[slot] != NULL) {
            if (g_lifecycle_depth == 0u) {
                g_lifecycle_thread_epoch = g_lifecycle_epoch;
            }
            ++g_lifecycle_inflight;
            ++g_lifecycle_depth;
            lease->epoch = g_lifecycle_epoch;
            lease->held = 1;
            *proxy_out = proxy;
            acquired = 1;
        }
    }
    (void)pthread_mutex_unlock(&g_lifecycle_lock);
    return acquired;
}

static int open_navi_lifecycle_is_current(const OpenNaviLifecycleLease *lease)
{
    int current;

    (void)pthread_mutex_lock(&g_lifecycle_lock);
    current = lease->held != 0 && g_state == OPEN_NAVI_ACTIVE &&
              lease->epoch == g_lifecycle_epoch;
    (void)pthread_mutex_unlock(&g_lifecycle_lock);
    return current;
}

/* Used by the low-level typed PAS ABI seams.  Direct seam calls remain
 * testable in isolation, while the production provider records status only
 * from a currently leased callback in the active epoch. */
int open_navi_callback_telemetry_active(void)
{
    int active;
    (void)pthread_mutex_lock(&g_lifecycle_lock);
    active = g_state == OPEN_NAVI_ACTIVE && g_lifecycle_depth != 0u &&
             g_lifecycle_thread_epoch == g_lifecycle_epoch;
    (void)pthread_mutex_unlock(&g_lifecycle_lock);
    return active;
}

/* Internal hooks used by producer backends whose own commit happens after the
 * PAS callback returns.  Keeping the outer lease alive makes that commit part
 * of the same lifecycle epoch instead of a post-callback race. */
int open_navi_producer_begin(uint64_t *epoch_out)
{
    OpenNaviLifecycleLease lease;

    if (epoch_out == NULL || !open_navi_lifecycle_acquire(&lease)) {
        return 0;
    }
    *epoch_out = lease.epoch;
    return 1;
}

int open_navi_producer_current(uint64_t epoch)
{
    OpenNaviLifecycleLease lease;

    lease.epoch = epoch;
    lease.held = 1;
    return open_navi_lifecycle_is_current(&lease);
}

void open_navi_producer_end(uint64_t epoch)
{
    OpenNaviLifecycleLease lease;

    lease.epoch = epoch;
    lease.held = 1;
    open_navi_lifecycle_release(&lease);
}

static void open_navi_lifecycle_finish_claimed(void)
{
    open_navi_finish_deinitialize();
    (void)pthread_mutex_lock(&g_lifecycle_lock);
    open_navi_clear_host_proxies_locked();
    g_deinitialize_deferred = 0;
    g_deinitialize_finishing = 0;
    g_state = OPEN_NAVI_COLD;
    (void)pthread_cond_broadcast(&g_lifecycle_condition);
    (void)pthread_mutex_unlock(&g_lifecycle_lock);
}

static void open_navi_lifecycle_release(OpenNaviLifecycleLease *lease)
{
    int finish_deinitialize = 0;

    if (lease->held == 0) {
        return;
    }
    (void)pthread_mutex_lock(&g_lifecycle_lock);
    lease->held = 0;
    if (g_lifecycle_depth != 0u) {
        --g_lifecycle_depth;
        if (g_lifecycle_depth == 0u) {
            g_lifecycle_thread_epoch = 0u;
        }
    }
    if (g_lifecycle_inflight != 0u) {
        --g_lifecycle_inflight;
    }
    if (g_state == OPEN_NAVI_STOPPING && g_lifecycle_inflight == 0u &&
        g_deinitialize_deferred != 0 && g_deinitialize_finishing == 0 &&
        g_readiness_worker_thread == 0) {
        g_deinitialize_finishing = 1;
        finish_deinitialize = 1;
    }
    (void)pthread_cond_broadcast(&g_lifecycle_condition);
    (void)pthread_mutex_unlock(&g_lifecycle_lock);
    if (finish_deinitialize != 0) {
        open_navi_lifecycle_finish_claimed();
    }
}

/* A teardown requested reentrantly by a host callback can run on the
 * readiness worker itself.  That worker must leave its shared-state cleanup
 * epilogue before the lifecycle becomes COLD; otherwise a new lifecycle can
 * create its worker and then have the old epilogue overwrite its flags. */
static void open_navi_lifecycle_finish_after_worker_exit(void)
{
    int finish_deinitialize = 0;

    (void)pthread_mutex_lock(&g_lifecycle_lock);
    if (g_state == OPEN_NAVI_STOPPING && g_lifecycle_inflight == 0u &&
        g_deinitialize_deferred != 0 && g_deinitialize_finishing == 0) {
        g_deinitialize_finishing = 1;
        finish_deinitialize = 1;
    }
    (void)pthread_cond_broadcast(&g_lifecycle_condition);
    (void)pthread_mutex_unlock(&g_lifecycle_lock);
    if (finish_deinitialize != 0) {
        open_navi_lifecycle_finish_claimed();
    }
}

static uint32_t open_navi_generic_virtual(void *self, ...)
{
    OpenNaviLifecycleLease lease;

    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) {
        return 1u;
    }
    open_navi_lifecycle_release(&lease);
    return 0u;
}

static uint32_t nc_provider_gps_data(void *self, const void *gps_record)
{
    OpenNaviInterfaceObject *adapter = (OpenNaviInterfaceObject *)self;
    OpenNaviLifecycleLease lease;
    uint8_t local_record[OPEN_NAVI_GPS_RECORD_SIZE];
    OpenNaviGpsSnapshot snapshot;
    uint32_t call_count;
    int parsed = 0;
    int retained = 1;

    if (!open_navi_lifecycle_acquire(&lease)) {
        return 1u;
    }
    /* Copy the complete host record synchronously before parsing. Neither the
     * parser nor backend ever receives the caller-owned address. */
    if (gps_record != NULL) {
        memcpy(local_record, gps_record, sizeof(local_record));
        open_navi_gps_snapshot_parse(&snapshot, local_record);
        open_navi_gps_backend_submit(&snapshot);
        retained = open_navi_renderer_notify_gps(&snapshot) == 0;
        parsed = 1;
    }

    (void)pthread_mutex_lock(&g_worker_lock);
    if (g_gps_call_count != UINT32_MAX) {
        ++g_gps_call_count;
    }
    g_gps_last_record_address = (uintptr_t)gps_record;
    call_count = g_gps_call_count;
    (void)pthread_mutex_unlock(&g_worker_lock);

    if (gps_record == NULL || call_count == 1u ||
        (call_count != 0u && (call_count & (call_count - 1u)) == 0u)) {
        open_navi_log(LOG_NOTICE,
                 "GPS data slot=4 self=%p adapter=%u record=%p parsed=%d "
                 "call_count=%u checkpoint=1",
                 self, adapter != NULL ? adapter->adapter_id : UINT32_MAX,
                 gps_record, parsed, call_count);
    }
    open_navi_lifecycle_release(&lease);
    return retained != 0 ? 0u : 1u;
}

static uint32_t open_navi_vehicle_day_night_status(void *self, uint8_t value)
{
    OpenNaviLifecycleLease lease;
    int retained;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_api_telemetry_record(8u, 4u, 1u, (uint32_t)value, 0u, 0u, 0u);
    open_navi_scalar_backend_vehicle_day_night_status(value);
    retained = open_navi_renderer_notify_vehicle_status(4u, 0u, value) == 0;
    open_navi_log(LOG_NOTICE, "VehicleData slot=4 self=%p day_night_status=%u",
             self, (unsigned int)value);
    open_navi_lifecycle_release(&lease);
    return retained != 0 ? 0u : 1u;
}

static uint32_t open_navi_vehicle_day_night_setting(void *self, uint8_t value)
{
    OpenNaviLifecycleLease lease;
    int retained;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_api_telemetry_record(8u, 6u, 1u, (uint32_t)value, 0u, 0u, 0u);
    open_navi_scalar_backend_vehicle_day_night_setting(value);
    retained = open_navi_renderer_notify_vehicle_setting(6u, 0u, value) == 0;
    open_navi_log(LOG_NOTICE, "VehicleData slot=6 self=%p day_night_setting=%u",
             self, (unsigned int)value);
    open_navi_lifecycle_release(&lease);
    return retained != 0 ? 0u : 1u;
}

static uint32_t open_navi_vehicle_speed(void *self, float value)
{
    OpenNaviLifecycleLease lease;
    uint32_t bits;
    int retained;

    if (!open_navi_lifecycle_acquire(&lease)) return 1u;

    memcpy(&bits, &value, sizeof(bits));
    open_navi_api_telemetry_record(8u, 8u, 1u, bits, 0u, 0u, 0u);
    open_navi_scalar_backend_vehicle_speed(value);
    retained = open_navi_renderer_notify_vehicle_speed(8u, bits) == 0;
    open_navi_log(LOG_NOTICE,
             "VehicleData slot=8 self=%p displayed_vehicle_speed_bits=%08x",
             self, (unsigned int)bits);
    open_navi_lifecycle_release(&lease);
    return retained != 0 ? 0u : 1u;
}

static uint32_t open_navi_gps_interval(void *self, uint32_t interval_ms)
{
    OpenNaviLifecycleLease lease;
    int retained;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_api_telemetry_record(9u, 6u, 1u, interval_ms, 0u, 0u, 0u);
    open_navi_scalar_backend_gps_interval(interval_ms);
    retained = open_navi_renderer_notify_gps_interval(6u, interval_ms) == 0;
    open_navi_log(LOG_NOTICE, "GPSData slot=6 self=%p interval_ms=%u",
             self, interval_ms);
    open_navi_lifecycle_release(&lease);
    return retained != 0 ? 0u : 1u;
}

static uint32_t open_navi_audio_volume(void *self,
                                  uint32_t audio_source,
                                  uint8_t volume)
{
    OpenNaviLifecycleLease lease;
    int retained;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_api_telemetry_record(10u, 19u, 2u, audio_source,
                              (uint32_t)volume, 0u, 0u);
    open_navi_scalar_backend_audio(audio_source, volume);
    retained = open_navi_renderer_notify_audio(19u, 0u, audio_source, volume) == 0;
    open_navi_log(LOG_NOTICE,
             "AudioHandling slot=19 self=%p audio_source=%u volume=%u",
             self, audio_source, (unsigned int)volume);
    open_navi_lifecycle_release(&lease);
    return retained != 0 ? 0u : 1u;
}

#define DEFINE_TELEM_0(name, adapter_value, slot_value)                       \
    static uint32_t name(void *self)                                           \
    {                                                                           \
        OpenNaviLifecycleLease lease;                                               \
        (void)self;                                                             \
        if (!open_navi_lifecycle_acquire(&lease)) return 1u;                        \
        open_navi_api_telemetry_record(adapter_value, slot_value, 0u,               \
                                  0u, 0u, 0u, 0u);                             \
        open_navi_lifecycle_release(&lease);                                         \
        return 0u;                                                              \
    }

#define DEFINE_TELEM_1_U32(name, adapter_value, slot_value)                    \
    static uint32_t name(void *self, uint32_t argument0)                       \
    {                                                                           \
        OpenNaviLifecycleLease lease;                                               \
        (void)self;                                                             \
        if (!open_navi_lifecycle_acquire(&lease)) return 1u;                        \
        open_navi_api_telemetry_record(adapter_value, slot_value, 1u,               \
                                  argument0, 0u, 0u, 0u);                      \
        open_navi_lifecycle_release(&lease);                                         \
        return 0u;                                                              \
    }

#define DEFINE_TELEM_1_U8(name, adapter_value, slot_value)                     \
    static uint32_t name(void *self, uint8_t argument0)                        \
    {                                                                           \
        OpenNaviLifecycleLease lease;                                               \
        (void)self;                                                             \
        if (!open_navi_lifecycle_acquire(&lease)) return 1u;                        \
        open_navi_api_telemetry_record(adapter_value, slot_value, 1u,               \
                                  (uint32_t)argument0, 0u, 0u, 0u);            \
        open_navi_lifecycle_release(&lease);                                         \
        return 0u;                                                              \
    }

#define DEFINE_TELEM_2_U32_U8(name, adapter_value, slot_value)                 \
    static uint32_t name(void *self, uint32_t argument0, uint8_t argument1)    \
    {                                                                           \
        OpenNaviLifecycleLease lease;                                               \
        (void)self;                                                             \
        if (!open_navi_lifecycle_acquire(&lease)) return 1u;                        \
        open_navi_api_telemetry_record(adapter_value, slot_value, 2u,               \
                                  argument0, (uint32_t)argument1, 0u, 0u);     \
        open_navi_lifecycle_release(&lease);                                         \
        return 0u;                                                              \
    }

#define DEFINE_TELEM_2_U32(name, adapter_value, slot_value)                    \
    static uint32_t name(void *self, uint32_t argument0, uint32_t argument1)   \
    {                                                                           \
        OpenNaviLifecycleLease lease;                                               \
        (void)self;                                                             \
        if (!open_navi_lifecycle_acquire(&lease)) return 1u;                        \
        open_navi_api_telemetry_record(adapter_value, slot_value, 2u,               \
                                  argument0, argument1, 0u, 0u);               \
        open_navi_lifecycle_release(&lease);                                         \
        return 0u;                                                              \
    }

#define DEFINE_TELEM_3_U32(name, adapter_value, slot_value)                    \
    static uint32_t name(void *self, uint32_t argument0, uint32_t argument1,   \
                         uint32_t argument2)                                    \
    {                                                                           \
        OpenNaviLifecycleLease lease;                                               \
        (void)self;                                                             \
        if (!open_navi_lifecycle_acquire(&lease)) return 1u;                        \
        open_navi_api_telemetry_record(adapter_value, slot_value, 3u,               \
                                  argument0, argument1, argument2, 0u);        \
        open_navi_lifecycle_release(&lease);                                         \
        return 0u;                                                              \
    }

static uint32_t open_navi_static_provider_accept(void *self,
                                             uint32_t slot,
                                             uint32_t word_count,
                                             uintptr_t argument0,
                                             uintptr_t argument1,
                                             uintptr_t argument2,
                                             uintptr_t argument3)
{
    OpenNaviLifecycleLease lease;
    uint32_t adapter = UINT32_MAX;

    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    if (self != NULL && self != (void *)&g_root) {
        adapter = ((const OpenNaviInterfaceObject *)self)->adapter_id;
        open_navi_api_telemetry_record(adapter, slot, word_count,
                                  (uint32_t)argument0,
                                  (uint32_t)argument1,
                                  (uint32_t)argument2,
                                  (uint32_t)argument3);
    }
    open_navi_log(LOG_NOTICE,
             "static provider adapter=%u slot=%u words=%u accepted",
             adapter, slot, word_count);
    open_navi_lifecycle_release(&lease);
    return 0u;
}

#define DEFINE_STATIC_0(name, slot)                                           \
    static uint32_t name(void *self)                                          \
    { return open_navi_static_provider_accept(self, slot, 0u, 0u, 0u, 0u, 0u); }
#define DEFINE_STATIC_1(name, slot)                                           \
    static uint32_t name(void *self, uintptr_t a0)                            \
    { return open_navi_static_provider_accept(self, slot, 1u, a0, 0u, 0u, 0u); }
#define DEFINE_STATIC_2(name, slot)                                           \
    static uint32_t name(void *self, uintptr_t a0, uintptr_t a1)              \
    { return open_navi_static_provider_accept(self, slot, 2u, a0, a1, 0u, 0u); }
#define DEFINE_STATIC_3(name, slot)                                           \
    static uint32_t name(void *self, uintptr_t a0, uintptr_t a1, uintptr_t a2)\
    { return open_navi_static_provider_accept(self, slot, 3u, a0, a1, a2, 0u); }
#define DEFINE_STATIC_4(name, slot)                                           \
    static uint32_t name(void *self, uintptr_t a0, uintptr_t a1,              \
                         uintptr_t a2, uintptr_t a3)                           \
    { return open_navi_static_provider_accept(self, slot, 4u, a0, a1, a2, a3); }
#define DEFINE_STATIC_5(name, slot)                                           \
    static uint32_t name(void *self, uintptr_t a0, uintptr_t a1,              \
                         uintptr_t a2, uintptr_t a3, uintptr_t a4)             \
    { (void)a4; return open_navi_static_provider_accept(                           \
          self, slot, 5u, a0, a1, a2, a3); }
#define DEFINE_STATIC_6(name, slot)                                           \
    static uint32_t name(void *self, uintptr_t a0, uintptr_t a1,              \
                         uintptr_t a2, uintptr_t a3, uintptr_t a4,             \
                         uintptr_t a5)                                        \
    { (void)a4; (void)a5; return open_navi_static_provider_accept(                 \
          self, slot, 6u, a0, a1, a2, a3); }
#define DEFINE_STATIC_7(name, slot)                                           \
    static uint32_t name(void *self, uintptr_t a0, uintptr_t a1,              \
                         uintptr_t a2, uintptr_t a3, uintptr_t a4,             \
                         uintptr_t a5, uintptr_t a6)                           \
    { (void)a4; (void)a5; (void)a6; return open_navi_static_provider_accept(       \
          self, slot, 7u, a0, a1, a2, a3); }
#define OPEN_NAVI_STATIC_SLOT(adapter, slot, words) \
    DEFINE_STATIC_##words(open_navi_static_##adapter##_##slot, slot)
#include "provider_slots.def"
#undef OPEN_NAVI_STATIC_SLOT
#undef DEFINE_STATIC_0
#undef DEFINE_STATIC_1
#undef DEFINE_STATIC_2
#undef DEFINE_STATIC_3
#undef DEFINE_STATIC_4
#undef DEFINE_STATIC_5
#undef DEFINE_STATIC_6
#undef DEFINE_STATIC_7

static uint32_t nc_provider_vehicle_day_night_status_reply(void *self, uint32_t handle, uint8_t status)
{
    OpenNaviLifecycleLease lease;
    int retained;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    (void)open_navi_callback_pending_complete(8u, 3u, handle);
    open_navi_api_telemetry_record(8u, 3u, 2u, handle, (uint32_t)status,
                              0u, 0u);
    open_navi_scalar_backend_vehicle_day_night_status(status);
    retained = open_navi_renderer_notify_vehicle_status(3u, handle, status) == 0;
    open_navi_lifecycle_release(&lease);
    return retained != 0 ? 0u : 1u;
}

static uint32_t nc_provider_vehicle_day_night_setting_reply(void *self, uint32_t handle, uint8_t setting)
{
    OpenNaviLifecycleLease lease;
    int retained;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    (void)open_navi_callback_pending_complete(8u, 5u, handle);
    open_navi_api_telemetry_record(8u, 5u, 2u, handle, (uint32_t)setting,
                              0u, 0u);
    open_navi_scalar_backend_vehicle_day_night_setting(setting);
    retained = open_navi_renderer_notify_vehicle_setting(5u, handle, setting) == 0;
    open_navi_lifecycle_release(&lease);
    return retained != 0 ? 0u : 1u;
}

static uint32_t open_navi_10_17(void *self,
                           uint32_t handle,
                           uint32_t source,
                           uint8_t volume)
{
    OpenNaviLifecycleLease lease;
    int retained;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    (void)open_navi_callback_pending_complete(10u, 17u, handle);
    open_navi_api_telemetry_record(10u, 17u, 3u, handle, source,
                              (uint32_t)volume, 0u);
    open_navi_scalar_backend_audio(source, volume);
    retained = open_navi_renderer_notify_audio(17u, handle, source, volume) == 0;
    open_navi_lifecycle_release(&lease);
    return retained != 0 ? 0u : 1u;
}

DEFINE_TELEM_1_U32(open_navi_12_5, 12u, 5u)
DEFINE_TELEM_1_U32(open_navi_12_6, 12u, 6u)
DEFINE_TELEM_3_U32(open_navi_12_7, 12u, 7u)
static uint32_t open_navi_13_3(void *self, uint32_t handle,
                          uint32_t distraction_status)
{
    OpenNaviLifecycleLease lease;
    int retained;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    (void)open_navi_callback_pending_complete(13u, 3u, handle);
    open_navi_api_telemetry_record(13u, 3u, 2u, handle,
                              distraction_status, 0u, 0u);
    retained = open_navi_renderer_notify_driver_distraction(
        3u, handle, distraction_status) == 0;
    open_navi_lifecycle_release(&lease);
    return retained != 0 ? 0u : 1u;
}

static uint32_t open_navi_13_4(void *self, uint32_t distraction_status)
{
    OpenNaviLifecycleLease lease;
    int retained;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_api_telemetry_record(13u, 4u, 1u, distraction_status,
                              0u, 0u, 0u);
    retained = open_navi_renderer_notify_driver_distraction(
        4u, 0u, distraction_status) == 0;
    open_navi_lifecycle_release(&lease);
    return retained != 0 ? 0u : 1u;
}
DEFINE_TELEM_1_U32(open_navi_13_12, 13u, 12u)
DEFINE_TELEM_1_U32(open_navi_13_13, 13u, 13u)
DEFINE_TELEM_1_U32(open_navi_13_14, 13u, 14u)
static uint32_t open_navi_17_6(void *self, uint32_t handle, uint8_t enabled)
{
    OpenNaviLifecycleLease lease;
    int retained;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    (void)open_navi_callback_pending_complete(17u,6u,handle);
    open_navi_api_telemetry_record(17u,6u,2u,handle,(uint32_t)enabled,0u,0u);
    retained = open_navi_renderer_notify_cluster_display(6u, handle, enabled) == 0;
    open_navi_lifecycle_release(&lease);
    return retained != 0 ? 0u : 1u;
}

static uint32_t open_navi_20_3(void *self,
                          uint32_t handle,
                          uint32_t service,
                          uint8_t connection_status,
                          uint32_t extended_status)
{
    OpenNaviLifecycleLease lease;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_api_telemetry_record(20u, 3u, 4u, handle, service,
                              (uint32_t)connection_status, extended_status);
    open_navi_lifecycle_release(&lease);
    return 0u;
}

static uint32_t open_navi_20_4(void *self,
                          uint32_t service,
                          uint8_t connection_status,
                          uint32_t extended_status)
{
    OpenNaviLifecycleLease lease;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_api_telemetry_record(20u, 4u, 3u, service,
                              (uint32_t)connection_status,
                              extended_status, 0u);
    open_navi_lifecycle_release(&lease);
    return 0u;
}

static uint32_t open_navi_20_7(void *self,
                          uint32_t handle,
                          uint32_t service,
                          uint8_t strength)
{
    OpenNaviLifecycleLease lease;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_api_telemetry_record(20u, 7u, 3u, handle, service,
                              (uint32_t)strength, 0u);
    open_navi_lifecycle_release(&lease);
    return 0u;
}

static uint32_t open_navi_20_8(void *self,
                          uint32_t handle,
                          uint32_t service,
                          uint8_t strength)
{
    OpenNaviLifecycleLease lease;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_api_telemetry_record(20u, 8u, 3u, handle, service,
                              (uint32_t)strength, 0u);
    open_navi_lifecycle_release(&lease);
    return 0u;
}

/* Bench marker: report the backend's guidance state in replyRouteGuidance
 * Status instead of the constant stock idle value.  Read at panaInitialize
 * (before the lifecycle turns ACTIVE; slot 19 reads it under a lease). */
#ifndef OPEN_NAVI_REPORT_GUIDANCE_STATUS_MARKER
#define OPEN_NAVI_REPORT_GUIDANCE_STATUS_MARKER \
    "/datastore/navicore-backend/report-guidance-status"
#endif
static const char *g_report_guidance_status_marker =
    OPEN_NAVI_REPORT_GUIDANCE_STATUS_MARKER;
static int g_report_guidance_status;

static void open_navi_read_guidance_status_marker(void)
{
    g_report_guidance_status =
        access(g_report_guidance_status_marker, F_OK) == 0;
    open_navi_log(LOG_NOTICE,
             "replyRouteGuidanceStatus guidance_status=%s (marker %s %s)",
             g_report_guidance_status != 0 ? "backend-state" : "constant-1",
             g_report_guidance_status_marker,
             g_report_guidance_status != 0 ? "present" : "absent");
}

#if defined(OPEN_NAVI_TESTING)
void open_navi_test_reload_guidance_status_marker(const char *path)
{
    g_report_guidance_status_marker =
        path != NULL ? path : OPEN_NAVI_REPORT_GUIDANCE_STATUS_MARKER;
    open_navi_read_guidance_status_marker();
}
#endif

/* requestGuidanceStop(handle), adapter 25 slot 13: nv_navigation calls it
 * from onGuidanceStopRequest (0x080ead12) and from the projection path
 * OnCARPLAY_RouteGuidanceInstruction (0x080ec5de, answered to SAL as
 * GuidanceStopResponse) and waits for replyGuidanceStop.  The stop goes to
 * a V6 backend now; replyGuidanceStop (callback 25/17, result 2) follows
 * asynchronously from the HMI reply FIFO, whether or not anything was
 * guiding (stock always answers). */
static uint32_t open_navi_25_13(void *self, uintptr_t handle_word)
{
    OpenNaviLifecycleLease lease;
    const uint32_t handle = (uint32_t)handle_word;
    unsigned int queue_depth;
    int delivery;
    int was_guiding = 0;
    int accepted = 0;

    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_api_telemetry_record(25u, 13u, 1u, handle, 0u, 0u, 0u);
    delivery = open_navi_renderer_notify_guidance_stop(13u, handle);
    if (delivery == 0)
        was_guiding = open_navi_callback_guidance_stop_requested();
    open_navi_callback_pending_add(25u, 13u, handle);
    (void)pthread_mutex_lock(&g_worker_lock);
    if (g_worker_stop == 0 &&
        g_hmi_event_queue_count < OPEN_NAVI_HMI_EVENT_QUEUE_CAPACITY) {
        unsigned int tail = (g_hmi_event_queue_head +
                             g_hmi_event_queue_count) %
                            OPEN_NAVI_HMI_EVENT_QUEUE_CAPACITY;
        g_hmi_event_queue[tail].type = OPEN_NAVI_HMI_EVENT_GUIDANCE_STOP;
        g_hmi_event_queue[tail].request.guidance_stop.handle = handle;
        ++g_hmi_event_queue_count;
        accepted = 1;
        (void)pthread_cond_signal(&g_worker_condition);
    }
    queue_depth = g_hmi_event_queue_count;
    (void)pthread_mutex_unlock(&g_worker_lock);
    if (accepted == 0)
        open_navi_callback_pending_cancel(25u, 13u, handle);
    open_navi_log(LOG_NOTICE,
             "VR requestGuidanceStop slot=13 handle=%u backend_delivery=%d "
             "was_guiding=%d accepted=%d queue_depth=%u",
             handle, delivery, was_guiding, accepted, queue_depth);
    open_navi_lifecycle_release(&lease);
    return accepted != 0 ? 0u : 1u;
}

/* requestRouteGuidanceStatus(handle), polled by nv_navigation's
 * OnRouteGuidanceRepeatTimer (1000 ms, re-armed after every poll from
 * adapterReady on), answered with replyRouteGuidanceStatus (callback 25/23:
 * handle, result 2, guidance_status).  The meaning of guidance_status is not
 * recovered: SAL publishes 0 as event status 1 (projection navigation
 * allowed), but stock answered 1 while idle.  Default: always 1, the stock
 * idle value.  With the bench marker OPEN_NAVI_REPORT_GUIDANCE_STATUS_MARKER
 * present at provider start, the backend's published state is reported
 * instead: 0 while guiding, 1 otherwise. */
static uint32_t open_navi_25_19(void *self, uint32_t handle)
{
    OpenNaviLifecycleLease inbound;
    OpenNaviLifecycleLease outbound;
    void *proxy;
    uint32_t reply_result,guidance;
    uint32_t status = 1u;
    (void)self;
    if (!open_navi_lifecycle_acquire(&inbound)) return 1u;
    open_navi_api_telemetry_record(25u,19u,1u,handle,0u,0u,0u);
    open_navi_callback_get_voice_state(&reply_result,&guidance);
    if (g_report_guidance_status == 0)
        guidance = OPEN_NAVI_VOICE_GUIDANCE_STATUS_IDLE;
    open_navi_callback_pending_add(25u,19u,handle);
    if (open_navi_lifecycle_acquire_proxy(25u, 23u, &outbound, &proxy)) {
        status = nc_host_voice_reply_route_guidance_status(proxy,handle,reply_result,guidance);
        if (status == 0u && open_navi_lifecycle_is_current(&outbound)) {
            (void)open_navi_callback_pending_complete(25u,19u,handle);
        }
        open_navi_lifecycle_release(&outbound);
    }
    if (status != 0u && open_navi_lifecycle_is_current(&inbound)) {
        open_navi_callback_pending_cancel(25u,19u,handle);
    }
    open_navi_lifecycle_release(&inbound);
    return 0u;
}
DEFINE_TELEM_2_U32_U8(open_navi_26_5, 26u, 5u)
DEFINE_TELEM_1_U32(open_navi_29_3, 29u, 3u)
DEFINE_TELEM_0(open_navi_29_4, 29u, 4u)
DEFINE_TELEM_1_U32(open_navi_30_3, 30u, 3u)
DEFINE_TELEM_1_U32(open_navi_30_4, 30u, 4u)
DEFINE_TELEM_1_U8(open_navi_31_3, 31u, 3u)
DEFINE_TELEM_1_U8(open_navi_31_5, 31u, 5u)

static uint32_t nc_provider_logging_config(void *self,
                         uint32_t handle,
                         uint32_t count,
                         const uint8_t *levels,
                         const uint32_t *sizes)
{
    OpenNaviLifecycleLease lease;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_pointer_backend_logging(handle, count, levels, sizes);
    open_navi_lifecycle_release(&lease);
    return 0u;
}

static uint32_t nc_provider_i18n_settings_reply(void *self, uint32_t handle,
                         const OpenNaviIntlSettingsInput *settings)
{
    OpenNaviLifecycleLease lease;
    uint8_t settings_raw[4] = {0u, 0u, 0u, 0u};
    int retained;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    (void)open_navi_callback_pending_complete(7u, 3u, handle);
    if (settings != NULL)
        memcpy(settings_raw, settings, sizeof(settings_raw));
    open_navi_pointer_backend_internationalization(
        handle, settings != NULL
                    ? (const OpenNaviIntlSettingsInput *)settings_raw : NULL);
    retained = open_navi_renderer_notify_internationalization(
        3u, handle, settings_raw, settings != NULL) == 0;
    open_navi_lifecycle_release(&lease);
    return retained != 0 ? 0u : 1u;
}

static uint32_t open_navi_8_12(void *self, uint32_t handle, const char *vin17)
{
    OpenNaviLifecycleLease lease;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    (void)open_navi_callback_pending_complete(8u,12u,handle);
    open_navi_pointer_backend_vin(handle, vin17);
    open_navi_lifecycle_release(&lease);
    return 0u;
}

static uint32_t open_navi_19_3(void *self,
                          uint32_t handle,
                          uint32_t count,
                          const OpenNaviTransportMeansObservedInput *items)
{
    OpenNaviLifecycleLease lease;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_pointer_backend_transport(handle, count, items);
    open_navi_lifecycle_release(&lease);
    return 0u;
}

static uint32_t open_navi_20_6(void *self,
                          uint32_t handle,
                          uint32_t service,
                          const OpenNaviConnectionDeviceInfoInput *info)
{
    OpenNaviLifecycleLease lease;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_pointer_backend_connection(handle, service, info);
    open_navi_lifecycle_release(&lease);
    return 0u;
}

static uint32_t open_navi_25_21(void *self,
                           uint32_t handle,
                           uint32_t category,
                           const char *filename)
{
    OpenNaviLifecycleLease lease;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_pointer_backend_tef(handle, category, filename);
    open_navi_lifecycle_release(&lease);
    return 0u;
}

#if defined(OPEN_NAVI_TESTING)
uint32_t open_navi_gps_test_call_count(void)
{
    uint32_t value;

    (void)pthread_mutex_lock(&g_worker_lock);
    value = g_gps_call_count;
    (void)pthread_mutex_unlock(&g_worker_lock);
    return value;
}

uintptr_t open_navi_gps_test_last_record_address(void)
{
    uintptr_t value;

    (void)pthread_mutex_lock(&g_worker_lock);
    value = g_gps_last_record_address;
    (void)pthread_mutex_unlock(&g_worker_lock);
    return value;
}

uint32_t open_navi_hmi_test_transition_reply_count(void)
{
    uint32_t value;

    (void)pthread_mutex_lock(&g_worker_lock);
    value = g_transition_reply_count;
    (void)pthread_mutex_unlock(&g_worker_lock);
    return value;
}
#endif

static int open_navi_worker_should_stop(void)
{
    int stop;
    uint64_t epoch;

    (void)pthread_mutex_lock(&g_worker_lock);
    stop = g_worker_stop;
    epoch = g_worker_epoch;
    (void)pthread_mutex_unlock(&g_worker_lock);
    (void)pthread_mutex_lock(&g_lifecycle_lock);
    if (g_state != OPEN_NAVI_ACTIVE || epoch != g_lifecycle_epoch) stop = 1;
    (void)pthread_mutex_unlock(&g_lifecycle_lock);
    return stop;
}

static int open_navi_worker_delay(unsigned int milliseconds)
{
    struct timespec requested;
    struct timespec remaining;

    requested.tv_sec = (time_t)(milliseconds / 1000u);
    requested.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    while (nanosleep(&requested, &remaining) != 0 && errno == EINTR) {
        requested = remaining;
    }
    return open_navi_worker_should_stop() == 0;
}

static uint32_t open_navi_allocate_hmi_handle(void)
{
    OpenNaviLifecycleLease lease;
    uint32_t handle;
    if (!open_navi_lifecycle_acquire(&lease)) return 0u;
    handle = open_navi_callback_allocate_handle();
    open_navi_lifecycle_release(&lease);
    return handle;
}

static int open_navi_wait_for_required_proxies(void)
{
    unsigned int attempt;

    /* signalStart normally follows registration. The bounded polling window
     * also tolerates scheduling visibility and proves we never call through a
     * proxy before stock has populated both +4 fields. */
    for (attempt = 0u; attempt < 20u; ++attempt) {
        OpenNaviLifecycleLease si_lease;
        OpenNaviLifecycleLease hmi_lease;
        void *si_proxy;
        void *hmi_proxy;
        int si_ready = open_navi_lifecycle_acquire_proxy(
            UINT32_MAX, 12u, &si_lease, &si_proxy);
        int hmi_ready = open_navi_lifecycle_acquire_proxy(
            1u, 2u, &hmi_lease, &hmi_proxy);

        if (si_ready) open_navi_lifecycle_release(&si_lease);
        if (hmi_ready) open_navi_lifecycle_release(&hmi_lease);
        if (si_ready && hmi_ready) {
            return 1;
        }
        if (!open_navi_worker_delay(100u)) {
            return 0;
        }
    }

    open_navi_log(LOG_ERR, "readiness worker timed out waiting for proxies");
    return 0;
}

static int open_navi_emit_root_ready(void)
{
    OpenNaviLifecycleLease lease;
    void *proxy;
    uint32_t status;
    if (!open_navi_lifecycle_acquire_proxy(UINT32_MAX, 12u, &lease, &proxy))
        return 0;
    status = nc_host_root_adapter_ready(proxy);
    if (!open_navi_lifecycle_is_current(&lease)) status = 1u;
    open_navi_lifecycle_release(&lease);
    return status == 0u;
}

static int open_navi_emit_hmi0(uint32_t slot)
{
    OpenNaviLifecycleLease lease;
    void *proxy;
    uint32_t status = 1u;
    if (!open_navi_lifecycle_acquire_proxy(1u, slot, &lease, &proxy)) return 0;
    if (slot == 2u) status = nc_host_hmi_adapter_ready(proxy);
    if (!open_navi_lifecycle_is_current(&lease)) status = 1u;
    open_navi_lifecycle_release(&lease);
    return status == 0u;
}

static int open_navi_emit_hmi1(uint32_t slot, uint32_t value)
{
    OpenNaviLifecycleLease lease;
    void *proxy;
    uint32_t status = 1u;
    if (!open_navi_lifecycle_acquire_proxy(1u, slot, &lease, &proxy)) return 0;
    if (slot == 3u) status = nc_host_hmi_subscribe(proxy, value);
    else if (slot == 14u) status = nc_host_hmi_signal_navigation_hmi_ready(proxy, value);
    if (!open_navi_lifecycle_is_current(&lease)) status = 1u;
    open_navi_lifecycle_release(&lease);
    return status == 0u;
}

static int open_navi_emit_hmi2(uint32_t slot, uint32_t a, uint32_t b)
{
    OpenNaviLifecycleLease lease;
    void *proxy;
    uint32_t status = 1u;
    if (!open_navi_lifecycle_acquire_proxy(1u, slot, &lease, &proxy)) return 0;
    if (slot == 6u) status = nc_host_hmi_request_screen_window_identifier(proxy, a, b);
    if (!open_navi_lifecycle_is_current(&lease)) status = 1u;
    open_navi_lifecycle_release(&lease);
    return status == 0u;
}

static int open_navi_emit_hmi3(uint32_t slot, uint32_t a, uint32_t b, uint32_t c)
{
    OpenNaviLifecycleLease lease;
    void *proxy;
    uint32_t status = 1u;
    if (!open_navi_lifecycle_acquire_proxy(1u, slot, &lease, &proxy)) return 0;
    if (slot == 7u) status = nc_host_hmi_request_hmi_state(proxy, a, b, c);
    if (!open_navi_lifecycle_is_current(&lease)) status = 1u;
    open_navi_lifecycle_release(&lease);
    return status == 0u;
}

static int open_navi_signal_adapter_ready(uint32_t adapter_id)
{
    OpenNaviLifecycleLease lease;
    void *proxy;
    uint32_t result;

    if (!open_navi_lifecycle_acquire_proxy(adapter_id, 2u, &lease, &proxy)) {
        open_navi_log(LOG_ERR,
                 "adapterReady unavailable adapter=%u", adapter_id);
        return 0;
    }
    open_navi_log(LOG_NOTICE,
             "adapterReady adapter=%u proxy=%p slot=2 begin",
             adapter_id, proxy);
    result = nc_host_adapter_ready(proxy, adapter_id);
    open_navi_log(LOG_NOTICE,
             "adapterReady adapter=%u complete result=%u",
             adapter_id, result);
    if (!open_navi_lifecycle_is_current(&lease)) result = 1u;
    open_navi_lifecycle_release(&lease);
    return result == 0u;
}

static int open_navi_call_callback0(uint32_t adapter_id, uint32_t slot,
                               OpenNaviCallbackClass kind)
{
    OpenNaviLifecycleLease lease;
    void *proxy;
    uint32_t result = 1u;
    int current = 0;

    if (open_navi_lifecycle_acquire_proxy(adapter_id, slot, &lease, &proxy) &&
        adapter_id == 17u && slot == 6u) {
        result = nc_host_cluster_request_or_signal_no_args(proxy);
        current = open_navi_lifecycle_is_current(&lease);
        open_navi_lifecycle_release(&lease);
    }
    open_navi_log(result == 0u ? LOG_NOTICE : LOG_WARNING,
             "callback adapter=%u slot=%u argc=0 status=%u class=%s",
             adapter_id, slot, result,
             kind == OPEN_NAVI_CALLBACK_CRITICAL ? "critical" : "optional");
    return current && (result == 0u || kind == OPEN_NAVI_CALLBACK_OPTIONAL);
}

static int open_navi_call_callback1(uint32_t adapter_id, uint32_t slot,
                               uint32_t value, OpenNaviCallbackClass kind)
{
    OpenNaviLifecycleLease lease;
    void *proxy;
    uint32_t result=1u;
    int current;
    if (!open_navi_lifecycle_acquire_proxy(adapter_id, slot, &lease, &proxy)) {
        return kind == OPEN_NAVI_CALLBACK_OPTIONAL;
    }
    if(adapter_id==8u&&slot==3u) result=nc_host_vehicle_subscribe(proxy,value);
    else if(adapter_id==8u&&slot==5u) result=nc_host_vehicle_request_day_night_status(proxy,value);
    else if(adapter_id==8u&&slot==6u) result=nc_host_vehicle_request_day_night_setting(proxy,value);
    else if(adapter_id==8u&&slot==11u) result=nc_host_vehicle_request_vehicle_identification_number(proxy,value);
    else if(adapter_id==9u&&slot==3u) result=nc_host_gps_subscribe(proxy,value);
    else if(adapter_id==17u&&slot==5u) result=nc_host_cluster_subscribe(proxy,value);
    else if(adapter_id==17u&&slot==7u) result=nc_host_cluster_request_tbt_settings_enable_status(proxy,value);
    current = open_navi_lifecycle_is_current(&lease);
    open_navi_lifecycle_release(&lease);
    return current && (result==0u||kind==OPEN_NAVI_CALLBACK_OPTIONAL);
}

static int open_navi_call_callback2(uint32_t adapter_id, uint32_t slot,
                               uint32_t a, uint32_t b, OpenNaviCallbackClass kind)
{
    OpenNaviLifecycleLease lease;
    void *proxy;
    uint32_t result=1u;
    int current;
    if (!open_navi_lifecycle_acquire_proxy(adapter_id, slot, &lease, &proxy)) {
        return kind == OPEN_NAVI_CALLBACK_OPTIONAL;
    }
    if(adapter_id==10u&&slot==3u) result=nc_host_audio_subscribe_audio_state(proxy,a,b);
    else if(adapter_id==10u&&slot==19u) result=nc_host_audio_request_audio_source_volume(proxy,a,b);
    current = open_navi_lifecycle_is_current(&lease);
    open_navi_lifecycle_release(&lease);
    return current && (result==0u||kind==OPEN_NAVI_CALLBACK_OPTIONAL);
}

static int open_navi_request1(uint32_t adapter, uint32_t callback_slot,
                         uint32_t provider_slot, OpenNaviCallbackClass kind)
{
    OpenNaviLifecycleLease lease;
    void *proxy;
    uint32_t handle;
    uint32_t status = 1u;
    int ok = 0;
    int current;
    if (!open_navi_lifecycle_acquire_proxy(adapter, callback_slot, &lease, &proxy))
        return kind == OPEN_NAVI_CALLBACK_OPTIONAL;
    handle=open_navi_callback_allocate_handle();
    open_navi_callback_pending_add(adapter,provider_slot,handle);
    if(adapter==8u&&callback_slot==5u) status=nc_host_vehicle_request_day_night_status(proxy,handle);
    else if(adapter==8u&&callback_slot==6u) status=nc_host_vehicle_request_day_night_setting(proxy,handle);
    else if(adapter==8u&&callback_slot==11u) status=nc_host_vehicle_request_vehicle_identification_number(proxy,handle);
    else if(adapter==17u&&callback_slot==7u) status=nc_host_cluster_request_tbt_settings_enable_status(proxy,handle);
    current = open_navi_lifecycle_is_current(&lease);
    ok = status == 0u && current;
    if(!ok && current)
        open_navi_callback_pending_cancel(adapter,provider_slot,handle);
    open_navi_lifecycle_release(&lease);
    return current && (ok || kind == OPEN_NAVI_CALLBACK_OPTIONAL);
}

static int open_navi_request2(uint32_t adapter, uint32_t callback_slot,
                         uint32_t provider_slot, uint32_t extra,
                         OpenNaviCallbackClass kind)
{
    OpenNaviLifecycleLease lease;
    void *proxy;
    uint32_t handle;
    uint32_t status = 1u;
    int ok;
    int current;
    if (!open_navi_lifecycle_acquire_proxy(adapter, callback_slot, &lease, &proxy))
        return kind == OPEN_NAVI_CALLBACK_OPTIONAL;
    handle=open_navi_callback_allocate_handle();
    open_navi_callback_pending_add(adapter,provider_slot,handle);
    if(adapter==10u&&callback_slot==19u)
        status=nc_host_audio_request_audio_source_volume(proxy,handle,extra);
    current=open_navi_lifecycle_is_current(&lease);
    ok=status==0u && current;
    if(!ok && current)
        open_navi_callback_pending_cancel(adapter,provider_slot,handle);
    open_navi_lifecycle_release(&lease);
    return current && (ok || kind == OPEN_NAVI_CALLBACK_OPTIONAL);
}

static int open_navi_signal_before_adapter_ready(uint32_t adapter_id)
{
    if (adapter_id == 10u) {
        return open_navi_call_callback2(10u,3u,1u,0x100u,OPEN_NAVI_CALLBACK_CRITICAL);
    }
    return 1;
}

static int open_navi_signal_after_adapter_ready(uint32_t adapter_id)
{
    switch (adapter_id) {
    case 8u:
        return open_navi_call_callback1(8u,3u,UINT32_MAX,OPEN_NAVI_CALLBACK_CRITICAL) &&
               open_navi_request1(8u,5u,3u,OPEN_NAVI_CALLBACK_CRITICAL) &&
               open_navi_request1(8u,6u,5u,OPEN_NAVI_CALLBACK_CRITICAL) &&
               open_navi_request1(8u,11u,12u,OPEN_NAVI_CALLBACK_OPTIONAL);
    case 10u:
        return open_navi_request2(10u,19u,17u,1u,OPEN_NAVI_CALLBACK_CRITICAL);
    case 17u:
        return open_navi_call_callback1(17u,5u,UINT32_MAX,OPEN_NAVI_CALLBACK_CRITICAL) &&
               open_navi_call_callback0(17u,6u,OPEN_NAVI_CALLBACK_CRITICAL) &&
               open_navi_request1(17u,7u,6u,OPEN_NAVI_CALLBACK_CRITICAL);
    case 9u:
        return open_navi_call_callback1(9u,3u,UINT32_MAX,OPEN_NAVI_CALLBACK_CRITICAL);
    default:
        return 1;
    }
}

uint32_t open_navi_callback_signal_audio_availability(uint32_t state)
{
    OpenNaviLifecycleLease lease;
    void *proxy;
    uint32_t attempt;
    uint32_t status;
    if(!open_navi_lifecycle_acquire_proxy(10u,10u,&lease,&proxy)) return 1u;
    if(!open_navi_callback_audio_begin(state,&attempt)) {
        open_navi_lifecycle_release(&lease);
        return 0u;
    }
    status=nc_host_audio_signal_audio_source_availability(proxy,state);
    open_navi_log(LOG_NOTICE,
             "outbound PAS callback adapter=10 slot=10 state=%u status=%u",
             state,status);
    if (open_navi_lifecycle_is_current(&lease))
        open_navi_callback_audio_complete(state,attempt,status==0u);
    open_navi_lifecycle_release(&lease);
    return status;
}

uint32_t open_navi_callback_send_cluster(const void *record_0xa5c)
{
    uint8_t owned[OPEN_NAVI_CLUSTER_RECORD_SIZE];
    void *proxy;
    OpenNaviLifecycleLease lease;
    uint32_t status;
    if (record_0xa5c == NULL ||
        !open_navi_lifecycle_acquire_proxy(17u, 3u, &lease, &proxy)) {
        return 1u;
    }
    memcpy(owned,record_0xa5c,sizeof(owned));
    status=nc_host_cluster_signal_instrument_cluster_display_data(proxy,owned);
    open_navi_log(LOG_NOTICE,
             "outbound PAS callback adapter=17 slot=3 payload_size=0xa5c route=%u navigation=%u changed=0x%04x valid=0x%04x status=%u",
             (uint32_t)owned[0],(uint32_t)owned[1],
             (unsigned int)((uint16_t)owned[0xa56] |
                 ((uint16_t)owned[0xa57] << 8)),
             (unsigned int)((uint16_t)owned[0xa58] |
                 ((uint16_t)owned[0xa59] << 8)),status);
    /* Diagnostic telemetry records accepted publications, not attempts.  A
     * concurrent or reentrant teardown invalidates this transport generation. */
    if (status == 0u &&
        open_navi_lifecycle_is_current(&lease)) {
        open_navi_callback_store_cluster(owned);
    }
    open_navi_lifecycle_release(&lease);
    return status;
}

uint32_t open_navi_callback_signal_thread_status(uint32_t thread_id,
                                            const char *thread_name,
                                            uint32_t thread_status)
{
    OpenNaviLifecycleLease lease;
    wchar_t wide[OPEN_NAVI_THREAD_NAME_LIMIT+1u];
    char normalized[OPEN_NAVI_THREAD_NAME_LIMIT+1u];
    size_t i=0u;
    void *proxy;
    uint32_t status;
    if(!open_navi_lifecycle_acquire_proxy(UINT32_MAX,10u,&lease,&proxy)) return 1u;
    while(i<OPEN_NAVI_THREAD_NAME_LIMIT&&thread_name&&thread_name[i]) {
        unsigned char byte=(unsigned char)thread_name[i];
        /* Keep the target's converted byte count bounded independently of
         * locale: every emitted wide character is single-byte ASCII. */
        normalized[i]=(char)(byte<0x80u?byte:(unsigned char)'?');
        wide[i]=(wchar_t)(unsigned char)normalized[i];
        ++i;
    }
    normalized[i]='\0';
    wide[i]=L'\0';
    status=nc_host_root_signal_thread_status(proxy,thread_id,wide,thread_status);
    open_navi_log(LOG_NOTICE,
             "outbound PAS callback root slot=10 thread_id=%u name=%.*s thread_status=%u status=%u",
             thread_id,(int)i,normalized,thread_status,status);
    if (status == 0u && open_navi_lifecycle_is_current(&lease))
        open_navi_callback_store_thread(thread_id,normalized,thread_status);
    open_navi_lifecycle_release(&lease);
    return status;
}

uint32_t open_navi_service_publish_cluster(
    const OpenNaviBackendClusterV1 *cluster)
{
    OpenNaviClusterNavigationState state;

    if (cluster == NULL ||
        cluster->abi_version != OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1 ||
        cluster->struct_size < sizeof(*cluster)) {
        return OPEN_NAVI_BACKEND_SERVICE_BAD_ABI;
    }
    if (cluster->reserved0[0] != 0u ||
        (cluster->current_road.byte_count != 0u &&
         cluster->current_road.bytes == NULL) ||
        (cluster->next_road.byte_count != 0u &&
         cluster->next_road.bytes == NULL)) {
        return OPEN_NAVI_BACKEND_SERVICE_BAD_VALUE;
    }

    memset(&state, 0, sizeof(state));
    state.valid_attributes = cluster->valid_attributes;
    state.route_guidance_state = cluster->route_guidance_state;
    state.navigation_status = cluster->navigation_status;
    state.distance_destination = cluster->distance_destination;
    state.distance_destination_unit = cluster->distance_destination_unit;
    state.eta_hour = cluster->eta_hour;
    state.eta_minute = cluster->eta_minute;
    state.rta_hour = cluster->rta_hour;
    state.rta_minute = cluster->rta_minute;
    state.distance_maneuver_tenths = cluster->distance_maneuver_tenths;
    state.distance_maneuver_unit = cluster->distance_maneuver_unit;
    state.current_road.bytes = cluster->current_road.bytes;
    state.current_road.length = cluster->current_road.byte_count;
    state.current_road.encoding = OPEN_NAVI_CLUSTER_TEXT_UTF8;
    state.lane_active = cluster->lane_active;
    state.next_road.bytes = cluster->next_road.bytes;
    state.next_road.length = cluster->next_road.byte_count;
    state.next_road.encoding = OPEN_NAVI_CLUSTER_TEXT_UTF8;
    state.bar_percent = cluster->bar_percent;
    state.compass = cluster->compass;
    state.speed_limit = cluster->speed_limit;
    state.speed_unit = cluster->speed_unit;
    state.legal_speed_limit = cluster->legal_speed_limit;
    state.legal_speed_unit = cluster->legal_speed_unit;
    state.route_guidance_symbol = cluster->route_guidance_symbol;
    return open_navi_cluster_backend_publish(&state);
}

/* Backend V6 guidance state: kept for the route-guidance status replies
 * (slot 19, polled about once a second by nv_navigation). */
uint32_t open_navi_service_publish_guidance_state(
    const OpenNaviBackendGuidanceStateV1 *guidance)
{
    int applied;

    if (guidance == NULL ||
        guidance->abi_version != OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1 ||
        guidance->struct_size < sizeof(*guidance)) {
        return OPEN_NAVI_BACKEND_SERVICE_BAD_ABI;
    }
    if (guidance->guiding != OPEN_NAVI_BACKEND_GUIDANCE_IDLE &&
        guidance->guiding != OPEN_NAVI_BACKEND_GUIDANCE_ACTIVE) {
        return OPEN_NAVI_BACKEND_SERVICE_BAD_VALUE;
    }
    applied = open_navi_callback_publish_guidance(
        guidance->guiding == OPEN_NAVI_BACKEND_GUIDANCE_ACTIVE);
    open_navi_log(LOG_NOTICE, "backend guidance state guiding=%u applied=%d",
             guidance->guiding, applied);
    return OPEN_NAVI_BACKEND_SERVICE_OK;
}

uint32_t open_navi_service_publish_audio_availability(
    const OpenNaviBackendAudioAvailabilityV1 *audio)
{
    if (audio == NULL ||
        audio->abi_version != OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1 ||
        audio->struct_size < sizeof(*audio)) {
        return OPEN_NAVI_BACKEND_SERVICE_BAD_ABI;
    }
    if (audio->availability_state < 1u ||
        audio->availability_state > 4u) {
        return OPEN_NAVI_BACKEND_SERVICE_BAD_VALUE;
    }
    return open_navi_callback_signal_audio_availability(
        audio->availability_state);
}

uint32_t open_navi_service_publish_thread_status(
    const OpenNaviBackendThreadStatusV1 *thread_status)
{
    char owned[OPEN_NAVI_THREAD_NAME_LIMIT + 1u];
    uint32_t index;

    if (thread_status == NULL ||
        thread_status->abi_version != OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1 ||
        thread_status->struct_size < sizeof(*thread_status)) {
        return OPEN_NAVI_BACKEND_SERVICE_BAD_ABI;
    }
    if (thread_status->name_byte_count > OPEN_NAVI_THREAD_NAME_LIMIT ||
        (thread_status->name_byte_count != 0u &&
         thread_status->name_ascii == NULL)) {
        return OPEN_NAVI_BACKEND_SERVICE_BAD_VALUE;
    }
    for (index = 0u; index < thread_status->name_byte_count; ++index) {
        unsigned char byte =
            (unsigned char)thread_status->name_ascii[index];
        if (byte == 0u || byte >= 0x80u)
            return OPEN_NAVI_BACKEND_SERVICE_BAD_VALUE;
        owned[index] = (char)byte;
    }
    owned[thread_status->name_byte_count] = '\0';
    return open_navi_callback_signal_thread_status(
        thread_status->thread_id, owned, thread_status->thread_status);
}

static void open_navi_create_renderer_at_stock_boundary(void)
{
    OpenNaviLifecycleLease lease;
    char diagnostic[257];
    int result;

    if (!open_navi_lifecycle_acquire(&lease)) return;
    diagnostic[0] = '\0';
    open_navi_log(LOG_NOTICE,
             "renderer create begin after_adapter=6 before_adapter=10");
    result = open_navi_renderer_create(diagnostic, sizeof(diagnostic));
    if (result == 0) {
        open_navi_log(LOG_NOTICE, "renderer create complete: %s", diagnostic);
    } else {
        /* Renderer failure must not truncate the recovered protocol worker. */
        open_navi_log(LOG_ERR, "renderer create failed result=%d: %s",
                 result, diagnostic[0] != '\0' ? diagnostic : "no detail");
    }
    open_navi_lifecycle_release(&lease);
}

static int open_navi_signal_adapter_ready_sequence(const uint8_t *adapter_ids,
                                              size_t count)
{
    size_t index;

    for (index = 0u; index < count; ++index) {
        if (open_navi_worker_should_stop() != 0) {
            return 0;
        }
        if (adapter_ids[index] == 10u) {
            open_navi_create_renderer_at_stock_boundary();
        }
        if (open_navi_worker_should_stop() != 0 ||
            !open_navi_signal_before_adapter_ready(adapter_ids[index]) ||
            !open_navi_signal_adapter_ready(adapter_ids[index]) ||
            !open_navi_signal_after_adapter_ready(adapter_ids[index])) {
            return 0;
        }
    }
    return 1;
}

static int open_navi_take_hmi_event(OpenNaviHMIEvent *event)
{
    int available = 0;

    (void)pthread_mutex_lock(&g_worker_lock);
    if (g_worker_stop == 0 && g_hmi_event_queue_count != 0u) {
        *event = g_hmi_event_queue[g_hmi_event_queue_head];
        g_hmi_event_queue_head = (g_hmi_event_queue_head + 1u) %
                                 OPEN_NAVI_HMI_EVENT_QUEUE_CAPACITY;
        --g_hmi_event_queue_count;
        if (event->type == OPEN_NAVI_HMI_EVENT_TRANSITION) {
            --g_transition_queue_count;
        }
        available = 1;
    }
    (void)pthread_mutex_unlock(&g_worker_lock);
    return available;
}

/* replyGuidanceStop(handle, result), adapter 25 callback 17 (nv_navigation
 * callback vtable 0x081ef688 entry 0x081ef6cc -> 0x080eb29c; stock
 * libNaviCore's poster 0x2a1e0c calls vtable +0x44).  nv_navigation answers
 * a pending projection stop with GuidanceStopResponse (PAS 0x1618), a
 * success only when result == 2, otherwise the VR request through PAS
 * 0x15e7; the handle word is not read there.  The stop was delivered to the
 * backend when the request arrived; like stock, the provider always answers
 * and always with result 2, also when nothing was guiding.  A failed host
 * call is logged and the worker continues (unlike the HMI replies, it does
 * not end it). */
static int open_navi_reply_guidance_stop(uint32_t handle)
{
    OpenNaviLifecycleLease lease;
    void *proxy;
    uint32_t reply_result;
    uint32_t guidance;
    uint32_t result;

    if (!open_navi_lifecycle_acquire_proxy(25u, 17u, &lease, &proxy)) {
        /* No reply proxy: drop this reply but keep serving the HMI FIFO
         * while the lifecycle is still active. */
        int active = open_navi_lifecycle_acquire(&lease);
        if (active) {
            open_navi_callback_pending_cancel(25u, 13u, handle);
            open_navi_log(LOG_ERR,
                     "VR replyGuidanceStop handle=%u dropped: no proxy",
                     handle);
            open_navi_lifecycle_release(&lease);
        }
        return active;
    }
    open_navi_callback_get_voice_state(&reply_result, &guidance);
    reply_result = OPEN_NAVI_VOICE_RESULT_OK;
    result = nc_host_voice_reply_guidance_stop(proxy, handle, reply_result);
    if (open_navi_lifecycle_is_current(&lease)) {
        if (result == 0u)
            (void)open_navi_callback_pending_complete(25u, 13u, handle);
        else
            open_navi_callback_pending_cancel(25u, 13u, handle);
    }
    open_navi_log(LOG_NOTICE,
             "VR replyGuidanceStop proxy=%p slot=17 handle=%u result=%u "
             "guidance_status=%u status=%u",
             proxy, handle, reply_result, guidance, result);
    open_navi_lifecycle_release(&lease);
    return 1;
}

static int open_navi_reply_hmi_event(const OpenNaviHMIEvent *event)
{
    OpenNaviLifecycleLease lease;
    void *hmi_proxy;
    uint32_t result;
    uint32_t slot;

    /* Requests can arrive synchronously from a host callback. Keep every
     * corresponding reply observably asynchronous and preserve FIFO order. */
    if (!open_navi_worker_delay(50u)) {
        return 0;
    }
    if (event->type == OPEN_NAVI_HMI_EVENT_GUIDANCE_STOP) {
        return open_navi_reply_guidance_stop(event->request.guidance_stop.handle);
    }
    slot = event->type == OPEN_NAVI_HMI_EVENT_TRANSITION ? 9u :
           event->type == OPEN_NAVI_HMI_EVENT_SCREEN_CHANGE ? 10u : 12u;
    if (!open_navi_lifecycle_acquire_proxy(1u, slot, &lease, &hmi_proxy)) {
        return 0;
    }
    switch (event->type) {
    case OPEN_NAVI_HMI_EVENT_TRANSITION:
        open_navi_log(LOG_NOTICE,
                 "HMI replyHMIStateTransition proxy=%p slot=9 "
                 "handle=%u display=%u visibility=%u interactivity=%u begin",
                 hmi_proxy, event->request.transition.handle,
                 event->request.transition.display,
                 event->request.transition.visibility,
                 event->request.transition.interactivity);
        result = nc_host_hmi_reply_hmi_state_transition(hmi_proxy,
                              event->request.transition.handle);
        if (result == 0u && open_navi_lifecycle_is_current(&lease)) {
            (void)pthread_mutex_lock(&g_worker_lock);
            ++g_transition_reply_count;
            (void)pthread_mutex_unlock(&g_worker_lock);
        }
        open_navi_log(LOG_NOTICE,
                 "HMI replyHMIStateTransition complete handle=%u result=%u",
                 event->request.transition.handle, result);
        break;
    case OPEN_NAVI_HMI_EVENT_SCREEN_CHANGE:
        /* Host onReplyScreenChange decodes the three payload words as handle,
         * boolean result, screen type. A zero result is stock success. */
        open_navi_log(LOG_NOTICE,
                 "HMI replyScreenChange proxy=%p slot=10 "
                 "handle=%u result=0 screen_type=%u begin",
                 hmi_proxy, event->request.screen_change.handle,
                 event->request.screen_change.screen_type);
        result = nc_host_hmi_reply_screen_change(hmi_proxy,
                               event->request.screen_change.handle, 0u,
                               event->request.screen_change.screen_type);
        open_navi_log(LOG_NOTICE,
                 "HMI replyScreenChange complete handle=%u result=%u",
                 event->request.screen_change.handle, result);
        break;
    case OPEN_NAVI_HMI_EVENT_THEME_CHANGE:
        open_navi_log(LOG_NOTICE,
                 "HMI replyScreenThemeChange proxy=%p slot=12 handle=%u begin",
                 hmi_proxy, event->request.theme_change.handle);
        result = nc_host_hmi_reply_screen_theme_change(hmi_proxy,
                               event->request.theme_change.handle);
        open_navi_log(LOG_NOTICE,
                 "HMI replyScreenThemeChange complete handle=%u result=%u",
                 event->request.theme_change.handle, result);
        break;
    default:
        open_navi_log(LOG_ERR, "unknown HMI event type=%u", (uint32_t)event->type);
        open_navi_lifecycle_release(&lease);
        return 0;
    }
    if (!open_navi_lifecycle_is_current(&lease)) result = 1u;
    open_navi_lifecycle_release(&lease);
    return result == 0u;
}

static void *open_navi_readiness_worker(void *unused)
{
    uint32_t display;

    (void)unused;
    g_readiness_worker_thread = 1;
    if (!open_navi_worker_delay(100u) ||
        !open_navi_wait_for_required_proxies()) {
        goto cleanup;
    }

    (void)pthread_mutex_lock(&g_worker_lock);
    if (g_worker_stop != 0) {
        (void)pthread_mutex_unlock(&g_worker_lock);
        goto cleanup;
    }
    if (g_si_ready_sent == 0) {
        (void)pthread_mutex_unlock(&g_worker_lock);
        if (!open_navi_emit_root_ready()) goto cleanup;
        (void)pthread_mutex_lock(&g_worker_lock);
        if (g_worker_stop == 0) g_si_ready_sent = 1;
        (void)pthread_mutex_unlock(&g_worker_lock);
    } else {
        (void)pthread_mutex_unlock(&g_worker_lock);
    }

    if (!open_navi_signal_adapter_ready_sequence(
            g_pre_hmi_ready_adapters,
                sizeof(g_pre_hmi_ready_adapters) /
                sizeof(g_pre_hmi_ready_adapters[0]))) {
        goto cleanup;
    }

    (void)pthread_mutex_lock(&g_worker_lock);
    if (g_worker_stop != 0 || g_hmi_ready_sent != 0) {
        (void)pthread_mutex_unlock(&g_worker_lock);
        goto cleanup;
    }
    (void)pthread_mutex_unlock(&g_worker_lock);
    if (!open_navi_emit_hmi0(2u)) goto cleanup;
    (void)pthread_mutex_lock(&g_worker_lock);
    if (g_worker_stop == 0) g_hmi_ready_sent = 1;
    (void)pthread_mutex_unlock(&g_worker_lock);
    if (!open_navi_emit_hmi1(3u, UINT32_MAX)) goto cleanup;

    for (display = 0u; display < 3u; ++display) {
        uint32_t handle = open_navi_allocate_hmi_handle();
        if (!open_navi_emit_hmi3(7u, handle, display, 1u) ||
            !open_navi_emit_hmi2(6u, handle, display) ||
            !open_navi_emit_hmi1(14u, display)) goto cleanup;
    }

    if (!open_navi_signal_adapter_ready_sequence(
            g_post_hmi_ready_adapters,
                sizeof(g_post_hmi_ready_adapters) /
                sizeof(g_post_hmi_ready_adapters[0]))) {
        goto cleanup;
    }

    /* adapterReady and initialization callbacks can synchronously submit HMI
     * provider requests. Stock does not reply until initialization returns. */
    for (;;) {
        OpenNaviHMIEvent event;

        if (!open_navi_take_hmi_event(&event)) {
            break;
        }
        if (!open_navi_reply_hmi_event(&event)) {
            goto cleanup;
        }
    }

    /* Continue servicing screen, theme, and transition requests in the exact
     * order in which the host submitted them. */
    for (;;) {
        OpenNaviHMIEvent event;

        (void)pthread_mutex_lock(&g_worker_lock);
        while (g_worker_stop == 0 && g_hmi_event_queue_count == 0u) {
            (void)pthread_cond_wait(&g_worker_condition, &g_worker_lock);
        }
        if (g_worker_stop != 0) {
            (void)pthread_mutex_unlock(&g_worker_lock);
            goto cleanup;
        }
        (void)pthread_mutex_unlock(&g_worker_lock);
        if (!open_navi_take_hmi_event(&event)) {
            continue;
        }
        if (!open_navi_reply_hmi_event(&event)) {
            goto cleanup;
        }
    }

cleanup:
    (void)pthread_mutex_lock(&g_worker_lock);
    g_worker_running = 0;
    (void)pthread_cond_broadcast(&g_worker_condition);
    (void)pthread_mutex_unlock(&g_worker_lock);
    open_navi_lifecycle_finish_after_worker_exit();
    g_readiness_worker_thread = 0;
    return NULL;
}

static uint32_t open_navi_signal_start(void *self)
{
    OpenNaviLifecycleLease inbound;
    OpenNaviLifecycleLease probe;
    void *unused_proxy;
    int call_si_ready = 0;
    int create_result = 0;

    (void)self;
    if (!open_navi_lifecycle_acquire(&inbound)) return 1u;

    /* In the captured stock call graph SI adapterReady is nested inside
     * signalStart.  Perform that callback synchronously when registration is
     * already complete, while retaining the worker fallback for an unusually
     * late +4 proxy write.  Never hold our mutex across a host callback. */
    (void)pthread_mutex_lock(&g_worker_lock);
    if (g_worker_stop == 0 && g_si_ready_sent == 0) {
        call_si_ready = 1;
    }
    (void)pthread_mutex_unlock(&g_worker_lock);

    if (call_si_ready != 0 &&
        open_navi_lifecycle_acquire_proxy(UINT32_MAX, 12u, &probe,
                                     &unused_proxy)) {
        open_navi_lifecycle_release(&probe);
        if (!open_navi_emit_root_ready()) {
            open_navi_lifecycle_release(&inbound);
            return 1u;
        }
        (void)pthread_mutex_lock(&g_worker_lock);
        if (g_worker_stop == 0) g_si_ready_sent = 1;
        (void)pthread_mutex_unlock(&g_worker_lock);
    }

    (void)pthread_mutex_lock(&g_worker_lock);
    if (g_worker_created == 0 && g_worker_stop == 0) {
        g_worker_running = 1;
        g_worker_epoch = inbound.epoch;
        create_result = pthread_create(&g_worker, NULL,
                                       open_navi_readiness_worker, NULL);
        if (create_result == 0) {
            g_worker_created = 1;
        } else {
            g_worker_running = 0;
        }
    }
    (void)pthread_mutex_unlock(&g_worker_lock);
    if (create_result != 0) {
        open_navi_log(LOG_ERR,
                 "signalStart readiness worker create failed error=%d",
                 create_result);
        open_navi_lifecycle_release(&inbound);
        return 1u;
    }
    if (!open_navi_lifecycle_is_current(&inbound)) create_result = 1;
    open_navi_lifecycle_release(&inbound);
    return create_result == 0 ? 0u : 1u;
}

static uint32_t open_navi_signal_start_watchdog(void *self, uint16_t interval)
{
    OpenNaviLifecycleLease lease;
    (void)self;
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    open_navi_log(LOG_NOTICE,
             "signalStartWatchdogHeartbeat interval=%u",
             (unsigned int)interval);
    open_navi_lifecycle_release(&lease);
    return 0u;
}

static uint32_t open_navi_hmi_state_transition(void *self,
                                          uint32_t handle,
                                          uint32_t display,
                                          uint32_t visibility,
                                          uint32_t interactivity,
                                          const void *window_attribute)
{
    OpenNaviInterfaceObject *adapter = (OpenNaviInterfaceObject *)self;
    OpenNaviLifecycleLease lease;
    unsigned int queue_depth = 0u;
    int accepted = 0;
    /* 32-byte native attribute: four float (x, y) points, NAV host +0x4c.
     * Only the split screen's area is used: read synchronously for
     * VISIBLE_SPLIT_SCREEN, never retained; other states leave it opaque. */
    const int split = visibility == OPEN_NAVI_BACKEND_HMI_VISIBLE_SPLIT_SCREEN &&
                      window_attribute != NULL;
    float points[8] = {0.0f};

    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    if (split)
        memcpy(points, window_attribute, sizeof(points));
    (void)pthread_mutex_lock(&g_worker_lock);
    if (g_worker_stop == 0 &&
        g_transition_queue_count < OPEN_NAVI_TRANSITION_QUEUE_CAPACITY &&
        g_hmi_event_queue_count < OPEN_NAVI_HMI_EVENT_QUEUE_CAPACITY) {
        unsigned int tail = (g_hmi_event_queue_head +
                             g_hmi_event_queue_count) %
                            OPEN_NAVI_HMI_EVENT_QUEUE_CAPACITY;
        g_hmi_event_queue[tail].type = OPEN_NAVI_HMI_EVENT_TRANSITION;
        g_hmi_event_queue[tail].request.transition.handle = handle;
        g_hmi_event_queue[tail].request.transition.display = display;
        g_hmi_event_queue[tail].request.transition.visibility = visibility;
        g_hmi_event_queue[tail].request.transition.interactivity =
            interactivity;
        ++g_hmi_event_queue_count;
        ++g_transition_queue_count;
        if (open_navi_renderer_notify_visibility_area(
                display, visibility, interactivity,
                split ? points : NULL) == 0) {
            queue_depth = g_transition_queue_count;
            accepted = 1;
            (void)pthread_cond_signal(&g_worker_condition);
        } else {
            --g_hmi_event_queue_count;
            --g_transition_queue_count;
            memset(&g_hmi_event_queue[tail], 0,
                   sizeof(g_hmi_event_queue[tail]));
            queue_depth = g_transition_queue_count;
        }
    } else {
        queue_depth = g_transition_queue_count;
    }
    (void)pthread_mutex_unlock(&g_worker_lock);

    open_navi_log(LOG_NOTICE,
             "HMI requestHMIStateTransition slot=5 self=%p adapter=%u "
             "handle=%u display=%u visibility=%u interactivity=%u "
             "window_attribute=%p points=%.0f,%.0f %.0f,%.0f %.0f,%.0f "
             "%.0f,%.0f accepted=%d queue_depth=%u",
             self,
             adapter != NULL ? adapter->adapter_id : UINT32_MAX,
             handle, display, visibility, interactivity,
             window_attribute, (double)points[0], (double)points[1],
             (double)points[2], (double)points[3], (double)points[4],
             (double)points[5], (double)points[6], (double)points[7],
             accepted, queue_depth);
    open_navi_lifecycle_release(&lease);
    return accepted != 0 ? 0u : 1u;
}

static uint32_t open_navi_hmi_screen_change(void *self,
                                       uint32_t handle,
                                       uint32_t display,
                                       uint32_t screen_type,
                                       uint32_t animation,
                                       uint32_t reserved)
{
    OpenNaviInterfaceObject *adapter = (OpenNaviInterfaceObject *)self;
    OpenNaviLifecycleLease lease;
    unsigned int queue_depth;
    int accepted = 0;

    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    (void)pthread_mutex_lock(&g_worker_lock);
    if (g_worker_stop == 0 &&
        g_hmi_event_queue_count < OPEN_NAVI_HMI_EVENT_QUEUE_CAPACITY) {
        unsigned int tail = (g_hmi_event_queue_head +
                             g_hmi_event_queue_count) %
                            OPEN_NAVI_HMI_EVENT_QUEUE_CAPACITY;
        g_hmi_event_queue[tail].type = OPEN_NAVI_HMI_EVENT_SCREEN_CHANGE;
        g_hmi_event_queue[tail].request.screen_change.handle = handle;
        g_hmi_event_queue[tail].request.screen_change.display = display;
        g_hmi_event_queue[tail].request.screen_change.screen_type = screen_type;
        g_hmi_event_queue[tail].request.screen_change.animation = animation;
        g_hmi_event_queue[tail].request.screen_change.reserved = reserved;
        ++g_hmi_event_queue_count;
        if (open_navi_renderer_notify_screen(display, screen_type,
                                        animation) == 0) {
            accepted = 1;
            (void)pthread_cond_signal(&g_worker_condition);
        } else {
            --g_hmi_event_queue_count;
            memset(&g_hmi_event_queue[tail], 0,
                   sizeof(g_hmi_event_queue[tail]));
        }
    }
    queue_depth = g_hmi_event_queue_count;
    (void)pthread_mutex_unlock(&g_worker_lock);

    open_navi_log(LOG_NOTICE,
             "HMI requestScreenChange slot=9 self=%p adapter=%u "
             "handle=%u display=%u screen_type=%u animation=%u reserved=%u "
             "accepted=%d queue_depth=%u",
             self, adapter != NULL ? adapter->adapter_id : UINT32_MAX,
             handle, display, screen_type, animation, reserved,
             accepted, queue_depth);
    open_navi_lifecycle_release(&lease);
    return accepted != 0 ? 0u : 1u;
}

static uint32_t open_navi_hmi_screen_theme_change(void *self,
                                             uint32_t handle,
                                             const void *theme_name)
{
    OpenNaviInterfaceObject *adapter = (OpenNaviInterfaceObject *)self;
    OpenNaviLifecycleLease lease;
    unsigned int queue_depth;
    int accepted = 0;

    if (!open_navi_lifecycle_acquire(&lease)) return 1u;
    (void)pthread_mutex_lock(&g_worker_lock);
    if (g_worker_stop == 0 &&
        g_hmi_event_queue_count < OPEN_NAVI_HMI_EVENT_QUEUE_CAPACITY) {
        unsigned int tail = (g_hmi_event_queue_head +
                             g_hmi_event_queue_count) %
                            OPEN_NAVI_HMI_EVENT_QUEUE_CAPACITY;
        g_hmi_event_queue[tail].type = OPEN_NAVI_HMI_EVENT_THEME_CHANGE;
        g_hmi_event_queue[tail].request.theme_change.handle = handle;
        ++g_hmi_event_queue_count;
        accepted = 1;
        (void)pthread_cond_signal(&g_worker_condition);
    }
    queue_depth = g_hmi_event_queue_count;
    (void)pthread_mutex_unlock(&g_worker_lock);

    /* The theme pointer is owned by the host.  It is logged synchronously but
     * is neither dereferenced nor retained by the asynchronous reply queue. */
    open_navi_log(LOG_NOTICE,
             "HMI requestScreenThemeChange slot=10 self=%p adapter=%u "
             "handle=%u theme_name=%p accepted=%d queue_depth=%u",
             self, adapter != NULL ? adapter->adapter_id : UINT32_MAX,
             handle, theme_name, accepted, queue_depth);
    open_navi_lifecycle_release(&lease);
    return accepted != 0 ? 0u : 1u;
}

static uint32_t nc_provider_get_adapter(void *self,
                                 uint32_t adapter,
                                 void **interface_out)
{
    OpenNaviLifecycleLease lease;
    (void)self;
    if (interface_out != NULL) {
        *interface_out = NULL;
    }
    if (!open_navi_lifecycle_acquire(&lease)) return 1u;

    if (interface_out == NULL) {
        open_navi_log(LOG_ERR, "getAdapter(%u) called with NULL output", adapter);
        open_navi_lifecycle_release(&lease);
        return 1u;
    }

    /* These three enums are rejected by both captured QR39.5/QR39.6 paths. */
    if (adapter == OPEN_NAVI_UNAVAILABLE_ADAPTER_21 ||
        adapter == OPEN_NAVI_UNAVAILABLE_ADAPTER_27 ||
        adapter == OPEN_NAVI_UNAVAILABLE_ADAPTER_28) {
        open_navi_log(LOG_NOTICE, "getAdapter(%u) unavailable", adapter);
        open_navi_lifecycle_release(&lease);
        return 1u;
    }

    if (adapter < OPEN_NAVI_ADAPTER_COUNT) {
        *interface_out = &g_adapters[adapter];
    } else {
        *interface_out = &g_overflow_adapter;
        open_navi_log(LOG_WARNING,
                 "getAdapter(%u) exceeds stable per-enum provider range",
                 adapter);
    }

    open_navi_log(LOG_NOTICE, "getAdapter(%u) -> %p", adapter, *interface_out);
    open_navi_lifecycle_release(&lease);
    return 0u;
}

uint32_t panaVersionCheck(
    PanaServerVersionHeadUnit server,
    PanaClientVersionNavigationSoftwareModule *client,
    uint32_t *compatibility)
{
    if (client != NULL) {
        client->api_major = (uint16_t)PANA_API_MAJOR;
        client->api_minor = (uint16_t)PANA_API_MINOR;
        client->version_a = 0u;
        client->version_b = 0u;
        client->packed_version = 0u;
        client->version_text = g_version_text;
    }
    if (compatibility != NULL) {
        /* The loader independently checks its table; advertise exact support. */
        *compatibility = 1u;
    }

    open_navi_log(LOG_NOTICE,
             "panaVersionCheck server=%08x/%08x/%08x/%08x/%08x/%08x",
             (unsigned int)server.word[0], (unsigned int)server.word[1],
             (unsigned int)server.word[2], (unsigned int)server.word[3],
             (unsigned int)server.word[4], (unsigned int)server.word[5]);
    return 0u;
}

uint32_t panaInitialize(const void *init_record)
{
    const PanaInitializationData *parameters =
        (const PanaInitializationData *)init_record;

    if (parameters == NULL) {
        open_navi_log(LOG_ERR, "panaInitialize called with NULL parameter block");
        return 1u;
    }
    (void)pthread_mutex_lock(&g_lifecycle_lock);
    if (g_state != OPEN_NAVI_COLD) {
        int rejected_state = g_state;
        (void)pthread_mutex_unlock(&g_lifecycle_lock);
        open_navi_log(LOG_WARNING, "panaInitialize rejected state=%d",
                 rejected_state);
        return 1u;
    }
    g_state = OPEN_NAVI_STARTING;
    ++g_lifecycle_epoch;
    if (g_lifecycle_epoch == 0u) ++g_lifecycle_epoch;
    prepare_objects();
    open_navi_clear_host_proxies_locked();
    (void)pthread_mutex_unlock(&g_lifecycle_lock);

    /* Log pointer values only. The provider does not own or dereference paths. */
    open_navi_log(LOG_NOTICE,
             "panaInitialize paths=%p/%p/%p/%p/%p/%p",
             parameters->path0, parameters->path1,
             parameters->path2, parameters->path3,
             parameters->path9, parameters->path10);
    (void)pthread_mutex_lock(&g_worker_lock);
    g_worker_stop = 0;
    g_worker_created = 0;
    g_worker_running = 0;
    g_worker_epoch = g_lifecycle_epoch;
    g_si_ready_sent = 0;
    g_hmi_ready_sent = 0;
    g_hmi_event_queue_head = 0u;
    g_hmi_event_queue_count = 0u;
    g_transition_queue_count = 0u;
    g_transition_reply_count = 0u;
    g_gps_call_count = 0u;
    g_gps_last_record_address = (uintptr_t)0u;
    (void)pthread_mutex_unlock(&g_worker_lock);
    open_navi_gps_backend_reset();
    open_navi_scalar_backend_reset();
    open_navi_api_telemetry_reset();
    open_navi_pointer_backend_reset();
    open_navi_callback_backend_reset();
    open_navi_cluster_backend_reset();
    open_navi_read_guidance_status_marker();
    /* The backend choice is process-lifetime and must be finalized while the
     * renderer is still COLD. Failure is deliberately non-fatal: bootstrap
     * leaves the built-in magenta backend selected. */
    open_navi_backend_loader_bootstrap();
    open_navi_renderer_prepare_lifecycle();
    (void)pthread_mutex_lock(&g_lifecycle_lock);
    g_state = OPEN_NAVI_ACTIVE;
    (void)pthread_cond_broadcast(&g_lifecycle_condition);
    (void)pthread_mutex_unlock(&g_lifecycle_lock);
    if (OPEN_NAVI_QR39_CONSUMER_SETTLE_MS != 0u) {
        open_navi_log(LOG_NOTICE, "panaInitialize consumer settle=%u ms",
                 OPEN_NAVI_QR39_CONSUMER_SETTLE_MS);
        (void)open_navi_worker_delay(OPEN_NAVI_QR39_CONSUMER_SETTLE_MS);
    }
    return 0u;
}

void *panaGetSoftwareInfrastructure(void)
{
    OpenNaviLifecycleLease lease;
    int state;
    if (!open_navi_lifecycle_acquire(&lease)) return NULL;
    (void)pthread_mutex_lock(&g_lifecycle_lock);
    state = g_state;
    (void)pthread_mutex_unlock(&g_lifecycle_lock);
    open_navi_log(LOG_NOTICE,
             "panaGetSoftwareInfrastructure -> %p (state=%d)",
             (const void *)&g_root, state);
    open_navi_lifecycle_release(&lease);
    return (void *)&g_root;
}

static void open_navi_finish_deinitialize(void)
{
    pthread_t worker;
    int join_worker;
    int self_worker;

    (void)pthread_mutex_lock(&g_worker_lock);
    g_worker_stop = 1;
    (void)pthread_cond_broadcast(&g_worker_condition);
    worker = g_worker;
    join_worker = g_worker_created;
    self_worker = join_worker != 0 && pthread_equal(pthread_self(), worker);
    while (g_worker_running != 0 && !self_worker) {
        (void)pthread_cond_wait(&g_worker_condition, &g_worker_lock);
    }
    if (self_worker) {
        (void)pthread_detach(worker);
        g_worker_created = 0;
    }
    (void)pthread_mutex_unlock(&g_worker_lock);
    if (join_worker != 0 && !self_worker) {
        (void)pthread_join(worker, NULL);
        (void)pthread_mutex_lock(&g_worker_lock);
        g_worker_created = 0;
        (void)pthread_mutex_unlock(&g_worker_lock);
        open_navi_log(LOG_NOTICE, "readiness worker joined");
    }
    (void)pthread_mutex_lock(&g_worker_lock);
    g_worker_epoch = 0u;
    g_si_ready_sent = 0;
    g_hmi_ready_sent = 0;
    g_hmi_event_queue_head = 0u;
    g_hmi_event_queue_count = 0u;
    g_transition_queue_count = 0u;
    g_transition_reply_count = 0u;
    g_gps_call_count = 0u;
    g_gps_last_record_address = (uintptr_t)0u;
    (void)pthread_mutex_unlock(&g_worker_lock);
    open_navi_renderer_destroy();
    open_navi_renderer_end_lifecycle();
    open_navi_log(LOG_NOTICE, "renderer destroyed");
    open_navi_gps_backend_reset();
    open_navi_scalar_backend_reset();
    open_navi_api_telemetry_reset();
    open_navi_pointer_backend_reset();
    open_navi_callback_backend_reset();
    open_navi_cluster_backend_reset();
}

void panaDeinitialize(void)
{
    int finish = 0;

    (void)pthread_mutex_lock(&g_lifecycle_lock);
    while (g_state == OPEN_NAVI_STARTING) {
        (void)pthread_cond_wait(&g_lifecycle_condition, &g_lifecycle_lock);
    }
    if (g_state == OPEN_NAVI_COLD) {
        (void)pthread_mutex_unlock(&g_lifecycle_lock);
        return;
    }
    if (g_state == OPEN_NAVI_STOPPING) {
        if (g_lifecycle_depth != 0u) {
            g_deinitialize_deferred = 1;
            while (g_lifecycle_inflight > g_lifecycle_depth) {
                (void)pthread_cond_wait(&g_lifecycle_condition,
                                        &g_lifecycle_lock);
            }
            (void)pthread_mutex_unlock(&g_lifecycle_lock);
            return;
        }
        while (g_state == OPEN_NAVI_STOPPING) {
            (void)pthread_cond_wait(&g_lifecycle_condition,
                                    &g_lifecycle_lock);
        }
        (void)pthread_mutex_unlock(&g_lifecycle_lock);
        return;
    }
    g_state = OPEN_NAVI_STOPPING;
    if (g_lifecycle_depth != 0u) {
        g_deinitialize_deferred = 1;
        while (g_lifecycle_inflight > g_lifecycle_depth) {
            (void)pthread_cond_wait(&g_lifecycle_condition,
                                    &g_lifecycle_lock);
        }
        (void)pthread_mutex_unlock(&g_lifecycle_lock);
        return;
    }
    while (g_lifecycle_inflight != 0u) {
        (void)pthread_cond_wait(&g_lifecycle_condition, &g_lifecycle_lock);
    }
    if (g_deinitialize_finishing == 0) {
        g_deinitialize_finishing = 1;
        finish = 1;
    }
    if (finish == 0) {
        while (g_state == OPEN_NAVI_STOPPING) {
            (void)pthread_cond_wait(&g_lifecycle_condition,
                                    &g_lifecycle_lock);
        }
    }
    (void)pthread_mutex_unlock(&g_lifecycle_lock);
    if (finish) open_navi_lifecycle_finish_claimed();
}
