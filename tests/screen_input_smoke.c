#include "../src/mock_screen_input.h"

#include <assert.h>
#include <string.h>

static MockScreenRawEvent raw_event(uint32_t type, uintptr_t window)
{
    MockScreenRawEvent raw;
    memset(&raw, 0, sizeof(raw));
    raw.type = type;
    raw.window = window;
    return raw;
}

int main(void)
{
    MockScreenInputState state;
    MockScreenRawEvent raw;
    MockNavBackendPointerV1 pointer;

    assert(mock_screen_input_platform_result(0) == 2);
    assert(mock_screen_input_platform_result(1) == 1);
    assert(mock_screen_input_platform_result(3) == 3);
    assert(mock_screen_input_platform_result(-1) == -1);

    mock_screen_input_reset(&state, 0x1234u, 0, 800u, 480u);
    raw = raw_event(MOCK_SCREEN_RAW_TOUCH, 0x9999u);
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 0);
    assert(state.foreign_window == 1u);

    raw = raw_event(MOCK_SCREEN_RAW_TOUCH, 0x1234u);
    raw.source_valid = 1u;
    raw.source_x = 10;
    raw.source_y = 20;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 0);
    assert(state.property_errors == 1u);

    raw.touch_id_valid = 1u;
    raw.touch_id = 7u;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    assert(pointer.action == MOCK_NAV_BACKEND_POINTER_PRESS);
    assert(pointer.x == 10 && pointer.y == 20);
    assert(sizeof(pointer) == 24u);

    raw.touch_id = 8u;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 0);
    assert(state.secondary_touch == 1u);

    raw = raw_event(MOCK_SCREEN_RAW_MOVE, 0x1234u);
    raw.touch_id_valid = 1u;
    raw.touch_id = 7u;
    raw.source_valid = 1u;
    raw.source_x = 11;
    raw.source_y = 21;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    assert(pointer.action == MOCK_NAV_BACKEND_POINTER_MOVE);

    raw = raw_event(MOCK_SCREEN_RAW_RELEASE, 0x1234u);
    raw.touch_id_valid = 1u;
    raw.touch_id = 7u;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    assert(pointer.action == MOCK_NAV_BACKEND_POINTER_RELEASE);
    assert(pointer.x == 11 && pointer.y == 21);
    assert(state.property_errors == 2u);

    mock_screen_input_reset(&state, 0x1234u, 1, 800u, 480u);
    raw = raw_event(MOCK_SCREEN_RAW_POINTER, 0x1234u);
    raw.position_valid = 1u;
    raw.position_x = 30;
    raw.position_y = 40;
    raw.buttons_valid = 1u;
    raw.buttons = 1u;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    assert(pointer.action == MOCK_NAV_BACKEND_POINTER_PRESS);
    assert(pointer.x == 30 && pointer.y == 40);
    raw.position_x = 31;
    raw.buttons = 1u;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    assert(pointer.action == MOCK_NAV_BACKEND_POINTER_MOVE);
    raw.buttons = 0u;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    assert(pointer.action == MOCK_NAV_BACKEND_POINTER_RELEASE);
    assert(pointer.buttons == 0u);
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 0);
    assert(state.ignored == 1u);

    mock_screen_input_reset(&state, 0x1234u, 0, 800u, 480u);
    raw = raw_event(MOCK_SCREEN_RAW_POINTER, 0x1234u);
    raw.position_valid = 1u;
    raw.buttons_valid = 1u;
    raw.buttons = 1u;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 0);
    assert(state.coordinate_drops == 1u);
    assert(state.pointer_buttons == 0u);

    mock_screen_input_reset(&state, 0x1234u, 0, 800u, 480u);
    raw = raw_event(MOCK_SCREEN_RAW_TOUCH, 0x1234u);
    raw.touch_id_valid = 1u;
    raw.touch_id = 9u;
    raw.source_valid = 1u;
    raw.source_x = -1;
    raw.source_y = 0;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 0);
    raw.source_x = 800;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 0);
    raw.source_x = 799;
    raw.source_y = 479;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    raw.type = MOCK_SCREEN_RAW_RELEASE;
    raw.source_x = 800;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    assert(pointer.x == 799 && pointer.y == 479);
    assert(state.out_of_bounds == 3u);
    assert(state.coordinate_drops == 2u);

    /* The first class to produce a valid press owns the complete input
     * generation. A synthesized second class must not duplicate edges or
     * mutate the ignored class's shadow state. */
    mock_screen_input_reset(&state, 0x1234u, 1, 800u, 480u);
    raw = raw_event(MOCK_SCREEN_RAW_TOUCH, 0x1234u);
    raw.touch_id_valid = 1u;
    raw.touch_id = 3u;
    raw.source_valid = 1u;
    raw.source_x = 100;
    raw.source_y = 101;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    assert(pointer.action == MOCK_NAV_BACKEND_POINTER_PRESS);
    assert(state.input_class == MOCK_SCREEN_INPUT_CLASS_TOUCH);
    raw = raw_event(MOCK_SCREEN_RAW_POINTER, 0x1234u);
    raw.position_valid = 1u;
    raw.position_x = 100;
    raw.position_y = 101;
    raw.buttons_valid = 1u;
    raw.buttons = 1u;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 0);
    assert(state.pointer_buttons == 0u);
    raw.type = MOCK_SCREEN_RAW_MOVE;
    raw.touch_id_valid = 1u;
    raw.touch_id = 3u;
    raw.source_valid = 1u;
    raw.source_x = 102;
    raw.source_y = 103;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    assert(pointer.action == MOCK_NAV_BACKEND_POINTER_MOVE);
    raw = raw_event(MOCK_SCREEN_RAW_POINTER, 0x1234u);
    raw.position_valid = 1u;
    raw.position_x = 102;
    raw.position_y = 103;
    raw.buttons_valid = 1u;
    raw.buttons = 0u;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 0);
    assert(state.pointer_buttons == 0u);
    raw = raw_event(MOCK_SCREEN_RAW_RELEASE, 0x1234u);
    raw.touch_id_valid = 1u;
    raw.touch_id = 3u;
    raw.source_valid = 1u;
    raw.source_x = 102;
    raw.source_y = 103;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    assert(pointer.action == MOCK_NAV_BACKEND_POINTER_RELEASE);
    assert(state.duplicate_class == 2u);

    mock_screen_input_reset(&state, 0x1234u, 1, 800u, 480u);
    assert(state.input_class == MOCK_SCREEN_INPUT_CLASS_NONE);
    assert(state.duplicate_class == 0u);
    raw = raw_event(MOCK_SCREEN_RAW_POINTER, 0x1234u);
    raw.position_valid = 1u;
    raw.position_x = 200;
    raw.position_y = 201;
    raw.buttons_valid = 1u;
    raw.buttons = 1u;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    assert(pointer.action == MOCK_NAV_BACKEND_POINTER_PRESS);
    assert(state.input_class == MOCK_SCREEN_INPUT_CLASS_POINTER);
    raw = raw_event(MOCK_SCREEN_RAW_TOUCH, 0x1234u);
    raw.touch_id_valid = 1u;
    raw.touch_id = 4u;
    raw.source_valid = 1u;
    raw.source_x = 200;
    raw.source_y = 201;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 0);
    assert(state.touch_active == 0u);
    raw = raw_event(MOCK_SCREEN_RAW_POINTER, 0x1234u);
    raw.position_valid = 1u;
    raw.position_x = 202;
    raw.position_y = 203;
    raw.buttons_valid = 1u;
    raw.buttons = 1u;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    assert(pointer.action == MOCK_NAV_BACKEND_POINTER_MOVE);
    raw = raw_event(MOCK_SCREEN_RAW_MOVE, 0x1234u);
    raw.touch_id_valid = 1u;
    raw.touch_id = 4u;
    raw.source_valid = 1u;
    raw.source_x = 202;
    raw.source_y = 203;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 0);
    raw = raw_event(MOCK_SCREEN_RAW_POINTER, 0x1234u);
    raw.position_valid = 1u;
    raw.position_x = 202;
    raw.position_y = 203;
    raw.buttons_valid = 1u;
    raw.buttons = 0u;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    assert(pointer.action == MOCK_NAV_BACKEND_POINTER_RELEASE);
    raw = raw_event(MOCK_SCREEN_RAW_RELEASE, 0x1234u);
    raw.touch_id_valid = 1u;
    raw.touch_id = 4u;
    raw.source_valid = 1u;
    raw.source_x = 202;
    raw.source_y = 203;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 0);
    assert(state.touch_active == 0u);
    assert(state.duplicate_class == 3u);

    mock_screen_input_reset(&state, 0x1234u, 0, 800u, 480u);
    raw = raw_event(MOCK_SCREEN_RAW_TOUCH, 0x1234u);
    raw.touch_id_valid = 1u;
    raw.touch_id = 5u;
    raw.source_valid = 1u;
    raw.source_x = 1;
    raw.source_y = 2;
    assert(mock_screen_input_decode(&state, &raw, &pointer) == 1);
    assert(state.input_class == MOCK_SCREEN_INPUT_CLASS_TOUCH);
    return 0;
}
