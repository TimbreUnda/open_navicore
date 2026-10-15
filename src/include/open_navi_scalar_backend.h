#ifndef OPEN_NAVI_SCALAR_BACKEND_H
#define OPEN_NAVI_SCALAR_BACKEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void open_navi_scalar_backend_vehicle_day_night_status(uint8_t value);
void open_navi_scalar_backend_vehicle_day_night_setting(uint8_t value);
void open_navi_scalar_backend_vehicle_speed(float value);
void open_navi_scalar_backend_gps_interval(uint32_t interval_ms);
void open_navi_scalar_backend_audio(uint32_t audio_source, uint8_t volume);
void open_navi_scalar_backend_reset(void);

#if defined(OPEN_NAVI_TESTING)
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
} OpenNaviScalarBackendTestState;

void open_navi_scalar_backend_test_state(OpenNaviScalarBackendTestState *state_out);
#endif

#ifdef __cplusplus
}
#endif

#endif
