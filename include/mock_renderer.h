#ifndef MOCK_RENDERER_H
#define MOCK_RENDERER_H

#include <stddef.h>
#include <stdint.h>

#include "mock_gps_backend.h"
#include <open_navicore/backend.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MOCK_RENDERER_DISPLAY_COUNT = 3 };

int mock_renderer_create(char *diagnostic, size_t diagnostic_capacity);
void mock_renderer_destroy(void);
void mock_renderer_prepare_lifecycle(void);
void mock_renderer_end_lifecycle(void);
int mock_renderer_notify_visibility(uint32_t display, uint32_t visibility,
                                    uint32_t interactivity);
int mock_renderer_notify_screen(uint32_t display, uint32_t screen_type,
                                uint32_t animation);
int mock_renderer_notify_gps(const MockGpsSnapshot *snapshot);
int mock_renderer_notify_vehicle_status(uint32_t source_slot,
                                        uint32_t request_handle,
                                        uint8_t value);
int mock_renderer_notify_vehicle_setting(uint32_t source_slot,
                                         uint32_t request_handle,
                                         uint8_t value);
int mock_renderer_notify_vehicle_speed(uint32_t source_slot,
                                       uint32_t speed_bits);
int mock_renderer_notify_gps_interval(uint32_t source_slot,
                                      uint32_t interval_ms);
int mock_renderer_notify_audio(uint32_t source_slot,
                               uint32_t request_handle,
                               uint32_t audio_source,
                               uint8_t volume);
int mock_renderer_notify_internationalization(
    uint32_t source_slot, uint32_t request_handle,
    const uint8_t settings_raw[4], int settings_present);
int mock_renderer_notify_cluster_display(uint32_t source_slot,
                                         uint32_t request_handle,
                                         uint8_t enabled_raw);
int mock_renderer_notify_driver_distraction(uint32_t source_slot,
                                            uint32_t request_handle,
                                            uint32_t status_raw);

#if defined(MOCK_NAVICORE_TESTING)
int mock_renderer_test_submit_pointer(uint32_t action, int32_t x, int32_t y,
                                      uint32_t buttons);
uint32_t mock_renderer_test_pointer_accepted(void);
uint32_t mock_renderer_test_pointer_dropped(void);
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
} MockRendererPointerQueueStats;
void mock_renderer_test_pointer_stats(
    MockRendererPointerQueueStats *stats_out);
uint32_t mock_renderer_test_visibility_unsupported(void);
uint32_t mock_renderer_test_screen_unsupported(void);
int mock_renderer_test_pending_visibility(
    uint32_t display, MockNavBackendVisibilityV1 *value_out);
int mock_renderer_test_pending_screen(
    uint32_t display, MockNavBackendScreenV1 *value_out);
#endif

#if defined(MOCK_NAVICORE_RENDERER_STUB)
enum mock_renderer_test_event {
    MOCK_RENDERER_TEST_CREATE = 1,
    MOCK_RENDERER_TEST_DESTROY = 2
};

unsigned int mock_renderer_stub_create_count(void);
unsigned int mock_renderer_stub_destroy_count(void);
void mock_renderer_stub_reset(void);
void mock_renderer_stub_set_create_result(int result);
void mock_renderer_stub_set_begin_result(int result);
void mock_renderer_test_observe(enum mock_renderer_test_event event);
int mock_renderer_stub_context_current(void);
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
} MockRendererStubRawEvent;
enum MockRendererStubRawType {
    MOCK_RENDERER_STUB_RAW_POINTER = 1,
    MOCK_RENDERER_STUB_RAW_TOUCH = 2,
    MOCK_RENDERER_STUB_RAW_MOVE = 3,
    MOCK_RENDERER_STUB_RAW_RELEASE = 4
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
} MockRendererStubInputStats;
int mock_renderer_stub_queue_raw_input(
    const MockRendererStubRawEvent *event);
uintptr_t mock_renderer_stub_input_window(void);
void mock_renderer_stub_set_identity_coordinates(int enabled);
void mock_renderer_stub_set_poll_result(int result);
void mock_renderer_stub_set_repeating_poll_result(int result);
void mock_renderer_stub_input_stats(MockRendererStubInputStats *stats_out);
#endif

#ifdef __cplusplus
}
#endif

#endif
