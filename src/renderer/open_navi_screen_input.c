#include "open_navi_screen_input.h"

#include <limits.h>
#include <string.h>

static void saturating_increment(uint32_t *value)
{
    if (*value != UINT32_MAX) ++*value;
}

void open_navi_screen_input_reset(OpenNaviScreenInputState *state,
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

static int coordinates_with(OpenNaviScreenInputState *state,
                            const OpenNaviScreenRawEvent *raw,
                            uint32_t action,
                            int32_t *x_out,
                            int32_t *y_out,
                            uint8_t last_valid,
                            int32_t last_x,
                            int32_t last_y);

static int coordinates(OpenNaviScreenInputState *state,
                       const OpenNaviScreenRawEvent *raw,
                       uint32_t action,
                       int32_t *x_out,
                       int32_t *y_out)
{
    return coordinates_with(state, raw, action, x_out, y_out,
                            state->last_position_valid, state->last_x,
                            state->last_y);
}

/* A release without a usable position reuses the contact's last one. */
static int coordinates_with(OpenNaviScreenInputState *state,
                            const OpenNaviScreenRawEvent *raw,
                            uint32_t action,
                            int32_t *x_out,
                            int32_t *y_out,
                            uint8_t last_valid,
                            int32_t last_x,
                            int32_t last_y)
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
    if (action == OPEN_NAVI_BACKEND_POINTER_RELEASE && last_valid != 0u) {
        *x_out = last_x;
        *y_out = last_y;
        return 1;
    }
    saturating_increment(&state->coordinate_drops);
    return 0;
}

static void emit(OpenNaviBackendPointerV2 *pointer_out, uint32_t action,
                 int32_t x, int32_t y, uint32_t buttons, uint32_t contact,
                 uint32_t count)
{
    memset(pointer_out, 0, sizeof(*pointer_out));
    pointer_out->abi_version = OPEN_NAVI_BACKEND_POINTER_ABI_V2;
    pointer_out->struct_size = sizeof(*pointer_out);
    pointer_out->action = action;
    pointer_out->x = x;
    pointer_out->y = y;
    pointer_out->buttons = buttons;
    pointer_out->contact_id = contact;
    pointer_out->contact_count = count;
}

/* Second touch contact (multi_contact only): a touch whose id is not the
 * first contact's while that one is down, or the tracked second id. */
static int decode_second(OpenNaviScreenInputState *state,
                         const OpenNaviScreenRawEvent *raw,
                         OpenNaviBackendPointerV2 *pointer_out)
{
    uint32_t action;
    int32_t x;
    int32_t y;
    const int known = state->second_active != 0u &&
                      state->second_touch_id == raw->touch_id;
    if (raw->type == OPEN_NAVI_SCREEN_RAW_TOUCH) {
        if (state->second_active != 0u && !known) {
            saturating_increment(&state->secondary_touch);
            return 0;
        }
        action = known ? OPEN_NAVI_BACKEND_POINTER_MOVE
                       : OPEN_NAVI_BACKEND_POINTER_PRESS;
    } else if (raw->type == OPEN_NAVI_SCREEN_RAW_MOVE ||
               raw->type == OPEN_NAVI_SCREEN_RAW_RELEASE) {
        if (!known) {
            saturating_increment(&state->secondary_touch);
            return 0;
        }
        action = raw->type == OPEN_NAVI_SCREEN_RAW_MOVE
                     ? OPEN_NAVI_BACKEND_POINTER_MOVE
                     : OPEN_NAVI_BACKEND_POINTER_RELEASE;
    } else {
        saturating_increment(&state->ignored);
        return 0;
    }
    if (!coordinates_with(state, raw, action, &x, &y,
                          state->second_position_valid,
                          state->second_last_x, state->second_last_y))
        return 0;
    if (action == OPEN_NAVI_BACKEND_POINTER_PRESS) {
        state->second_active = 1u;
        state->second_touch_id = raw->touch_id;
    } else if (action == OPEN_NAVI_BACKEND_POINTER_RELEASE) {
        state->second_active = 0u;
    }
    state->second_last_x = x;
    state->second_last_y = y;
    state->second_position_valid = 1u;
    emit(pointer_out, action, x, y, raw->buttons_valid != 0u ? raw->buttons : 0u,
         1u, (uint32_t)state->touch_active + state->second_active);
    saturating_increment(&state->accepted);
    return 1;
}

int open_navi_screen_input_decode(OpenNaviScreenInputState *state,
                             const OpenNaviScreenRawEvent *raw,
                             OpenNaviBackendPointerV2 *pointer_out)
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

    if (raw->type == OPEN_NAVI_SCREEN_RAW_POINTER) {
        event_class = OPEN_NAVI_SCREEN_INPUT_CLASS_POINTER;
    } else if (raw->type == OPEN_NAVI_SCREEN_RAW_TOUCH ||
               raw->type == OPEN_NAVI_SCREEN_RAW_MOVE ||
               raw->type == OPEN_NAVI_SCREEN_RAW_RELEASE) {
        event_class = OPEN_NAVI_SCREEN_INPUT_CLASS_TOUCH;
    } else {
        saturating_increment(&state->ignored);
        return 0;
    }
    if (state->input_class != OPEN_NAVI_SCREEN_INPUT_CLASS_NONE &&
        state->input_class != event_class) {
        /* V1 exposes one pointer. Keep the first class that produced a valid
         * press for this generation. In particular, ignored pointer button
         * transitions and late releases must not alter shadow state. */
        saturating_increment(&state->duplicate_class);
        return 0;
    }

    if (event_class == OPEN_NAVI_SCREEN_INPUT_CLASS_POINTER) {
        if (raw->buttons_valid == 0u) {
            saturating_increment(&state->property_errors);
            return 0;
        }
        if (state->pointer_buttons == 0u && raw->buttons != 0u)
            action = OPEN_NAVI_BACKEND_POINTER_PRESS;
        else if (state->pointer_buttons != 0u && raw->buttons == 0u)
            action = OPEN_NAVI_BACKEND_POINTER_RELEASE;
        else if (state->pointer_buttons != 0u)
            action = OPEN_NAVI_BACKEND_POINTER_MOVE;
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
        if (state->multi_contact != 0u &&
            ((state->second_active != 0u &&
              state->second_touch_id == raw->touch_id) ||
             (state->touch_active != 0u &&
              state->active_touch_id != raw->touch_id))) {
            if (state->input_class != OPEN_NAVI_SCREEN_INPUT_CLASS_TOUCH) {
                saturating_increment(&state->secondary_touch);
                return 0;
            }
            return decode_second(state, raw, pointer_out);
        }
        if (raw->type == OPEN_NAVI_SCREEN_RAW_TOUCH) {
            if (state->touch_active != 0u &&
                state->active_touch_id != raw->touch_id) {
                saturating_increment(&state->secondary_touch);
                return 0;
            }
            action = state->touch_active != 0u
                         ? OPEN_NAVI_BACKEND_POINTER_MOVE
                         : OPEN_NAVI_BACKEND_POINTER_PRESS;
        } else if (raw->type == OPEN_NAVI_SCREEN_RAW_MOVE) {
            if (state->touch_active == 0u ||
                state->active_touch_id != raw->touch_id) {
                saturating_increment(&state->secondary_touch);
                return 0;
            }
            action = OPEN_NAVI_BACKEND_POINTER_MOVE;
        } else if (raw->type == OPEN_NAVI_SCREEN_RAW_RELEASE) {
            if (state->touch_active == 0u ||
                state->active_touch_id != raw->touch_id) {
                saturating_increment(&state->secondary_touch);
                return 0;
            }
            action = OPEN_NAVI_BACKEND_POINTER_RELEASE;
        } else {
            saturating_increment(&state->ignored);
            return 0;
        }
        if (!coordinates(state, raw, action, &x, &y)) return 0;
        if (action == OPEN_NAVI_BACKEND_POINTER_PRESS) {
            state->touch_active = 1u;
            state->active_touch_id = raw->touch_id;
        } else if (action == OPEN_NAVI_BACKEND_POINTER_RELEASE) {
            state->touch_active = 0u;
        }
    }

    if (action == OPEN_NAVI_BACKEND_POINTER_PRESS &&
        state->input_class == OPEN_NAVI_SCREEN_INPUT_CLASS_NONE)
        state->input_class = event_class;

    emit(pointer_out, action, x, y, raw->buttons_valid != 0u ? raw->buttons : 0u,
         0u,
         event_class == OPEN_NAVI_SCREEN_INPUT_CLASS_TOUCH
             ? (uint32_t)state->touch_active + state->second_active
             : (state->pointer_buttons != 0u ? 1u : 0u));
    state->last_x = x;
    state->last_y = y;
    state->last_position_valid = 1u;
    saturating_increment(&state->accepted);
    return 1;
}

int open_navi_screen_input_platform_result(int decode_result)
{
    return decode_result == 0 ? 2 : decode_result;
}
