#ifndef MOCK_SCREEN_INPUT_H
#define MOCK_SCREEN_INPUT_H

#include <stdint.h>

#include <open_navicore/backend.h>

enum MockScreenRawType {
    MOCK_SCREEN_RAW_POINTER = 1,
    MOCK_SCREEN_RAW_TOUCH = 2,
    MOCK_SCREEN_RAW_MOVE = 3,
    MOCK_SCREEN_RAW_RELEASE = 4
};

enum MockScreenInputClass {
    MOCK_SCREEN_INPUT_CLASS_NONE = 0,
    MOCK_SCREEN_INPUT_CLASS_POINTER = 1,
    MOCK_SCREEN_INPUT_CLASS_TOUCH = 2
};

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
} MockScreenRawEvent;

typedef struct {
    uintptr_t target_window;
    uint32_t target_width;
    uint32_t target_height;
    uint32_t pointer_buttons;
    uint32_t active_touch_id;
    int32_t last_x;
    int32_t last_y;
    uint8_t identity_coordinates;
    uint8_t touch_active;
    uint8_t last_position_valid;
    uint8_t input_class;
    uint32_t accepted;
    uint32_t foreign_window;
    uint32_t secondary_touch;
    uint32_t property_errors;
    uint32_t coordinate_drops;
    uint32_t out_of_bounds;
    uint32_t ignored;
    uint32_t duplicate_class;
    /* Multi-touch (set per poll by the renderer when the backend wants
     * contacts): a second touch contact is tracked in slot 1 instead of
     * being counted in secondary_touch. */
    uint8_t multi_contact;
    uint8_t second_active;
    uint8_t second_position_valid;
    uint8_t reserved0;
    uint32_t second_touch_id;
    int32_t second_last_x;
    int32_t second_last_y;
} MockScreenInputState;

void mock_screen_input_reset(MockScreenInputState *state,
                             uintptr_t target_window,
                             int identity_coordinates,
                             uint32_t target_width,
                             uint32_t target_height);
/* pointer_out is a V2 payload (V1 fields plus the contact). Without
 * multi_contact every accepted event is contact 0. */
int mock_screen_input_decode(MockScreenInputState *state,
                             const MockScreenRawEvent *raw,
                             MockNavBackendPointerV2 *pointer_out);
/* Convert a decoder result to the renderer-platform poll contract. A decoder
 * result of zero still means that one raw event was consumed. */
int mock_screen_input_platform_result(int decode_result);

#endif
