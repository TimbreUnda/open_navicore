#include "mock_scalar_backend.h"
#include "mock_counter.h"

#include <pthread.h>
#include <string.h>

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
} MockScalarBackendState;

static pthread_mutex_t g_scalar_lock = PTHREAD_MUTEX_INITIALIZER;
static MockScalarBackendState g_scalar_state;

void mock_scalar_backend_vehicle_day_night_status(uint8_t value)
{
    (void)pthread_mutex_lock(&g_scalar_lock);
    g_scalar_state.day_night_status = value;
    mock_increment_saturating(&g_scalar_state.day_night_status_calls);
    mock_increment_saturating(&g_scalar_state.vehicle_generation);
    (void)pthread_mutex_unlock(&g_scalar_lock);
}

void mock_scalar_backend_vehicle_day_night_setting(uint8_t value)
{
    (void)pthread_mutex_lock(&g_scalar_lock);
    g_scalar_state.day_night_setting = value;
    mock_increment_saturating(&g_scalar_state.day_night_setting_calls);
    mock_increment_saturating(&g_scalar_state.vehicle_generation);
    (void)pthread_mutex_unlock(&g_scalar_lock);
}

void mock_scalar_backend_vehicle_speed(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    (void)pthread_mutex_lock(&g_scalar_lock);
    g_scalar_state.displayed_vehicle_speed = value;
    g_scalar_state.displayed_vehicle_speed_bits = bits;
    mock_increment_saturating(&g_scalar_state.vehicle_speed_calls);
    mock_increment_saturating(&g_scalar_state.vehicle_generation);
    (void)pthread_mutex_unlock(&g_scalar_lock);
}

void mock_scalar_backend_gps_interval(uint32_t interval_ms)
{
    (void)pthread_mutex_lock(&g_scalar_lock);
    g_scalar_state.gps_interval_ms = interval_ms;
    mock_increment_saturating(&g_scalar_state.gps_interval_calls);
    mock_increment_saturating(&g_scalar_state.gps_interval_generation);
    (void)pthread_mutex_unlock(&g_scalar_lock);
}

void mock_scalar_backend_audio(uint32_t audio_source, uint8_t volume)
{
    (void)pthread_mutex_lock(&g_scalar_lock);
    g_scalar_state.audio_source = audio_source;
    g_scalar_state.volume = volume;
    mock_increment_saturating(&g_scalar_state.audio_calls);
    mock_increment_saturating(&g_scalar_state.audio_generation);
    (void)pthread_mutex_unlock(&g_scalar_lock);
}

void mock_scalar_backend_reset(void)
{
    (void)pthread_mutex_lock(&g_scalar_lock);
    memset(&g_scalar_state, 0, sizeof(g_scalar_state));
    (void)pthread_mutex_unlock(&g_scalar_lock);
}

#if defined(MOCK_NAVICORE_TESTING)
void mock_scalar_backend_test_state(MockScalarBackendTestState *state_out)
{
    (void)pthread_mutex_lock(&g_scalar_lock);
    if (state_out != NULL) {
        memcpy(state_out, &g_scalar_state, sizeof(*state_out));
    }
    (void)pthread_mutex_unlock(&g_scalar_lock);
}
#endif
