#ifndef MOCK_SCALAR_BACKEND_H
#define MOCK_SCALAR_BACKEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void mock_scalar_backend_vehicle_day_night_status(uint8_t value);
void mock_scalar_backend_vehicle_day_night_setting(uint8_t value);
void mock_scalar_backend_vehicle_speed(float value);
void mock_scalar_backend_gps_interval(uint32_t interval_ms);
void mock_scalar_backend_audio(uint32_t audio_source, uint8_t volume);
void mock_scalar_backend_reset(void);

#if defined(MOCK_NAVICORE_TESTING)
typedef struct {
    uint8_t day_night_status;
    uint8_t day_night_setting;
    float displayed_vehicle_speed;
    uint32_t displayed_vehicle_speed_bits;
    uint32_t gps_interval_ms;
    uint32_t audio_source;
    uint8_t volume;
    uint32_t vehicle_generation;
    uint32_t gps_interval_generation;
    uint32_t audio_generation;
    uint32_t day_night_status_calls;
    uint32_t day_night_setting_calls;
    uint32_t vehicle_speed_calls;
    uint32_t gps_interval_calls;
    uint32_t audio_calls;
} MockScalarBackendTestState;

void mock_scalar_backend_test_state(MockScalarBackendTestState *state_out);
#endif

#ifdef __cplusplus
}
#endif

#endif
