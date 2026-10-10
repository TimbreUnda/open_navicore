#include "mock_screen_input.h"

#include <limits.h>
#include <string.h>

static void saturating_increment(uint32_t *value)
{
    if (*value != UINT32_MAX) ++*value;
}

void mock_screen_input_reset(MockScreenInputState *state,
                             uintptr_t target_window,
                             int identity_coordinates,
                             uint32_t target_width,
                             uint32_t target_height)
{
    memset(state, 0, sizeof(*state));
    state->target_window = target_window;
    state->target_width = target_width;
    state->target_height = target_height;
    state->identity_coordinates = identity_coordinates != 0;
}

static int coordinates(MockScreenInputState *state,
                       const MockScreenRawEvent *raw,
                       uint32_t action,
                       int32_t *x_out,
                       int32_t *y_out)
{
    int available = 0;
    if (raw->source_valid != 0u) {
        *x_out = raw->source_x;
        *y_out = raw->source_y;
        available = 1;
    } else if (state->identity_coordinates != 0u &&
               raw->position_valid != 0u) {
        *x_out = raw->position_x;
        *y_out = raw->position_y;
        available = 1;
    }
    if (available != 0 && *x_out >= 0 && *y_out >= 0 &&
        (uint32_t)*x_out < state->target_width &&
        (uint32_t)*y_out < state->target_height) {
        return 1;
    }
    if (available != 0) {
        saturating_increment(&state->out_of_bounds);
    } else {
        saturating_increment(&state->property_errors);
    }
    if (action == MOCK_NAV_BACKEND_POINTER_RELEASE &&
        state->last_position_valid != 0u) {
        *x_out = state->last_x;
        *y_out = state->last_y;
        return 1;
    }
    saturating_increment(&state->coordinate_drops);
    return 0;
}

int mock_screen_input_decode(MockScreenInputState *state,
                             const MockScreenRawEvent *raw,
                             MockNavBackendPointerV1 *pointer_out)
{
    uint32_t action;
    uint8_t event_class;
    int32_t x;
    int32_t y;

    if (state == NULL || raw == NULL || pointer_out == NULL) return -1;
    if (raw->window != state->target_window) {
        saturating_increment(&state->foreign_window);
        return 0;
    }

    if (raw->type == MOCK_SCREEN_RAW_POINTER) {
        event_class = MOCK_SCREEN_INPUT_CLASS_POINTER;
    } else if (raw->type == MOCK_SCREEN_RAW_TOUCH ||
               raw->type == MOCK_SCREEN_RAW_MOVE ||
               raw->type == MOCK_SCREEN_RAW_RELEASE) {
        event_class = MOCK_SCREEN_INPUT_CLASS_TOUCH;
    } else {
        saturating_increment(&state->ignored);
        return 0;
    }
    if (state->input_class != MOCK_SCREEN_INPUT_CLASS_NONE &&
        state->input_class != event_class) {
        /* V1 exposes one pointer. Keep the first class that produced a valid
         * press for this generation. In particular, ignored pointer button
         * transitions and late releases must not alter shadow state. */
        saturating_increment(&state->duplicate_class);
        return 0;
    }

    if (event_class == MOCK_SCREEN_INPUT_CLASS_POINTER) {
        if (raw->buttons_valid == 0u) {
            saturating_increment(&state->property_errors);
            return 0;
        }
        if (state->pointer_buttons == 0u && raw->buttons != 0u)
            action = MOCK_NAV_BACKEND_POINTER_PRESS;
        else if (state->pointer_buttons != 0u && raw->buttons == 0u)
            action = MOCK_NAV_BACKEND_POINTER_RELEASE;
        else if (state->pointer_buttons != 0u)
            action = MOCK_NAV_BACKEND_POINTER_MOVE;
        else {
            saturating_increment(&state->ignored);
            return 0;
        }
        if (!coordinates(state, raw, action, &x, &y)) return 0;
        state->pointer_buttons = raw->buttons;
    } else {
        if (raw->touch_id_valid == 0u) {
            saturating_increment(&state->property_errors);
            return 0;
        }
        if (raw->type == MOCK_SCREEN_RAW_TOUCH) {
            if (state->touch_active != 0u &&
                state->active_touch_id != raw->touch_id) {
                saturating_increment(&state->secondary_touch);
                return 0;
            }
            action = state->touch_active != 0u
                         ? MOCK_NAV_BACKEND_POINTER_MOVE
                         : MOCK_NAV_BACKEND_POINTER_PRESS;
        } else if (raw->type == MOCK_SCREEN_RAW_MOVE) {
            if (state->touch_active == 0u ||
                state->active_touch_id != raw->touch_id) {
                saturating_increment(&state->secondary_touch);
                return 0;
            }
            action = MOCK_NAV_BACKEND_POINTER_MOVE;
        } else if (raw->type == MOCK_SCREEN_RAW_RELEASE) {
            if (state->touch_active == 0u ||
                state->active_touch_id != raw->touch_id) {
                saturating_increment(&state->secondary_touch);
                return 0;
            }
            action = MOCK_NAV_BACKEND_POINTER_RELEASE;
        } else {
            saturating_increment(&state->ignored);
            return 0;
        }
        if (!coordinates(state, raw, action, &x, &y)) return 0;
        if (action == MOCK_NAV_BACKEND_POINTER_PRESS) {
            state->touch_active = 1u;
            state->active_touch_id = raw->touch_id;
        } else if (action == MOCK_NAV_BACKEND_POINTER_RELEASE) {
            state->touch_active = 0u;
        }
    }

    if (action == MOCK_NAV_BACKEND_POINTER_PRESS &&
        state->input_class == MOCK_SCREEN_INPUT_CLASS_NONE)
        state->input_class = event_class;

    memset(pointer_out, 0, sizeof(*pointer_out));
    pointer_out->abi_version = MOCK_NAV_BACKEND_ABI_V1;
    pointer_out->struct_size = sizeof(*pointer_out);
    pointer_out->action = action;
    pointer_out->x = x;
    pointer_out->y = y;
    pointer_out->buttons = raw->buttons_valid != 0u ? raw->buttons : 0u;
    state->last_x = x;
    state->last_y = y;
    state->last_position_valid = 1u;
    saturating_increment(&state->accepted);
    return 1;
}

int mock_screen_input_platform_result(int decode_result)
{
    return decode_result == 0 ? 2 : decode_result;
}
