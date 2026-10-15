#include "open_navi_pointer_backend.h"
#include "open_navi_counter.h"

#include <pthread.h>
#include <stddef.h>
#include <string.h>

#if UINTPTR_MAX == 0xffffffffu
typedef char connection_info_must_be_16_bytes[
    sizeof(OpenNaviConnectionDeviceInfoInput) == 16u ? 1 : -1];
typedef char connection_info_field_04_offset[
    offsetof(OpenNaviConnectionDeviceInfoInput, field_04) == 4u ? 1 : -1];
typedef char connection_info_msisdn_offset[
    offsetof(OpenNaviConnectionDeviceInfoInput, msisdn) == 8u ? 1 : -1];
typedef char connection_info_sim_offset[
    offsetof(OpenNaviConnectionDeviceInfoInput, sim_iccid) == 12u ? 1 : -1];
#endif

static pthread_mutex_t g_pointer_lock = PTHREAD_MUTEX_INITIALIZER;
static OpenNaviPointerBackendTestState g_pointer_state;

typedef char intl_settings_must_be_4_bytes[
    sizeof(OpenNaviIntlSettingsInput) == 4u ? 1 : -1];
typedef char transport_item_must_be_16_bytes[
    sizeof(OpenNaviTransportMeansObservedInput) == 16u ? 1 : -1];
typedef char transport_buffer_offset_must_be_8[
    offsetof(OpenNaviTransportMeansObservedInput, buffer_size) == 8u ? 1 : -1];

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint32_t copy_bounded_string(char *destination, uint32_t limit,
                                    const char *source)
{
    uint32_t length = 0u;

    if (source != NULL) {
        while (length < limit && source[length] != '\0') {
            destination[length] = source[length];
            ++length;
        }
    }
    destination[length] = '\0';
    return length;
}

void open_navi_pointer_backend_logging(uint32_t handle, uint32_t count,
                                  const uint8_t *levels,
                                  const uint32_t *sizes)
{
    uint8_t local_levels[4] = {0u, 0u, 0u, 0u};
    uint32_t local_sizes[4] = {0u, 0u, 0u, 0u};
    int supported = count == OPEN_NAVI_LOGGING_OBSERVED_COUNT;
    int present = supported && levels != NULL && sizes != NULL;

    if (present) {
        memcpy(local_levels, levels, sizeof(local_levels));
        memcpy(local_sizes, sizes, sizeof(local_sizes));
    }
    (void)pthread_mutex_lock(&g_pointer_lock);
    open_navi_increment_saturating(&g_pointer_state.generation);
    open_navi_increment_saturating(&g_pointer_state.logging_calls);
    g_pointer_state.logging_handle = handle;
    g_pointer_state.logging_count = count;
    g_pointer_state.logging_supported = (uint8_t)supported;
    g_pointer_state.logging_present = (uint8_t)present;
    memcpy(g_pointer_state.logging_levels, local_levels, sizeof(local_levels));
    memcpy(g_pointer_state.logging_sizes, local_sizes, sizeof(local_sizes));
    (void)pthread_mutex_unlock(&g_pointer_lock);
}

void open_navi_pointer_backend_internationalization(uint32_t handle,
    const OpenNaviIntlSettingsInput *settings)
{
    uint8_t bytes[OPEN_NAVI_INTL_SETTINGS_SIZE] = {0u, 0u, 0u, 0u};

    if (settings != NULL) {
        memcpy(bytes, settings, sizeof(bytes));
    }
    (void)pthread_mutex_lock(&g_pointer_lock);
    open_navi_increment_saturating(&g_pointer_state.generation);
    open_navi_increment_saturating(&g_pointer_state.intl_calls);
    g_pointer_state.intl_handle = handle;
    g_pointer_state.intl_present = (uint8_t)(settings != NULL);
    g_pointer_state.intl_language = read_le16(bytes);
    g_pointer_state.intl_date_format = bytes[2];
    g_pointer_state.intl_time_format = bytes[3];
    (void)pthread_mutex_unlock(&g_pointer_lock);
}

void open_navi_pointer_backend_vin(uint32_t handle, const char *vin17)
{
    char vin[OPEN_NAVI_VIN_SIZE + 1u];

    memset(vin, 0, sizeof(vin));
    if (vin17 != NULL) {
        memcpy(vin, vin17, OPEN_NAVI_VIN_SIZE);
    }
    vin[OPEN_NAVI_VIN_SIZE] = '\0';
    (void)pthread_mutex_lock(&g_pointer_lock);
    open_navi_increment_saturating(&g_pointer_state.generation);
    open_navi_increment_saturating(&g_pointer_state.vin_calls);
    g_pointer_state.vin_handle = handle;
    g_pointer_state.vin_present = (uint8_t)(vin17 != NULL);
    memcpy(g_pointer_state.vin, vin, sizeof(vin));
    (void)pthread_mutex_unlock(&g_pointer_lock);
}

void open_navi_pointer_backend_transport(uint32_t handle, uint32_t count,
    const OpenNaviTransportMeansObservedInput *items)
{
    const uint8_t *bytes = (const uint8_t *)items;
    uint8_t kind = 0u;
    uint8_t buffer_bytes[4] = {0u, 0u, 0u, 0u};
    uint8_t field_bytes[4] = {0u, 0u, 0u, 0u};
    int supported = count == 0u || count == OPEN_NAVI_TRANSPORT_OBSERVED_COUNT;
    int present = count == OPEN_NAVI_TRANSPORT_OBSERVED_COUNT && items != NULL;

    if (present) {
        /* Read only proven fields. Bytes +1..+7 are indeterminate padding and
         * are neither copied nor compared. */
        kind = bytes[0];
        memcpy(buffer_bytes, bytes + 8, sizeof(buffer_bytes));
        memcpy(field_bytes, bytes + 12, sizeof(field_bytes));
    }
    (void)pthread_mutex_lock(&g_pointer_lock);
    open_navi_increment_saturating(&g_pointer_state.generation);
    open_navi_increment_saturating(&g_pointer_state.transport_calls);
    g_pointer_state.transport_handle = handle;
    g_pointer_state.transport_count = count;
    g_pointer_state.transport_supported = (uint8_t)supported;
    g_pointer_state.transport_present = (uint8_t)present;
    g_pointer_state.transport_kind = kind;
    g_pointer_state.transport_buffer_size = read_le32(buffer_bytes);
    g_pointer_state.transport_field_0c = read_le32(field_bytes);
    (void)pthread_mutex_unlock(&g_pointer_lock);
}

void open_navi_pointer_backend_connection(
    uint32_t handle, uint32_t service,
    const OpenNaviConnectionDeviceInfoInput *info)
{
    uint32_t field_04 = 0u;
    const char *imei_source = NULL;
    const char *msisdn_source = NULL;
    const char *sim_source = NULL;
    char imei[129] = {0};
    char msisdn[129] = {0};
    char sim_iccid[129] = {0};
    uint32_t imei_length;
    uint32_t msisdn_length;
    uint32_t sim_length;

    if (info != NULL) {
        /* Copy outer fields by value before following any nested pointer. */
        memcpy(&imei_source, &info->imei, sizeof(imei_source));
        memcpy(&field_04, &info->field_04, sizeof(field_04));
        memcpy(&msisdn_source, &info->msisdn, sizeof(msisdn_source));
        memcpy(&sim_source, &info->sim_iccid, sizeof(sim_source));
    }
    imei_length = copy_bounded_string(imei, 128u, imei_source);
    msisdn_length = copy_bounded_string(msisdn, 128u, msisdn_source);
    sim_length = copy_bounded_string(sim_iccid, 128u, sim_source);

    (void)pthread_mutex_lock(&g_pointer_lock);
    open_navi_increment_saturating(&g_pointer_state.generation);
    open_navi_increment_saturating(&g_pointer_state.connection_calls);
    g_pointer_state.connection_handle = handle;
    g_pointer_state.connection_service = service;
    g_pointer_state.connection_info_present = (uint8_t)(info != NULL);
    g_pointer_state.connection_field_04 = field_04;
    g_pointer_state.imei_present = (uint8_t)(imei_source != NULL);
    g_pointer_state.msisdn_present = (uint8_t)(msisdn_source != NULL);
    g_pointer_state.sim_iccid_present = (uint8_t)(sim_source != NULL);
    g_pointer_state.imei_length = imei_length;
    g_pointer_state.msisdn_length = msisdn_length;
    g_pointer_state.sim_iccid_length = sim_length;
    memcpy(g_pointer_state.imei, imei, sizeof(imei));
    memcpy(g_pointer_state.msisdn, msisdn, sizeof(msisdn));
    memcpy(g_pointer_state.sim_iccid, sim_iccid, sizeof(sim_iccid));
    (void)pthread_mutex_unlock(&g_pointer_lock);
}

void open_navi_pointer_backend_tef(uint32_t handle, uint32_t category,
                              const char *filename)
{
    char local_filename[513] = {0};
    uint32_t length = copy_bounded_string(local_filename, 512u, filename);

    (void)pthread_mutex_lock(&g_pointer_lock);
    open_navi_increment_saturating(&g_pointer_state.generation);
    open_navi_increment_saturating(&g_pointer_state.tef_calls);
    g_pointer_state.tef_handle = handle;
    g_pointer_state.tef_category = category;
    g_pointer_state.tef_present = (uint8_t)(filename != NULL);
    g_pointer_state.tef_length = length;
    memcpy(g_pointer_state.tef_filename, local_filename,
           sizeof(local_filename));
    (void)pthread_mutex_unlock(&g_pointer_lock);
}

void open_navi_pointer_backend_reset(void)
{
    (void)pthread_mutex_lock(&g_pointer_lock);
    memset(&g_pointer_state, 0, sizeof(g_pointer_state));
    (void)pthread_mutex_unlock(&g_pointer_lock);
}

#if defined(OPEN_NAVI_TESTING)
void open_navi_pointer_backend_test_state(OpenNaviPointerBackendTestState *state_out)
{
    (void)pthread_mutex_lock(&g_pointer_lock);
    if (state_out != NULL) {
        *state_out = g_pointer_state;
    }
    (void)pthread_mutex_unlock(&g_pointer_lock);
}
#endif
