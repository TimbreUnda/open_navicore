#ifndef OPEN_NAVI_CALLBACK_BACKEND_H
#define OPEN_NAVI_CALLBACK_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include <open_navicore/cluster.h>

enum { OPEN_NAVI_THREAD_NAME_LIMIT = 63 };

typedef enum {
    OPEN_NAVI_CALLBACK_OPTIONAL = 0,
    OPEN_NAVI_CALLBACK_CRITICAL = 1
} OpenNaviCallbackClass;

uint32_t open_navi_callback_allocate_handle(void);
void open_navi_callback_pending_add(uint32_t adapter, uint32_t provider_slot,
                               uint32_t handle);
void open_navi_callback_pending_cancel(uint32_t adapter, uint32_t provider_slot,
                                  uint32_t handle);
int open_navi_callback_pending_complete(uint32_t adapter, uint32_t provider_slot,
                                   uint32_t handle);
void open_navi_callback_record_status(uint32_t adapter, uint32_t slot,
                                 uint32_t status, OpenNaviCallbackClass kind);
int open_navi_callback_audio_begin(uint32_t state, uint32_t *attempt);
void open_navi_callback_audio_complete(uint32_t state, uint32_t attempt,
                                  int delivered);
void open_navi_callback_set_voice_state(uint32_t result, uint32_t guidance_status);
void open_navi_callback_get_voice_state(uint32_t *result, uint32_t *guidance_status);
void open_navi_callback_store_cluster(const void *record);
void open_navi_callback_store_thread(uint32_t id, const char *name, uint32_t status);
void open_navi_callback_backend_reset(void);
uint32_t open_navi_callback_signal_audio_availability(uint32_t state);
uint32_t open_navi_callback_send_cluster(const void *record_0xa5c);
uint32_t open_navi_callback_signal_thread_status(uint32_t thread_id,
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
    uint8_t cluster_record[OPEN_NAVI_CLUSTER_RECORD_SIZE];
    uint32_t thread_generation;
    uint32_t thread_id;
    uint32_t thread_status;
    char thread_name[OPEN_NAVI_THREAD_NAME_LIMIT + 1];
} OpenNaviCallbackTestState;
#if defined(OPEN_NAVI_TESTING)
void open_navi_callback_test_state(OpenNaviCallbackTestState *out);
void open_navi_callback_test_set_next_handle(uint32_t next);
#endif

#endif
