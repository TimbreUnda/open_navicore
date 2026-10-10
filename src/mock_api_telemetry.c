#include "mock_api_telemetry.h"
#include "mock_counter.h"

#include <pthread.h>
#include <string.h>

#define MOCK_TELEMETRY_ADAPTERS 32u
#define MOCK_TELEMETRY_SLOTS 128u

typedef struct {
    uint32_t call_count;
    uint32_t argument_count;
    uint32_t argument[4];
} MockApiTelemetryState;

static pthread_mutex_t g_telemetry_lock = PTHREAD_MUTEX_INITIALIZER;
static MockApiTelemetryState
    g_telemetry[MOCK_TELEMETRY_ADAPTERS][MOCK_TELEMETRY_SLOTS];

void mock_api_telemetry_record(uint32_t adapter,
                               uint32_t slot,
                               uint32_t argument_count,
                               uint32_t argument0,
                               uint32_t argument1,
                               uint32_t argument2,
                               uint32_t argument3)
{
    MockApiTelemetryState *state;

    if (adapter >= MOCK_TELEMETRY_ADAPTERS ||
        slot >= MOCK_TELEMETRY_SLOTS) {
        return;
    }
    (void)pthread_mutex_lock(&g_telemetry_lock);
    state = &g_telemetry[adapter][slot];
    mock_increment_saturating(&state->call_count);
    state->argument_count = argument_count <= 4u ? argument_count : 4u;
    state->argument[0] = argument0;
    state->argument[1] = argument1;
    state->argument[2] = argument2;
    state->argument[3] = argument3;
    (void)pthread_mutex_unlock(&g_telemetry_lock);
}

void mock_api_telemetry_reset(void)
{
    (void)pthread_mutex_lock(&g_telemetry_lock);
    memset(g_telemetry, 0, sizeof(g_telemetry));
    (void)pthread_mutex_unlock(&g_telemetry_lock);
}

#if defined(MOCK_NAVICORE_TESTING)
int mock_api_telemetry_test_get(uint32_t adapter,
                                uint32_t slot,
                                MockApiTelemetryRecord *record_out)
{
    int present;

    if (adapter >= MOCK_TELEMETRY_ADAPTERS ||
        slot >= MOCK_TELEMETRY_SLOTS) {
        return 0;
    }
    (void)pthread_mutex_lock(&g_telemetry_lock);
    present = g_telemetry[adapter][slot].call_count != 0u;
    if (record_out != NULL) {
        memcpy(record_out, &g_telemetry[adapter][slot],
               sizeof(*record_out));
    }
    (void)pthread_mutex_unlock(&g_telemetry_lock);
    return present;
}
#endif
