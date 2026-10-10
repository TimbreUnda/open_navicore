#include "mock_raw_callback_capture.h"
#include "mock_raw_callback_capture_internal.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct FakeProxy { void **vtable; } FakeProxy;

int main(void)
{
    void *vtable[MOCK_RAW_CALLBACK_MAX_VTABLE_SLOTS];
    FakeProxy proxy = { vtable };
    void *late_proxy = NULL;
    MockRawCallbackWrapper wrapper;
    MockRawCallbackRecord input;
    MockRawCallbackRecord output[MOCK_RAW_CALLBACK_RECORD_CAPACITY];
    uint8_t binary_id[MOCK_RAW_CALLBACK_BINARY_ID_SIZE];
    uint32_t index;

    for (index = 0u; index < MOCK_RAW_CALLBACK_MAX_VTABLE_SLOTS; ++index) {
        vtable[index] = (void *)(uintptr_t)(0x1000u + index * 4u);
    }
    memset(binary_id, 0xa5, sizeof(binary_id));
    assert(mock_raw_callback_wrapper_init(&wrapper, 25u, &proxy,
                                          binary_id) == 0);
    assert(mock_raw_callback_wrapper_proxy(&wrapper) == &wrapper);
    assert(wrapper.vptr == wrapper.shadow_vtable);
    assert(wrapper.vtable_slots == 36u);
    assert(wrapper.shadow_vtable[0] == vtable[0] &&
           wrapper.shadow_vtable[1] == vtable[1]);
    /* Host mode uses NULL in place of ARM code but still proves all rewrites. */
    for (index = 2u; index <= 35u; ++index) {
        assert(wrapper.shadow_vtable[index] == NULL);
    }
    wrapper.inflight = 1u;
    assert(mock_raw_callback_wrapper_destroy(&wrapper) == 1);
    assert(mock_raw_callback_wrapper_proxy(&wrapper) == NULL);
    wrapper.inflight = 0u;
    assert(mock_raw_callback_wrapper_destroy(&wrapper) == 0);

    assert(mock_raw_callback_wrapper_init_indirect(&wrapper, 25u,
                                                   &late_proxy,
                                                   binary_id) == 0);
    assert(mock_raw_callback_wrapper_proxy(&wrapper) == &wrapper);
    assert(wrapper.original_self == NULL &&
           wrapper.original_self_slot == &late_proxy);
    late_proxy = &proxy;
    assert(wrapper.original_self_slot != NULL &&
           *wrapper.original_self_slot == &proxy);
    assert(mock_raw_callback_wrapper_destroy(&wrapper) == 0);

    mock_raw_callback_capture_reset();
    assert(mock_raw_callback_register_stack(0u, 4u) == -1);
    assert(mock_raw_callback_register_stack(0x1000u, 0x2000u) == 0);
    assert(mock_raw_callback_unregister_stack(0x1000u, 0x2000u) == 0);
    assert(mock_raw_callback_unregister_stack(0x1000u, 0x2000u) == -1);
    memset(&input, 0, sizeof(input));
    input.magic = MOCK_RAW_CALLBACK_RECORD_MAGIC;
    input.version = MOCK_RAW_CALLBACK_SCHEMA_VERSION;
    input.size = (uint16_t)sizeof(input);
    for (index = 0u; index < MOCK_RAW_CALLBACK_RECORD_CAPACITY; ++index) {
        input.slot = (uint16_t)index;
        assert(mock_raw_callback_test_publish(&input) == 0);
    }
    assert(mock_raw_callback_test_publish(&input) == 1);
    assert(mock_raw_callback_dropped() == 1u);
    assert(mock_raw_callback_drain(output,
                                   MOCK_RAW_CALLBACK_RECORD_CAPACITY) ==
           MOCK_RAW_CALLBACK_RECORD_CAPACITY);
    for (index = 0u; index < MOCK_RAW_CALLBACK_RECORD_CAPACITY; ++index) {
        assert(output[index].magic == MOCK_RAW_CALLBACK_RECORD_MAGIC);
    }
    assert(mock_raw_callback_drain(output, 1u) == 0u);
    assert(mock_raw_callback_test_publish(&input) == 0);
    assert(mock_raw_callback_drain(output, 1u) == 1u);
    return 0;
}
