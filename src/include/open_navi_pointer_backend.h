#ifndef OPEN_NAVI_POINTER_BACKEND_H
#define OPEN_NAVI_POINTER_BACKEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OPEN_NAVI_LOGGING_OBSERVED_COUNT = 4,
    OPEN_NAVI_INTL_SETTINGS_SIZE = 4,
    OPEN_NAVI_VIN_SIZE = 17,
    OPEN_NAVI_TRANSPORT_OBSERVED_COUNT = 1,
    OPEN_NAVI_CONNECTION_STRING_LIMIT = 128,
    OPEN_NAVI_TEF_FILENAME_LIMIT = 512
};

typedef struct {
    uint16_t language;
    uint8_t date_format;
    uint8_t time_format;
} OpenNaviIntlSettingsInput;

typedef struct {
    uint8_t kind;
    uint8_t padding[7];
    uint32_t buffer_size;
    uint32_t field_0c;
} OpenNaviTransportMeansObservedInput;

typedef struct {
    const char *imei;
    uint32_t field_04;
    const char *msisdn;
    const char *sim_iccid;
} OpenNaviConnectionDeviceInfoInput;

void open_navi_pointer_backend_logging(uint32_t handle, uint32_t count,
                                  const uint8_t *levels,
                                  const uint32_t *sizes);
void open_navi_pointer_backend_internationalization(uint32_t handle,
    const OpenNaviIntlSettingsInput *settings);
void open_navi_pointer_backend_vin(uint32_t handle, const char *vin17);
void open_navi_pointer_backend_transport(uint32_t handle, uint32_t count,
    const OpenNaviTransportMeansObservedInput *items);
void open_navi_pointer_backend_connection(
    uint32_t handle, uint32_t service,
    const OpenNaviConnectionDeviceInfoInput *info);
void open_navi_pointer_backend_tef(uint32_t handle, uint32_t category,
                              const char *filename);
void open_navi_pointer_backend_reset(void);

typedef struct {
    uint32_t generation;
    uint32_t logging_calls;
    uint32_t logging_handle;
    uint32_t logging_count;
    uint8_t logging_supported;
    uint8_t logging_present;
    uint8_t logging_levels[4];
    uint32_t logging_sizes[4];
    uint32_t intl_calls;
    uint32_t intl_handle;
    uint8_t intl_present;
    uint16_t intl_language;
    uint8_t intl_date_format;
    uint8_t intl_time_format;
    uint32_t vin_calls;
    uint32_t vin_handle;
    uint8_t vin_present;
    char vin[18];
    uint32_t transport_calls;
    uint32_t transport_handle;
    uint32_t transport_count;
    uint8_t transport_supported;
    uint8_t transport_present;
    uint8_t transport_kind;
    uint32_t transport_buffer_size;
    uint32_t transport_field_0c;
    uint32_t connection_calls;
    uint32_t connection_handle;
    uint32_t connection_service;
    uint8_t connection_info_present;
    uint32_t connection_field_04;
    uint8_t imei_present;
    uint8_t msisdn_present;
    uint8_t sim_iccid_present;
    uint32_t imei_length;
    uint32_t msisdn_length;
    uint32_t sim_iccid_length;
    char imei[129];
    char msisdn[129];
    char sim_iccid[129];
    uint32_t tef_calls;
    uint32_t tef_handle;
    uint32_t tef_category;
    uint8_t tef_present;
    uint32_t tef_length;
    char tef_filename[513];
} OpenNaviPointerBackendTestState;

#if defined(OPEN_NAVI_TESTING)
void open_navi_pointer_backend_test_state(OpenNaviPointerBackendTestState *state_out);
#endif

#ifdef __cplusplus
}
#endif

#endif
