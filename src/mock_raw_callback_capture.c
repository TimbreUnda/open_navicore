#include "mock_raw_callback_capture_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { MOCK_RAW_CONTEXT_CAPACITY = 64, MOCK_RAW_WRAPPER_CAPACITY = 64 };
enum { MOCK_RAW_STACK_RANGE_CAPACITY = 64 };
enum { CELL_FREE = 0u, CELL_WRITING = 1u, CELL_READY = 2u };

typedef struct MockRawRecordCell {
    volatile uint32_t state;
    MockRawCallbackRecord record;
} MockRawRecordCell;

typedef struct MockRawReturnContext {
    volatile uint32_t active;
    uint32_t entry_sp;
    uint32_t original_lr;
    uint32_t record_index;
    MockRawCallbackWrapper *wrapper;
} MockRawReturnContext;

typedef struct MockRawStackRange {
    volatile uint32_t low;
    volatile uint32_t high;
} MockRawStackRange;

static MockRawRecordCell g_records[MOCK_RAW_CALLBACK_RECORD_CAPACITY];
static MockRawReturnContext g_contexts[MOCK_RAW_CONTEXT_CAPACITY];
static volatile uint32_t g_sequence;
static volatile uint32_t g_dropped;
static volatile uint32_t g_context_dropped;
static MockRawCallbackWrapper *volatile g_wrappers[MOCK_RAW_WRAPPER_CAPACITY];
static MockRawStackRange g_stack_ranges[MOCK_RAW_STACK_RANGE_CAPACITY];

#ifdef MOCK_RAW_CALLBACK_HOST_TEST
void mock_raw_callback_return(void)
{
}
#endif

typedef char MockRawRecordSizeMustBe136[
    sizeof(MockRawCallbackRecord) == 136u ? 1 : -1];
typedef char MockRawFrameSizeMustBe32[
    sizeof(MockRawCallbackEntryFrame) == 32u ? 1 : -1];

static uint32_t ptr32(const void *pointer)
{
    return (uint32_t)(uintptr_t)pointer;
}

static uint32_t mock_raw_failure(void *ignored)
{
    (void)ignored;
    return 1u;
}

static MockRawReturnContext *claim_context(void)
{
    uint32_t index;

    for (index = 0u; index < MOCK_RAW_CONTEXT_CAPACITY; ++index) {
        if (__sync_bool_compare_and_swap(&g_contexts[index].active, 0u, 1u)) {
            return &g_contexts[index];
        }
    }
    __sync_fetch_and_add(&g_context_dropped, 1u);
    return NULL;
}

static uint32_t claim_record(uint32_t sequence)
{
    uint32_t offset;

    for (offset = 0u; offset < MOCK_RAW_CALLBACK_RECORD_CAPACITY; ++offset) {
        uint32_t index = (sequence + offset) % MOCK_RAW_CALLBACK_RECORD_CAPACITY;
        if (__sync_bool_compare_and_swap(&g_records[index].state,
                                         CELL_FREE, CELL_WRITING)) {
            return index;
        }
    }
    __sync_fetch_and_add(&g_dropped, 1u);
    return UINT32_MAX;
}

int mock_raw_callback_register_stack(uint32_t low, uint32_t high)
{
    uint32_t index;

    if ((low & 3u) != 0u || (high & 3u) != 0u || low == 0u || low >= high) {
        return -1;
    }
    for (index = 0u; index < MOCK_RAW_STACK_RANGE_CAPACITY; ++index) {
        if (__sync_bool_compare_and_swap(&g_stack_ranges[index].low, 0u, low)) {
            g_stack_ranges[index].high = high;
            __sync_synchronize();
            return 0;
        }
    }
    return 1;
}

int mock_raw_callback_unregister_stack(uint32_t low, uint32_t high)
{
    uint32_t index;

    for (index = 0u; index < MOCK_RAW_STACK_RANGE_CAPACITY; ++index) {
        if (g_stack_ranges[index].low == low &&
            g_stack_ranges[index].high == high) {
            g_stack_ranges[index].high = 0u;
            __sync_synchronize();
            if (__sync_bool_compare_and_swap(&g_stack_ranges[index].low,
                                             low, 0u)) {
                return 0;
            }
            return -1;
        }
    }
    return -1;
}

static uint32_t readable_stack_words(uint32_t entry_sp)
{
    uint32_t index;

    for (index = 0u; index < MOCK_RAW_STACK_RANGE_CAPACITY; ++index) {
        uint32_t low = g_stack_ranges[index].low;
        uint32_t high = g_stack_ranges[index].high;
        if (entry_sp >= low && entry_sp < high) {
            uint32_t words = (high - entry_sp) / 4u;
            return words < MOCK_RAW_CALLBACK_STACK_WORDS
                       ? words
                       : MOCK_RAW_CALLBACK_STACK_WORDS;
        }
    }
    return 0u;
}

static int wrapper_init_common(
    MockRawCallbackWrapper *wrapper, uint32_t adapter, void *original_self,
    void *volatile *original_self_slot,
    const uint8_t binary_id[MOCK_RAW_CALLBACK_BINARY_ID_SIZE])
{
    const void *const *original_vtable;
    uint32_t index;
    uint32_t max_slot = 0u;
    uint32_t matches = 0u;
    uint32_t registry_slot = UINT32_MAX;

    if (wrapper == NULL || binary_id == NULL ||
        (original_self == NULL && original_self_slot == NULL) ||
        (original_self != NULL && original_self_slot != NULL)) {
        return -1;
    }
    original_vtable = original_self != NULL
                          ? *(const void *const *const *)original_self
                          : NULL;
    if (original_self != NULL && original_vtable == NULL) {
        return -1;
    }
    for (index = 0u; index < mock_raw_callback_slot_count; ++index) {
        if (mock_raw_callback_slots[index].adapter == adapter) {
            if (mock_raw_callback_slots[index].slot > max_slot) {
                max_slot = mock_raw_callback_slots[index].slot;
            }
            ++matches;
        }
    }
    if (matches == 0u || max_slot >= MOCK_RAW_CALLBACK_MAX_VTABLE_SLOTS) {
        return -1;
    }

    for (index = 0u; index < MOCK_RAW_WRAPPER_CAPACITY; ++index) {
        if (__sync_bool_compare_and_swap(&g_wrappers[index], NULL, wrapper)) {
            registry_slot = index;
            break;
        }
    }
    if (registry_slot == UINT32_MAX) {
        return -1;
    }

    memset(wrapper, 0, sizeof(*wrapper));
    wrapper->original_self = original_self;
    wrapper->original_vtable = original_vtable;
    wrapper->original_self_slot = original_self_slot;
    memcpy(wrapper->binary_id, binary_id, MOCK_RAW_CALLBACK_BINARY_ID_SIZE);
    wrapper->adapter = adapter;
    wrapper->vtable_slots = max_slot + 1u;
    wrapper->registry_slot = registry_slot;
    for (index = 0u; index <= max_slot; ++index) {
        wrapper->shadow_vtable[index] = original_vtable != NULL
                                            ? (void *)original_vtable[index]
                                            : NULL;
    }
    for (index = 0u; index < mock_raw_callback_slot_count; ++index) {
        const MockRawCallbackSlot *entry = &mock_raw_callback_slots[index];
        if (entry->adapter == adapter) {
            wrapper->shadow_vtable[entry->slot] = (void *)entry->trampoline;
        }
    }
    wrapper->vptr = wrapper->shadow_vtable;
    wrapper->magic = MOCK_RAW_CALLBACK_WRAPPER_MAGIC;
    __sync_synchronize();
    wrapper->active = 1u;
    return 0;
}

int mock_raw_callback_wrapper_init(
    MockRawCallbackWrapper *wrapper, uint32_t adapter, void *original_self,
    const uint8_t binary_id[MOCK_RAW_CALLBACK_BINARY_ID_SIZE])
{
    return wrapper_init_common(wrapper, adapter, original_self, NULL,
                               binary_id);
}

int mock_raw_callback_wrapper_init_indirect(
    MockRawCallbackWrapper *wrapper, uint32_t adapter,
    void *volatile *original_self_slot,
    const uint8_t binary_id[MOCK_RAW_CALLBACK_BINARY_ID_SIZE])
{
    return wrapper_init_common(wrapper, adapter, NULL, original_self_slot,
                               binary_id);
}

void *mock_raw_callback_wrapper_proxy(MockRawCallbackWrapper *wrapper)
{
    return wrapper != NULL && wrapper->magic == MOCK_RAW_CALLBACK_WRAPPER_MAGIC &&
                   wrapper->active != 0u
               ? wrapper
               : NULL;
}

int mock_raw_callback_wrapper_destroy(MockRawCallbackWrapper *wrapper)
{
    if (wrapper == NULL || wrapper->magic != MOCK_RAW_CALLBACK_WRAPPER_MAGIC) {
        return -1;
    }
    wrapper->active = 0u;
    __sync_synchronize();
    if (wrapper->inflight != 0u) {
        return 1;
    }
    if (wrapper->registry_slot >= MOCK_RAW_WRAPPER_CAPACITY ||
        !__sync_bool_compare_and_swap(&g_wrappers[wrapper->registry_slot],
                                      wrapper, NULL)) {
        return -1;
    }
    wrapper->magic = 0u;
    wrapper->vptr = NULL;
    wrapper->original_self = NULL;
    wrapper->original_vtable = NULL;
    wrapper->original_self_slot = NULL;
    return 0;
}

static MockRawCallbackWrapper *lookup_wrapper(const MockRawCallbackEntryFrame *frame,
                                               uint32_t *self_lane)
{
    MockRawCallbackWrapper *r0 =
        (MockRawCallbackWrapper *)(uintptr_t)frame->r0;
    MockRawCallbackWrapper *r1 =
        (MockRawCallbackWrapper *)(uintptr_t)frame->r1;
    uint32_t index;

    for (index = 0u; index < MOCK_RAW_WRAPPER_CAPACITY; ++index) {
        MockRawCallbackWrapper *candidate = g_wrappers[index];
        if (candidate == r0) {
            *self_lane = 0u;
            return candidate;
        }
        if (candidate == r1) {
            *self_lane = 1u;
            return candidate;
        }
    }
    return NULL;
}

void mock_raw_callback_prepare(MockRawCallbackEntryFrame *frame,
                               uint32_t slot_id, uint32_t cpsr)
{
    uint32_t self_lane = UINT32_MAX;
    MockRawCallbackWrapper *wrapper = lookup_wrapper(frame, &self_lane);
    const MockRawCallbackSlot *slot = slot_id < mock_raw_callback_slot_count
                                          ? &mock_raw_callback_slots[slot_id]
                                          : NULL;
    MockRawReturnContext *context = NULL;
    uint32_t entry_sp = ptr32(frame) + (uint32_t)sizeof(*frame);
    uint32_t target = ptr32(mock_raw_failure);
    uint32_t original_lr = frame->lr_or_return;
    void *original_self = NULL;
    const void *const *original_vtable = NULL;
    uint32_t sequence;
    uint32_t record_index;
    uint32_t index;

    frame->cpsr = cpsr;
    if (wrapper != NULL && slot != NULL &&
        wrapper->magic == MOCK_RAW_CALLBACK_WRAPPER_MAGIC &&
        wrapper->adapter == slot->adapter &&
        slot->slot < wrapper->vtable_slots && wrapper->active != 0u) {
        original_self = wrapper->original_self_slot != NULL
                            ? *wrapper->original_self_slot
                            : wrapper->original_self;
        original_vtable = original_self != NULL
                              ? *(const void *const *const *)original_self
                              : NULL;
        target = original_vtable != NULL
                     ? ptr32(original_vtable[slot->slot])
                     : ptr32(mock_raw_failure);
        if (self_lane == 0u) {
            frame->r0 = ptr32(original_self);
        } else {
            frame->r1 = ptr32(original_self);
        }
        if (original_self != NULL && original_vtable != NULL &&
            original_vtable[slot->slot] != NULL) {
            __sync_fetch_and_add(&wrapper->inflight, 1u);
            __sync_synchronize();
            if (wrapper->active != 0u) {
                context = claim_context();
            }
            if (context == NULL) {
                __sync_fetch_and_sub(&wrapper->inflight, 1u);
            }
        }
    }

    if (context == NULL) {
        frame->r12_or_target = target;
        frame->lr_or_return = original_lr;
        return;
    }

    sequence = __sync_fetch_and_add(&g_sequence, 1u);
    record_index = claim_record(sequence);
    context->entry_sp = entry_sp;
    context->original_lr = original_lr;
    context->record_index = record_index;
    context->wrapper = wrapper;

    if (record_index != UINT32_MAX) {
        MockRawCallbackRecord *record = &g_records[record_index].record;
        memset(record, 0, sizeof(*record));
        record->magic = MOCK_RAW_CALLBACK_RECORD_MAGIC;
        record->version = MOCK_RAW_CALLBACK_SCHEMA_VERSION;
        record->size = (uint16_t)sizeof(*record);
        record->sequence = sequence;
        memcpy(record->binary_id, wrapper->binary_id,
               MOCK_RAW_CALLBACK_BINARY_ID_SIZE);
        record->adapter = slot->adapter;
        record->slot = slot->slot;
        record->interface_id = slot->interface_id;
        record->flags = MOCK_RAW_CALLBACK_FLAG_STACK_BOUNDED |
                        MOCK_RAW_CALLBACK_FLAG_NO_TIME_OR_THREAD;
        if (self_lane == 1u) {
            record->flags |= MOCK_RAW_CALLBACK_FLAG_SELF_IN_R1;
        }
        record->r[0] = frame->r0;
        record->r[1] = frame->r1;
        record->r[2] = frame->r2;
        record->r[3] = frame->r3;
        record->entry_sp = entry_sp;
        record->entry_lr = original_lr;
        record->entry_cpsr = cpsr;
        record->target_pc = target;
        record->original_self = ptr32(original_self);
        record->stack_word_count = readable_stack_words(entry_sp);
        for (index = 0u; index < record->stack_word_count; ++index) {
            record->stack_words[index] =
                ((const uint32_t *)(uintptr_t)entry_sp)[index];
        }
    }
    __sync_synchronize();
    frame->r12_or_target = target;
    frame->lr_or_return = ptr32(mock_raw_callback_return);
}

uint32_t mock_raw_callback_complete(uint32_t entry_sp,
                                    const uint32_t result_registers[6])
{
    uint32_t index;

    for (index = 0u; index < MOCK_RAW_CONTEXT_CAPACITY; ++index) {
        MockRawReturnContext *context = &g_contexts[index];
        if (context->active != 0u && context->entry_sp == entry_sp) {
            uint32_t original_lr = context->original_lr;
            uint32_t record_index = context->record_index;
            MockRawCallbackWrapper *wrapper = context->wrapper;
            if (record_index != UINT32_MAX) {
                MockRawCallbackRecord *record = &g_records[record_index].record;
                record->return_r0 = result_registers[0];
                record->return_r1 = result_registers[1];
                record->flags |= MOCK_RAW_CALLBACK_FLAG_RETURN_CAPTURED;
                __sync_synchronize();
                g_records[record_index].state = CELL_READY;
            }
            __sync_fetch_and_sub(&wrapper->inflight, 1u);
            __sync_synchronize();
            context->active = 0u;
            return original_lr;
        }
    }
    /* A claimed context is never discarded.  Trap instead of misreturning. */
    for (;;) {
    }
}

uint32_t mock_raw_callback_drain(MockRawCallbackRecord *records,
                                 uint32_t capacity)
{
    uint32_t copied = 0u;
    uint32_t index;

    if (records == NULL) {
        return 0u;
    }
    for (index = 0u; index < MOCK_RAW_CALLBACK_RECORD_CAPACITY &&
                     copied < capacity; ++index) {
        if (__sync_bool_compare_and_swap(&g_records[index].state,
                                         CELL_READY, CELL_WRITING)) {
            records[copied++] = g_records[index].record;
            __sync_synchronize();
            g_records[index].state = CELL_FREE;
        }
    }
    return copied;
}

uint32_t mock_raw_callback_dropped(void)
{
    return g_dropped;
}

uint32_t mock_raw_callback_context_dropped(void)
{
    return g_context_dropped;
}

void mock_raw_callback_capture_reset(void)
{
    memset(g_records, 0, sizeof(g_records));
    memset(g_contexts, 0, sizeof(g_contexts));
    g_sequence = 0u;
    g_dropped = 0u;
    g_context_dropped = 0u;
}

#ifdef MOCK_RAW_CALLBACK_TESTING
int mock_raw_callback_test_publish(const MockRawCallbackRecord *record)
{
    uint32_t sequence;
    uint32_t index;

    if (record == NULL) {
        return -1;
    }
    sequence = __sync_fetch_and_add(&g_sequence, 1u);
    index = claim_record(sequence);
    if (index == UINT32_MAX) {
        return 1;
    }
    g_records[index].record = *record;
    g_records[index].record.sequence = sequence;
    __sync_synchronize();
    g_records[index].state = CELL_READY;
    return 0;
}
#endif
