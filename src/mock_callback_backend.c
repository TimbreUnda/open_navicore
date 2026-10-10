#include "mock_callback_backend.h"
#include "mock_counter.h"

#include <pthread.h>
#include <string.h>

enum { MOCK_PENDING_CAPACITY = 32 };

typedef struct {
    uint32_t adapter;
    uint32_t slot;
    uint32_t handle;
    int active;
} MockPendingRequest;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static MockCallbackTestState g_state;
static MockPendingRequest g_pending[MOCK_PENDING_CAPACITY];
static uint32_t g_next_handle = 1u;
static uint32_t g_audio_state;
static uint32_t g_audio_latest_attempt;
static int g_audio_valid;

#if defined(__GNUC__)
extern int mock_navicore_callback_telemetry_active(void)
    __attribute__((weak));
#else
extern int mock_navicore_callback_telemetry_active(void);
#endif

uint32_t mock_callback_allocate_handle(void)
{
    uint32_t value;

    (void)pthread_mutex_lock(&g_lock);
    if (g_next_handle == 0u) {
        g_next_handle = 1u;
    }
    value = g_next_handle++;
    if (g_next_handle == 0u) {
        g_next_handle = 1u;
    }
    mock_increment_saturating(&g_state.allocated_count);
    (void)pthread_mutex_unlock(&g_lock);
    return value;
}

void mock_callback_pending_add(uint32_t adapter,
                               uint32_t slot,
                               uint32_t handle)
{
    unsigned int index;

    (void)pthread_mutex_lock(&g_lock);
    for (index = 0u; index < MOCK_PENDING_CAPACITY; ++index) {
        if (g_pending[index].active == 0) {
            g_pending[index].adapter = adapter;
            g_pending[index].slot = slot;
            g_pending[index].handle = handle;
            g_pending[index].active = 1;
            mock_increment_saturating(&g_state.pending_count);
            break;
        }
    }
    (void)pthread_mutex_unlock(&g_lock);
}

/* Remove the first matching request while g_lock is held. Cancellation and
 * completion share removal, but only completion records correlation status. */
static int mock_pending_remove_locked(uint32_t adapter,
                                      uint32_t slot,
                                      uint32_t handle)
{
    unsigned int index;

    for (index = 0u; index < MOCK_PENDING_CAPACITY; ++index) {
        if (g_pending[index].active != 0 &&
            g_pending[index].adapter == adapter &&
            g_pending[index].slot == slot &&
            g_pending[index].handle == handle) {
            g_pending[index].active = 0;
            if (g_state.pending_count != 0u) {
                --g_state.pending_count;
            }
            return 1;
        }
    }
    return 0;
}

void mock_callback_pending_cancel(uint32_t adapter,
                                  uint32_t slot,
                                  uint32_t handle)
{
    (void)pthread_mutex_lock(&g_lock);
    (void)mock_pending_remove_locked(adapter, slot, handle);
    (void)pthread_mutex_unlock(&g_lock);
}

int mock_callback_pending_complete(uint32_t adapter,
                                   uint32_t slot,
                                   uint32_t handle)
{
    int found;

    (void)pthread_mutex_lock(&g_lock);
    found = mock_pending_remove_locked(adapter, slot, handle);
    if (found != 0) {
        mock_increment_saturating(&g_state.correlation_successes);
    } else {
        mock_increment_saturating(&g_state.correlation_failures);
    }
    (void)pthread_mutex_unlock(&g_lock);
    return found;
}

void mock_callback_record_status(uint32_t adapter,
                                 uint32_t slot,
                                 uint32_t status,
                                 MockCallbackClass kind)
{
    /* Standalone ABI-seam tests intentionally have no lifecycle owner.  When
     * linked into the provider, only the provider's leased production path is
     * allowed to mutate callback telemetry. */
    if (mock_navicore_callback_telemetry_active != NULL &&
        !mock_navicore_callback_telemetry_active()) {
        return;
    }
    (void)pthread_mutex_lock(&g_lock);
    mock_increment_saturating(&g_state.callback_count);
    g_state.last_adapter = adapter;
    g_state.last_slot = slot;
    g_state.last_status = status;
    if (status != 0u) {
        if (kind == MOCK_CALLBACK_CRITICAL) {
            mock_increment_saturating(&g_state.critical_failures);
        } else {
            mock_increment_saturating(&g_state.optional_failures);
        }
    }
    (void)pthread_mutex_unlock(&g_lock);
}

int mock_callback_audio_begin(uint32_t state, uint32_t *attempt)
{
    int changed;

    (void)pthread_mutex_lock(&g_lock);
    changed = g_audio_valid == 0 || g_audio_state != state;
    if (changed != 0) {
        ++g_audio_latest_attempt;
        if (g_audio_latest_attempt == 0u) {
            g_audio_latest_attempt = 1u;
        }
        if (attempt != NULL) {
            *attempt = g_audio_latest_attempt;
        }
    }
    (void)pthread_mutex_unlock(&g_lock);
    return changed;
}

void mock_callback_audio_complete(uint32_t state,
                                  uint32_t attempt,
                                  int delivered)
{
    (void)pthread_mutex_lock(&g_lock);
    if (delivered != 0 && attempt == g_audio_latest_attempt) {
        g_audio_state = state;
        g_audio_valid = 1;
        mock_increment_saturating(&g_state.audio_transitions);
    }
    (void)pthread_mutex_unlock(&g_lock);
}

void mock_callback_set_voice_state(uint32_t result,
                                   uint32_t guidance_status)
{
    (void)pthread_mutex_lock(&g_lock);
    g_state.voice_result = result;
    g_state.voice_guidance_status = guidance_status;
    (void)pthread_mutex_unlock(&g_lock);
}

void mock_callback_get_voice_state(uint32_t *result,
                                   uint32_t *guidance_status)
{
    (void)pthread_mutex_lock(&g_lock);
    *result = g_state.voice_result;
    *guidance_status = g_state.voice_guidance_status;
    (void)pthread_mutex_unlock(&g_lock);
}

void mock_callback_store_cluster(const void *record)
{
    (void)pthread_mutex_lock(&g_lock);
    memcpy(g_state.cluster_record, record, MOCK_CLUSTER_RECORD_SIZE);
    mock_increment_saturating(&g_state.cluster_generation);
    (void)pthread_mutex_unlock(&g_lock);
}

void mock_callback_store_thread(uint32_t id,
                                const char *name,
                                uint32_t status)
{
    size_t length = 0u;

    (void)pthread_mutex_lock(&g_lock);
    while (length < MOCK_THREAD_NAME_LIMIT &&
           name != NULL && name[length] != '\0') {
        g_state.thread_name[length] = name[length];
        ++length;
    }
    g_state.thread_name[length] = '\0';
    g_state.thread_id = id;
    g_state.thread_status = status;
    mock_increment_saturating(&g_state.thread_generation);
    (void)pthread_mutex_unlock(&g_lock);
}

void mock_callback_backend_reset(void)
{
    (void)pthread_mutex_lock(&g_lock);
    memset(&g_state, 0, sizeof(g_state));
    memset(g_pending, 0, sizeof(g_pending));
    g_next_handle = 1u;
    g_audio_valid = 0;
    g_audio_state = 0u;
    g_audio_latest_attempt = 0u;
    g_state.voice_result = 2u;
    g_state.voice_guidance_status = 1u;
    (void)pthread_mutex_unlock(&g_lock);
}

#if defined(MOCK_NAVICORE_TESTING)
void mock_callback_test_state(MockCallbackTestState *out)
{
    (void)pthread_mutex_lock(&g_lock);
    if (out != NULL) {
        *out = g_state;
    }
    (void)pthread_mutex_unlock(&g_lock);
}

void mock_callback_test_set_next_handle(uint32_t next)
{
    (void)pthread_mutex_lock(&g_lock);
    g_next_handle = next;
    (void)pthread_mutex_unlock(&g_lock);
}
#endif
