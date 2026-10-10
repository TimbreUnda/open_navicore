#ifndef MOCK_RAW_CALLBACK_CAPTURE_INTERNAL_H
#define MOCK_RAW_CALLBACK_CAPTURE_INTERNAL_H

#include "mock_raw_callback_capture.h"

typedef void (*MockRawCallbackTrampoline)(void);

typedef struct MockRawCallbackSlot {
    uint32_t adapter;
    uint16_t slot;
    uint16_t interface_id;
    const char *interface_name;
    MockRawCallbackTrampoline trampoline;
} MockRawCallbackSlot;

/* Layout is coupled to mock_raw_callback_trampolines_arm.S. */
typedef struct MockRawCallbackEntryFrame {
    uint32_t cpsr;
    uint32_t pad;
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12_or_target;
    uint32_t lr_or_return;
} MockRawCallbackEntryFrame;

extern const MockRawCallbackSlot mock_raw_callback_slots[];
extern const uint32_t mock_raw_callback_slot_count;
extern void mock_raw_callback_return(void);

void mock_raw_callback_prepare(MockRawCallbackEntryFrame *frame,
                               uint32_t slot_id, uint32_t cpsr);
uint32_t mock_raw_callback_complete(uint32_t entry_sp,
                                    const uint32_t result_registers[6]);

#ifdef MOCK_RAW_CALLBACK_TESTING
int mock_raw_callback_test_publish(const MockRawCallbackRecord *record);
#endif

#endif
