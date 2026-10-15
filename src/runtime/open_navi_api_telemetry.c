#include "open_navi_api_telemetry.h"
#include "open_navi_counter.h"

#include <pthread.h>
#include <string.h>

#define OPEN_NAVI_TELEMETRY_ADAPTERS 32u
#define OPEN_NAVI_TELEMETRY_SLOTS 128u

typedef struct {
    uint32_t call_count;
    uint32_t argument_count;
    uint32_t argument[4];
} OpenNaviApiTelemetryState;

static pthread_mutex_t g_telemetry_lock = PTHREAD_MUTEX_INITIALIZER;
static OpenNaviApiTelemetryState
    g_telemetry[OPEN_NAVI_TELEMETRY_ADAPTERS][OPEN_NAVI_TELEMETRY_SLOTS];

void open_navi_api_telemetry_record(uint32_t adapter,
                               uint32_t slot,
                               uint32_t argument_count,
                               uint32_t argument0,
                               uint32_t argument1,
                               uint32_t argument2,
                               uint32_t argument3)
{
    OpenNaviApiTelemetryState *state;

    if (adapter >= OPEN_NAVI_TELEMETRY_ADAPTERS ||
        slot >= OPEN_NAVI_TELEMETRY_SLOTS) {
        return;
    }
    (void)pthread_mutex_lock(&g_telemetry_lock);
    state = &g_telemetry[adapter][slot];
    open_navi_increment_saturating(&state->call_count);
    state->argument_count = argument_count <= 4u ? argument_count : 4u;
    state->argument[0] = argument0;
    state->argument[1] = argument1;
    state->argument[2] = argument2;
    state->argument[3] = argument3;
    (void)pthread_mutex_unlock(&g_telemetry_lock);
}

void open_navi_api_telemetry_reset(void)
{
    (void)pthread_mutex_lock(&g_telemetry_lock);
    memset(g_telemetry, 0, sizeof(g_telemetry));
    (void)pthread_mutex_unlock(&g_telemetry_lock);
}

#if defined(OPEN_NAVI_TESTING)
int open_navi_api_telemetry_test_get(uint32_t adapter,
                                uint32_t slot,
                                OpenNaviApiTelemetryRecord *record_out)
{
    int present;

    if (adapter >= OPEN_NAVI_TELEMETRY_ADAPTERS ||
        slot >= OPEN_NAVI_TELEMETRY_SLOTS) {
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
