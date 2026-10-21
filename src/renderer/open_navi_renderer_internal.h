#ifndef OPEN_NAVI_RENDERER_INTERNAL_H
#define OPEN_NAVI_RENDERER_INTERNAL_H

/* Renderer state shared by the open_navi_renderer*.c files. Not part of the
 * public API: include open_navi_renderer.h for that. */

#include "open_navi_renderer.h"

#include <pthread.h>
#include <stdint.h>

enum {
    OPEN_NAVI_POINTER_QUEUE_CAPACITY = 32u,
    OPEN_NAVI_POINTER_EDGE_RESERVE = 2u,
    OPEN_NAVI_PLATFORM_DRAIN_LIMIT = 16u,
    OPEN_NAVI_PLATFORM_POLL_MILLISECONDS = 10u,
    OPEN_NAVI_RENDERER_CANCELLED = -3
};

typedef enum {
    RENDER_EVENT_VISIBILITY = 1,
    RENDER_EVENT_SCREEN = 2,
    RENDER_EVENT_GPS = 3,
    RENDER_EVENT_VEHICLE = 4,
    RENDER_EVENT_GPS_INTERVAL = 5,
    RENDER_EVENT_AUDIO = 6,
    RENDER_EVENT_INTERNATIONALIZATION = 7,
    RENDER_EVENT_CLUSTER_DISPLAY = 8,
    RENDER_EVENT_DRIVER_DISTRACTION = 9,
    RENDER_EVENT_POINTER = 10,
    RENDER_EVENT_FRAME = 11,
    RENDER_EVENT_GUIDANCE_STOP = 12
} RenderEventType;

typedef struct {
    uint64_t sequence;
    RenderEventType type;
    union {
        OpenNaviBackendVisibilityV2 visibility;  /* V1 prefix unless window area */
        OpenNaviBackendScreenV1 screen;
        OpenNaviBackendGpsV1 gps;
        OpenNaviBackendVehicleV2 vehicle;
        OpenNaviBackendGpsIntervalV2 gps_interval;
        OpenNaviBackendAudioV2 audio;
        OpenNaviBackendInternationalizationV1 internationalization;
        OpenNaviBackendClusterDisplayV1 cluster_display;
        OpenNaviBackendDriverDistractionV1 driver_distraction;
        OpenNaviBackendGuidanceStopV1 guidance_stop;
        OpenNaviBackendPointerV2 pointer;  /* V1 backends get a V1 copy */
    } data;
} RenderEvent;

typedef enum {
    RENDERER_COLD = 0,
    RENDERER_STARTING = 1,
    RENDERER_RUNNING = 2,
    RENDERER_STOPPING = 3
} RendererState;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendVisibilityV2 value;  /* V1 header; area for V2 plugins */
} VisibilitySlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendScreenV1 value;
} ScreenSlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendGpsV1 value;
} GpsSlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendVehicleV2 value;
} VehicleSlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendGpsIntervalV2 value;
} GpsIntervalSlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendAudioV2 value;
} AudioSlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendInternationalizationV1 value;
} InternationalizationSlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendClusterDisplayV1 value;
} ClusterDisplaySlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendDriverDistractionV1 value;
} DriverDistractionSlot;

typedef struct {
    int pending;
    uint64_t sequence;
    OpenNaviBackendGuidanceStopV1 value;
} GuidanceStopSlot;

/* The backend as installed by its plugin and as activated for a run, each at
 * every ABI level it implements. */
typedef struct {
    OpenNaviBackendV1 installed;
    OpenNaviBackendV2 installed_v2;
    OpenNaviBackendV3 installed_v3;
    OpenNaviBackendV4 installed_v4;
    OpenNaviBackendV5 installed_v5;
    OpenNaviBackendV6 installed_v6;
    uint32_t installed_version;
    OpenNaviBackendV1 active;
    OpenNaviBackendV2 active_v2;
    OpenNaviBackendV3 active_v3;
    OpenNaviBackendV4 active_v4;
    OpenNaviBackendV5 active_v5;
    OpenNaviBackendV6 active_v6;
    uint32_t active_version;
} BackendTable;

/* Pointer events waiting for the render thread, and what became of the rest. */
typedef struct {
    RenderEvent queue[OPEN_NAVI_POINTER_QUEUE_CAPACITY];
    unsigned int head;
    unsigned int count;
    uint32_t accepted;
    uint32_t dropped;
    uint32_t move_coalesced;
    uint32_t move_dropped;
    uint32_t edge_dropped;
    uint32_t move_evicted;
    uint32_t gesture_pairs_evicted;
} PointerQueue;

/* Events the platform layer reported but the renderer could not act on. */
typedef struct {
    uint32_t platform_ignored;
    uint32_t platform_errors;
    uint32_t platform_window_lost;
    uint32_t visibility_unsupported;
    uint32_t screen_unsupported;
} PlatformCounters;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    pthread_t thread;
    RendererState state;
    int thread_created;
    int join_claimed;
    int thread_detached;
    int stop;
    int startup_done;
    int start_result;
    int accepting_state;
    uintptr_t next_service_token;
    uintptr_t active_service_token;
    int outbound_services_active;
    uint64_t next_event_sequence;
    int frame_pending;
    uint64_t frame_event_sequence;
    uint64_t frame_sequence;
    BackendTable backend;
    OpenNaviBackendRenderTargetV1 target;
    VisibilitySlot visibility[OPEN_NAVI_RENDERER_DISPLAY_COUNT];
    ScreenSlot screen[OPEN_NAVI_RENDERER_DISPLAY_COUNT];
    GpsSlot gps;
    VehicleSlot vehicle;
    GpsIntervalSlot gps_interval;
    AudioSlot audio;
    InternationalizationSlot internationalization;
    ClusterDisplaySlot cluster_display;
    DriverDistractionSlot driver_distraction;
    GuidanceStopSlot guidance_stop;
    PointerQueue pointer;
    int pointer_contacts;
    int window_area;
    PlatformCounters counters;
    char diagnostic[257];
} RendererRuntime;

/* The one renderer runtime. Every access holds g_runtime.lock. */
extern RendererRuntime g_runtime;

/* open_navi_renderer_backend_table.c */
void open_navi_renderer_activate_installed_locked(void);

#endif
