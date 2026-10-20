#ifndef OPEN_NAVI_RENDERER_H
#define OPEN_NAVI_RENDERER_H

#include <stddef.h>
#include <stdint.h>

#include "open_navi_gps_backend.h"
#include <open_navicore/backend.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { OPEN_NAVI_RENDERER_DISPLAY_COUNT = 3 };

int open_navi_renderer_create(char *diagnostic, size_t diagnostic_capacity);
void open_navi_renderer_destroy(void);
void open_navi_renderer_prepare_lifecycle(void);
void open_navi_renderer_end_lifecycle(void);
int open_navi_renderer_notify_visibility(uint32_t display, uint32_t visibility,
                                    uint32_t interactivity);
/* As above, with the native window attribute (four float x,y points). */
int open_navi_renderer_notify_visibility_area(uint32_t display,
                                              uint32_t visibility,
                                              uint32_t interactivity,
                                              const float *points);
int open_navi_renderer_notify_screen(uint32_t display, uint32_t screen_type,
                                uint32_t animation);
int open_navi_renderer_notify_gps(const OpenNaviGpsSnapshot *snapshot);
int open_navi_renderer_notify_vehicle_status(uint32_t source_slot,
                                        uint32_t request_handle,
                                        uint8_t value);
int open_navi_renderer_notify_vehicle_setting(uint32_t source_slot,
                                         uint32_t request_handle,
                                         uint8_t value);
int open_navi_renderer_notify_vehicle_speed(uint32_t source_slot,
                                       uint32_t speed_bits);
int open_navi_renderer_notify_gps_interval(uint32_t source_slot,
                                      uint32_t interval_ms);
int open_navi_renderer_notify_audio(uint32_t source_slot,
                               uint32_t request_handle,
                               uint32_t audio_source,
                               uint8_t volume);
int open_navi_renderer_notify_internationalization(
    uint32_t source_slot, uint32_t request_handle,
    const uint8_t settings_raw[4], int settings_present);
int open_navi_renderer_notify_cluster_display(uint32_t source_slot,
                                         uint32_t request_handle,
                                         uint8_t enabled_raw);
int open_navi_renderer_notify_driver_distraction(uint32_t source_slot,
                                            uint32_t request_handle,
                                            uint32_t status_raw);
/* Queues a guidance stop request for a V6 backend: 0 queued, 1 the
 * installed/active backend has no guidance stop input (below V6), -1 not
 * accepting (no lifecycle or stopping). */
int open_navi_renderer_notify_guidance_stop(uint32_t source_slot,
                                            uint32_t request_handle);

/* The installed backend takes OpenNaviBackendPointerV2 contacts (every
 * touch contact, V2 payloads); otherwise only the first contact, as V1.
 * Set by the plugin loader before install; rejected unless cold. */
int open_navi_renderer_set_pointer_contacts(int enabled);
int open_navi_renderer_set_window_area(int enabled);

#if defined(OPEN_NAVI_TESTING)
int open_navi_renderer_test_submit_contact(uint32_t action, int32_t x, int32_t y,
                                      uint32_t buttons, uint32_t contact,
                                      uint32_t count);
int open_navi_renderer_test_submit_pointer(uint32_t action, int32_t x, int32_t y,
                                      uint32_t buttons);
uint32_t open_navi_renderer_test_pointer_accepted(void);
uint32_t open_navi_renderer_test_pointer_dropped(void);
typedef struct {
    uint32_t accepted;
    uint32_t dropped;
    uint32_t move_coalesced;
    uint32_t move_dropped;
    uint32_t edge_dropped;
    uint32_t move_evicted;
    uint32_t gesture_pairs_evicted;
    uint32_t platform_ignored;
    uint32_t platform_errors;
    uint32_t platform_window_lost;
} OpenNaviRendererPointerQueueStats;
void open_navi_renderer_test_pointer_stats(
    OpenNaviRendererPointerQueueStats *stats_out);
uint32_t open_navi_renderer_test_visibility_unsupported(void);
uint32_t open_navi_renderer_test_screen_unsupported(void);
int open_navi_renderer_test_pending_visibility(
    uint32_t display, OpenNaviBackendVisibilityV1 *value_out);
int open_navi_renderer_test_pending_screen(
    uint32_t display, OpenNaviBackendScreenV1 *value_out);
#endif

#if defined(OPEN_NAVI_RENDERER_STUB)
enum open_navi_renderer_test_event {
    OPEN_NAVI_RENDERER_TEST_CREATE = 1,
    OPEN_NAVI_RENDERER_TEST_DESTROY = 2
};

unsigned int open_navi_renderer_stub_create_count(void);
unsigned int open_navi_renderer_stub_destroy_count(void);
void open_navi_renderer_stub_reset(void);
void open_navi_renderer_stub_set_create_result(int result);
void open_navi_renderer_stub_set_begin_result(int result);
void open_navi_renderer_test_observe(enum open_navi_renderer_test_event event);
int open_navi_renderer_stub_context_current(void);
typedef struct {
    uint32_t type;
    uintptr_t window;
    int32_t position_x;
    int32_t position_y;
    int32_t source_x;
    int32_t source_y;
    uint32_t buttons;
    uint32_t touch_id;
    uint8_t position_valid;
    uint8_t source_valid;
    uint8_t buttons_valid;
    uint8_t touch_id_valid;
} OpenNaviRendererStubRawEvent;
enum OpenNaviRendererStubRawType {
    OPEN_NAVI_RENDERER_STUB_RAW_POINTER = 1,
    OPEN_NAVI_RENDERER_STUB_RAW_TOUCH = 2,
    OPEN_NAVI_RENDERER_STUB_RAW_MOVE = 3,
    OPEN_NAVI_RENDERER_STUB_RAW_RELEASE = 4
};
typedef struct {
    uint32_t accepted;
    uint32_t foreign_window;
    uint32_t secondary_touch;
    uint32_t property_errors;
    uint32_t coordinate_drops;
    uint32_t out_of_bounds;
    uint32_t ignored;
    uint32_t duplicate_class;
} OpenNaviRendererStubInputStats;
int open_navi_renderer_stub_queue_raw_input(
    const OpenNaviRendererStubRawEvent *event);
uintptr_t open_navi_renderer_stub_input_window(void);
void open_navi_renderer_stub_set_identity_coordinates(int enabled);
void open_navi_renderer_stub_set_poll_result(int result);
void open_navi_renderer_stub_set_repeating_poll_result(int result);
void open_navi_renderer_stub_input_stats(OpenNaviRendererStubInputStats *stats_out);
#endif

#ifdef __cplusplus
}
#endif

#endif
