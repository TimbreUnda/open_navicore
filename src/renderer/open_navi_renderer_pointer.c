#define _POSIX_C_SOURCE 200112L

#include "open_navi_renderer_internal.h"

#include "open_navi_renderer_platform.h"

#include <string.h>

static void remove_pointer_offset_locked(unsigned int offset)
{
    unsigned int index;
    for (index = offset; index + 1u < g_runtime.pointer.count; ++index) {
        unsigned int destination =
            (g_runtime.pointer.head + index) % OPEN_NAVI_POINTER_QUEUE_CAPACITY;
        unsigned int source =
            (g_runtime.pointer.head + index + 1u) % OPEN_NAVI_POINTER_QUEUE_CAPACITY;
        g_runtime.pointer.queue[destination] = g_runtime.pointer.queue[source];
    }
    --g_runtime.pointer.count;
}

static int evict_oldest_gesture_pair_locked(void)
{
    unsigned int offset;
    for (offset = 0u; offset + 1u < g_runtime.pointer.count; ++offset) {
        unsigned int first = (g_runtime.pointer.head + offset) %
                             OPEN_NAVI_POINTER_QUEUE_CAPACITY;
        unsigned int second = (first + 1u) % OPEN_NAVI_POINTER_QUEUE_CAPACITY;
        if (g_runtime.pointer.queue[first].data.pointer.action ==
                OPEN_NAVI_BACKEND_POINTER_PRESS &&
            g_runtime.pointer.queue[second].data.pointer.action ==
                OPEN_NAVI_BACKEND_POINTER_RELEASE) {
            remove_pointer_offset_locked(offset);
            remove_pointer_offset_locked(offset);
            increment_counter(&g_runtime.pointer.dropped);
            increment_counter(&g_runtime.pointer.dropped);
            increment_counter(&g_runtime.pointer.gesture_pairs_evicted);
            return 1;
        }
    }
    return 0;
}

/* The queued move of the same contact a new move can replace: searched
 * back from the tail across moves only (other contacts' moves may be
 * passed, never a press or release), so per-contact order is kept. */
static RenderEvent *coalescible_move_locked(uint32_t contact)
{
    unsigned int back;
    for (back = 1u; back <= g_runtime.pointer.count; ++back) {
        RenderEvent *event = &g_runtime.pointer.queue[
            (g_runtime.pointer.head + g_runtime.pointer.count - back) %
            OPEN_NAVI_POINTER_QUEUE_CAPACITY];
        if (event->data.pointer.action != OPEN_NAVI_BACKEND_POINTER_MOVE)
            return NULL;
        if (event->data.pointer.contact_id == contact)
            return event;
    }
    return NULL;
}

int open_navi_renderer_enqueue_pointer(const OpenNaviBackendPointerV2 *pointer)
{
    RenderEvent *event;
    unsigned int tail;
    int result = 0;

    if (pointer == NULL ||
        (pointer->action != OPEN_NAVI_BACKEND_POINTER_PRESS &&
         pointer->action != OPEN_NAVI_BACKEND_POINTER_RELEASE &&
         pointer->action != OPEN_NAVI_BACKEND_POINTER_MOVE)) return -2;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.accepting_state == 0 ||
        (g_runtime.state != RENDERER_STARTING &&
         g_runtime.state != RENDERER_RUNNING) || g_runtime.stop != 0) {
        increment_counter(&g_runtime.pointer.dropped);
        result = -1;
    } else if (pointer->action == OPEN_NAVI_BACKEND_POINTER_MOVE &&
               (event = coalescible_move_locked(pointer->contact_id)) != NULL) {
        /* Keep the queued move's sequence: take_event() serves the oldest
         * event first, and a move renumbered on every coalesce stays newer
         * than the frame request the backend renews each frame, so during a
         * drag it was only dispatched about once a second. Updating the
         * position in place delivers the latest position every frame. */
        event->data.pointer = *pointer;
        increment_counter(&g_runtime.pointer.accepted);
        increment_counter(&g_runtime.pointer.move_coalesced);
        (void)pthread_cond_signal(&g_runtime.condition);
    } else if (pointer->action == OPEN_NAVI_BACKEND_POINTER_MOVE &&
               g_runtime.pointer.count >=
                   OPEN_NAVI_POINTER_QUEUE_CAPACITY - OPEN_NAVI_POINTER_EDGE_RESERVE) {
        increment_counter(&g_runtime.pointer.dropped);
        increment_counter(&g_runtime.pointer.move_dropped);
        result = -1;
    } else {
        if (g_runtime.pointer.count == OPEN_NAVI_POINTER_QUEUE_CAPACITY) {
            unsigned int offset;
            for (offset = 0u; offset < g_runtime.pointer.count; ++offset) {
                if (g_runtime.pointer.queue[
                        (g_runtime.pointer.head + offset) %
                        OPEN_NAVI_POINTER_QUEUE_CAPACITY].data.pointer.action ==
                    OPEN_NAVI_BACKEND_POINTER_MOVE) break;
            }
            if (offset == g_runtime.pointer.count) {
                if (!evict_oldest_gesture_pair_locked()) {
                    increment_counter(&g_runtime.pointer.dropped);
                    increment_counter(&g_runtime.pointer.edge_dropped);
                    result = -1;
                }
            } else {
                remove_pointer_offset_locked(offset);
                increment_counter(&g_runtime.pointer.dropped);
                increment_counter(&g_runtime.pointer.move_evicted);
            }
        }
        if (result == 0) {
            tail = (g_runtime.pointer.head + g_runtime.pointer.count) %
                   OPEN_NAVI_POINTER_QUEUE_CAPACITY;
            event = &g_runtime.pointer.queue[tail];
            memset(event, 0, sizeof(*event));
            event->sequence = open_navi_renderer_next_event_sequence_locked();
            event->type = RENDER_EVENT_POINTER;
            event->data.pointer = *pointer;
            ++g_runtime.pointer.count;
            increment_counter(&g_runtime.pointer.accepted);
            (void)pthread_cond_signal(&g_runtime.condition);
        }
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_renderer_drain_platform_pointer(void)
{
    unsigned int drained;
    for (drained = 0u; drained < OPEN_NAVI_PLATFORM_DRAIN_LIMIT; ++drained) {
        OpenNaviBackendPointerV2 pointer;
        int result = open_navi_renderer_platform_poll_pointer(
            &pointer, g_runtime.pointer_contacts, g_runtime.diagnostic,
            sizeof(g_runtime.diagnostic));
        if (result == 0) return 0;
        if (result == 1) {
            (void)open_navi_renderer_enqueue_pointer(&pointer);
        } else if (result == 2) {
            (void)pthread_mutex_lock(&g_runtime.lock);
            increment_counter(&g_runtime.counters.platform_ignored);
            (void)pthread_mutex_unlock(&g_runtime.lock);
        } else if (result == 3) {
            (void)pthread_mutex_lock(&g_runtime.lock);
            increment_counter(&g_runtime.counters.platform_errors);
            (void)pthread_mutex_unlock(&g_runtime.lock);
            return 0;
        } else {
            (void)pthread_mutex_lock(&g_runtime.lock);
            if (result == -2)
                increment_counter(&g_runtime.counters.platform_window_lost);
            else
                increment_counter(&g_runtime.counters.platform_errors);
            (void)pthread_mutex_unlock(&g_runtime.lock);
            return -1;
        }
    }
    return 1;
}
