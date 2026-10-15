#ifndef OPEN_NAVI_API_TELEMETRY_H
#define OPEN_NAVI_API_TELEMETRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void open_navi_api_telemetry_record(uint32_t adapter,
                               uint32_t slot,
                               uint32_t argument_count,
                               uint32_t argument0,
                               uint32_t argument1,
                               uint32_t argument2,
                               uint32_t argument3);
void open_navi_api_telemetry_reset(void);

#if defined(OPEN_NAVI_TESTING)
typedef struct {
    uint32_t call_count;
    uint32_t argument_count;
    uint32_t argument[4];
} OpenNaviApiTelemetryRecord;

int open_navi_api_telemetry_test_get(uint32_t adapter,
                                uint32_t slot,
                                OpenNaviApiTelemetryRecord *record_out);
#endif

#ifdef __cplusplus
}
#endif

#endif
