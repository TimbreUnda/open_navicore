#ifndef MOCK_CALLBACK_BACKEND_H
#define MOCK_CALLBACK_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include <open_navicore/cluster.h>

enum { MOCK_THREAD_NAME_LIMIT = 63 };

typedef enum {
    MOCK_CALLBACK_OPTIONAL = 0,
    MOCK_CALLBACK_CRITICAL = 1
} MockCallbackClass;

uint32_t mock_callback_allocate_handle(void);
void mock_callback_pending_add(uint32_t adapter, uint32_t provider_slot,
                               uint32_t handle);
void mock_callback_pending_cancel(uint32_t adapter, uint32_t provider_slot,
                                  uint32_t handle);
int mock_callback_pending_complete(uint32_t adapter, uint32_t provider_slot,
                                   uint32_t handle);
void mock_callback_record_status(uint32_t adapter, uint32_t slot,
                                 uint32_t status, MockCallbackClass kind);
int mock_callback_audio_begin(uint32_t state, uint32_t *attempt);
void mock_callback_audio_complete(uint32_t state, uint32_t attempt,
                                  int delivered);
void mock_callback_set_voice_state(uint32_t result, uint32_t guidance_status);
void mock_callback_get_voice_state(uint32_t *result, uint32_t *guidance_status);
void mock_callback_store_cluster(const void *record);
void mock_callback_store_thread(uint32_t id, const char *name, uint32_t status);
void mock_callback_backend_reset(void);
uint32_t mock_callback_signal_audio_availability(uint32_t state);
uint32_t mock_callback_send_cluster(const void *record_0xa5c);
uint32_t mock_callback_signal_thread_status(uint32_t thread_id,
                                            const char *thread_name,
                                            uint32_t thread_status);

typedef struct {
    uint32_t allocated_count;
    uint32_t pending_count;
    uint32_t correlation_successes;
    uint32_t correlation_failures;
    uint32_t callback_count;
    uint32_t critical_failures;
    uint32_t optional_failures;
    uint32_t last_adapter;
    uint32_t last_slot;
    uint32_t last_status;
    uint32_t audio_transitions;
    uint32_t voice_result;
    uint32_t voice_guidance_status;
    uint32_t cluster_generation;
    uint8_t cluster_record[MOCK_CLUSTER_RECORD_SIZE];
    uint32_t thread_generation;
    uint32_t thread_id;
    uint32_t thread_status;
    char thread_name[MOCK_THREAD_NAME_LIMIT + 1];
} MockCallbackTestState;
#if defined(MOCK_NAVICORE_TESTING)
void mock_callback_test_state(MockCallbackTestState *out);
void mock_callback_test_set_next_handle(uint32_t next);
#endif

#endif
