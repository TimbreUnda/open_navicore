#ifndef MOCK_RAW_CALLBACK_CAPTURE_H
#define MOCK_RAW_CALLBACK_CAPTURE_H

#include <stdint.h>

enum {
    MOCK_RAW_CALLBACK_SCHEMA_VERSION = 1,
    MOCK_RAW_CALLBACK_BINARY_ID_SIZE = 32,
    MOCK_RAW_CALLBACK_STACK_WORDS = 8,
    MOCK_RAW_CALLBACK_MAX_VTABLE_SLOTS = 36,
    MOCK_RAW_CALLBACK_RECORD_CAPACITY = 256,
    MOCK_RAW_CALLBACK_ROOT = UINT32_MAX,
    MOCK_RAW_CALLBACK_RECORD_MAGIC = 0x4d524342u,
    MOCK_RAW_CALLBACK_WRAPPER_MAGIC = 0x4d525750u
};

enum {
    MOCK_RAW_CALLBACK_FLAG_RETURN_CAPTURED = 1u << 0,
    MOCK_RAW_CALLBACK_FLAG_STACK_BOUNDED = 1u << 1,
    MOCK_RAW_CALLBACK_FLAG_NO_TIME_OR_THREAD = 1u << 2,
    MOCK_RAW_CALLBACK_FLAG_SELF_IN_R1 = 1u << 3
};

/* Stable, little-endian on-disk record: mock-nav-raw-callback/v1. */
typedef struct MockRawCallbackRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    uint8_t binary_id[MOCK_RAW_CALLBACK_BINARY_ID_SIZE];
    uint32_t adapter;
    uint16_t slot;
    uint16_t interface_id;
    uint32_t flags;
    uint32_t r[4];
    uint32_t entry_sp;
    uint32_t entry_lr;
    uint32_t entry_cpsr;
    uint32_t target_pc;
    uint32_t original_self;
    uint32_t stack_word_count;
    uint32_t stack_words[MOCK_RAW_CALLBACK_STACK_WORDS];
    uint32_t return_r0;
    uint32_t return_r1;
} MockRawCallbackRecord;

/*
 * Storage is caller-owned.  The wrapper must outlive its registration with
 * PAS.  destroy returns busy until every forwarded invocation has returned.
 */
typedef struct MockRawCallbackWrapper {
    void **vptr;
    void *shadow_vtable[MOCK_RAW_CALLBACK_MAX_VTABLE_SLOTS];
    void *original_self;
    const void *const *original_vtable;
    /* Optional trusted registration slot used by late-bound PAS proxies. */
    void *volatile *original_self_slot;
    uint8_t binary_id[MOCK_RAW_CALLBACK_BINARY_ID_SIZE];
    uint32_t adapter;
    uint32_t vtable_slots;
    uint32_t registry_slot;
    volatile uint32_t inflight;
    volatile uint32_t active;
    uint32_t magic;
} MockRawCallbackWrapper;

int mock_raw_callback_wrapper_init(
    MockRawCallbackWrapper *wrapper, uint32_t adapter, void *original_self,
    const uint8_t binary_id[MOCK_RAW_CALLBACK_BINARY_ID_SIZE]);
int mock_raw_callback_wrapper_init_indirect(
    MockRawCallbackWrapper *wrapper, uint32_t adapter,
    void *volatile *original_self_slot,
    const uint8_t binary_id[MOCK_RAW_CALLBACK_BINARY_ID_SIZE]);
void *mock_raw_callback_wrapper_proxy(MockRawCallbackWrapper *wrapper);
int mock_raw_callback_wrapper_destroy(MockRawCallbackWrapper *wrapper);

/* Single-consumer drain.  Capture remains nonblocking if the ring is full. */
uint32_t mock_raw_callback_drain(MockRawCallbackRecord *records,
                                 uint32_t capacity);
uint32_t mock_raw_callback_dropped(void);
uint32_t mock_raw_callback_context_dropped(void);
void mock_raw_callback_capture_reset(void);
/* Register known-readable stack bounds outside callback context. */
int mock_raw_callback_register_stack(uint32_t low, uint32_t high);
int mock_raw_callback_unregister_stack(uint32_t low, uint32_t high);

#endif
