/* Handwritten typed ABI adapters, checked against the callback manifests.
 * The generated raw-capture trampolines are a separate diagnostic path; they
 * do not supply prototypes or decide which callbacks the provider emits. */
#include "host_callbacks.h"

#include <stddef.h>
#include <string.h>
#include <limits.h>

typedef uint32_t (*PasCallback0)(void *self);
typedef uint32_t (*PasCallback1U32)(void *self, uint32_t value);
typedef uint32_t (*PasCallback2U32)(void *self, uint32_t a, uint32_t b);
typedef uint32_t (*PasCallback3U32)(void *self, uint32_t a, uint32_t b,
                                   uint32_t c);
typedef uint32_t (*PasCallback4U32)(void *self, uint32_t a, uint32_t b,
                                   uint32_t c, uint32_t d);
typedef uint32_t (*PasCallback5U32)(void *self, uint32_t a, uint32_t b,
                                   uint32_t c, uint32_t d, uint32_t e);

typedef char MockPasSxmLocationSizeMustBe16[
    sizeof(MockPasSxmLocation) == 16u ? 1 : -1];
typedef char MockPasSxmMbrSizeMustBe48[
    sizeof(MockPasSxmMbr) == 48u ? 1 : -1];

static const void *const *nc_host_vtable(void *proxy)
{
    return proxy != NULL ? *(const void *const *const *)proxy : NULL;
}

static void *nc_host_callback_at(void *proxy, uint32_t slot)
{
    const void *const *vtable = nc_host_vtable(proxy);

    return vtable != NULL ? (void *)vtable[slot] : NULL;
}

static uint32_t nc_host_record_callback_status(uint32_t adapter, uint32_t slot,
                                uint32_t status, MockCallbackClass kind)
{
    mock_callback_record_status(adapter, slot, status, kind);
    return status;
}

static int nc_host_cstr8_within(const char *text, size_t maximum)
{
    size_t index;

    if (text == NULL) {
        return 0;
    }
    for (index = 0u; index <= maximum; ++index) {
        if (text[index] == '\0') {
            return 1;
        }
    }
    return 0;
}

static int nc_host_cstr16_within(const wchar_t *text, size_t maximum)
{
    size_t index;

    if (text == NULL) {
        return 0;
    }
    for (index = 0u; index <= maximum; ++index) {
        if (text[index] == L'\0') {
            return 1;
        }
    }
    return 0;
}

#define DEFINE_PAS_0(name, adapter, slot, kind)                               \
    uint32_t name(void *proxy)                                                 \
    {                                                                          \
        PasCallback0 callback = (PasCallback0)nc_host_callback_at(proxy, slot);      \
        uint32_t status = callback != NULL ? callback(proxy) : 1u;             \
        return nc_host_record_callback_status(adapter, slot, status, kind);                   \
    }

#define DEFINE_PAS_1(name, adapter, slot, kind)                               \
    uint32_t name(void *proxy, uint32_t value)                                 \
    {                                                                          \
        PasCallback1U32 callback =                                             \
            (PasCallback1U32)nc_host_callback_at(proxy, slot);                       \
        uint32_t status = callback != NULL ? callback(proxy, value) : 1u;      \
        return nc_host_record_callback_status(adapter, slot, status, kind);                   \
    }

#define DEFINE_PAS_2(name, adapter, slot, kind)                               \
    uint32_t name(void *proxy, uint32_t a, uint32_t b)                         \
    {                                                                          \
        PasCallback2U32 callback =                                             \
            (PasCallback2U32)nc_host_callback_at(proxy, slot);                       \
        uint32_t status = callback != NULL ? callback(proxy, a, b) : 1u;       \
        return nc_host_record_callback_status(adapter, slot, status, kind);                   \
    }

#define DEFINE_PAS_3(name, adapter, slot, kind)                               \
    uint32_t name(void *proxy, uint32_t a, uint32_t b, uint32_t c)             \
    {                                                                          \
        PasCallback3U32 callback =                                             \
            (PasCallback3U32)nc_host_callback_at(proxy, slot);                       \
        uint32_t status = callback != NULL ? callback(proxy, a, b, c) : 1u;    \
        return nc_host_record_callback_status(adapter, slot, status, kind);                   \
    }

#define DEFINE_PAS_4(name, adapter, slot, kind)                               \
    uint32_t name(void *proxy, uint32_t a, uint32_t b, uint32_t c,             \
                  uint32_t d)                                                  \
    {                                                                          \
        PasCallback4U32 callback =                                             \
            (PasCallback4U32)nc_host_callback_at(proxy, slot);                       \
        uint32_t status = callback != NULL                                     \
                              ? callback(proxy, a, b, c, d)                    \
                              : 1u;                                            \
        return nc_host_record_callback_status(adapter, slot, status, kind);                   \
    }

#define DEFINE_PAS_5(name, adapter, slot, kind)                               \
    uint32_t name(void *proxy, uint32_t a, uint32_t b, uint32_t c,             \
                  uint32_t d, uint32_t e)                                      \
    {                                                                          \
        PasCallback5U32 callback =                                             \
            (PasCallback5U32)nc_host_callback_at(proxy, slot);                       \
        uint32_t status = callback != NULL                                     \
                              ? callback(proxy, a, b, c, d, e)                 \
                              : 1u;                                            \
        return nc_host_record_callback_status(adapter, slot, status, kind);                   \
    }

DEFINE_PAS_0(nc_host_root_adapter_ready, UINT32_MAX, 12u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_0(nc_host_root_no_arg_internal_state_cleanup, UINT32_MAX, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_root_signal_initiate_restart_once, UINT32_MAX, 5u,
             MOCK_CALLBACK_OPTIONAL)

uint32_t nc_host_adapter_ready(void *proxy, uint32_t adapter)
{
    PasCallback0 callback = (PasCallback0)nc_host_callback_at(proxy, 2u);
    uint32_t status = callback != NULL ? callback(proxy) : 1u;

    return nc_host_record_callback_status(adapter, 2u, status, MOCK_CALLBACK_CRITICAL);
}

DEFINE_PAS_0(nc_host_hmi_adapter_ready, 1u, 2u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(nc_host_hmi_subscribe, 1u, 3u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(nc_host_hmi_request_create_request_code, 1u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_hmi_request_create_finger_print, 1u, 5u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_hmi_request_screen_window_identifier, 1u, 6u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_3(nc_host_hmi_request_hmi_state, 1u, 7u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(nc_host_hmi_reply_hmi_state_transition, 1u, 9u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_3(nc_host_hmi_reply_screen_change, 1u, 10u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(nc_host_hmi_reply_screen_theme_change, 1u, 12u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_hmi_signal_navigation_hmi_ready, 1u, 14u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_2(nc_host_hmi_signal_hmi_state_change, 1u, 8u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_4(nc_host_hmi_signal_screen_change, 1u, 11u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_hardkey_subscribe, 4u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_logging_request_log_buffer_sizes, 6u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_i18n_subscribe, 7u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_i18n_request_internationalization_settings, 7u, 4u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_vehicle_subscribe, 8u, 3u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(nc_host_vehicle_request_day_night_status, 8u, 5u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(nc_host_vehicle_request_day_night_setting, 8u, 6u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(nc_host_vehicle_request_vehicle_identification_number, 8u, 11u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_gps_subscribe, 9u, 3u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_2(nc_host_audio_subscribe_audio_state, 10u, 3u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_2(nc_host_audio_request_audio_out_source_descriptor, 10u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_audio_request_audio_in_source_descriptor, 10u, 5u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_audio_request_audio_source_allocation, 10u, 6u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_audio_request_audio_source_preparation, 10u, 7u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_audio_signal_deallocate_audio_source, 10u, 8u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(nc_host_audio_reply_audio_source_deactivation, 10u, 9u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_audio_signal_audio_source_availability, 10u, 10u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_audio_request_audio_source_status, 10u, 11u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_audio_signal_enable_server_audio_source, 10u, 12u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_audio_signal_disable_server_audio_source, 10u, 13u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_audio_request_audio_source_enable_status, 10u, 14u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_audio_signal_audio_source_comfort_volume, 10u, 15u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_audio_signal_enable_audio_source_volume_separation, 10u, 16u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_audio_signal_disable_audio_source_volume_separation, 10u, 17u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_audio_request_audio_source_volume_separation_status, 10u, 18u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_audio_request_audio_source_volume, 10u, 19u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_3(nc_host_audio_request_audio_source_volume_change, 10u, 20u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_diagnosis_request_vehicle_brand, 12u, 8u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_nav_diagnosis_subscribe_driver_distraction_status, 13u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_nav_diagnosis_request_driver_distraction_status, 13u, 4u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_nav_diagnosis_request_static_setting_11fc, 13u, 5u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_nav_diagnosis_request_static_setting_11fb, 13u, 6u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_nav_diagnosis_request_static_setting_11fa, 13u, 7u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_nav_diagnosis_request_static_setting_11fe, 13u, 8u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_nav_diagnosis_request_static_setting_11fd, 13u, 9u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_cluster_subscribe, 17u, 5u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(mock_pas_17_4_raw, 17u, 4u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(nc_host_cluster_request_or_signal_no_args, 17u, 6u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(nc_host_cluster_request_tbt_settings_enable_status, 17u, 7u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(nc_host_data_exchange_request_supported_transport_means_and_buffer_sizes, 19u, 5u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(mock_pas_19_6_raw, 19u, 6u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(nc_host_data_exchange_request_send_data_exchange_object_produced_by_server,
             19u, 7u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_connectivity_subscribe, 20u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_connectivity_request_connectivity_status, 20u, 4u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_connectivity_request_connection_device_info, 20u, 8u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_connectivity_request_signal_strength, 20u, 9u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_connectivity_release_connection_for_service, 20u, 6u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(nc_host_voice_initialization_no_op, 25u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(nc_host_voice_signal_or_request_route_guidance_state, 25u, 4u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(nc_host_voice_reply_alphabet_get, 25u, 7u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_voice_reply_destination_adb_entry_set, 25u, 8u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_voice_reply_destination_home_set, 25u, 9u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_voice_reply_destination_work_set, 25u, 10u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(nc_host_voice_reply_destination_status, 25u, 11u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(nc_host_voice_reply_file_from_dbid_get, 25u, 12u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_voice_reply_guidance_instructions_repeat, 25u, 15u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_voice_reply_guidance_stop, 25u, 16u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_voice_reply_guidance_start, 25u, 17u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(nc_host_voice_reply_location_check, 25u, 18u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(nc_host_voice_reply_route_guidance_status, 25u, 23u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(nc_host_voice_reply_states_settings, 25u, 24u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_voice_reply_tef_file, 25u, 25u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_4(nc_host_voice_reply_poi_set, 25u, 22u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_voice_reply_change_route_guidance, 25u, 26u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_voice_reply_change_map_direction, 25u, 27u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_voice_reply_change_map_dimensional_view, 25u, 28u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_voice_reply_zoom_in, 25u, 29u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_voice_reply_zoom_out, 25u, 30u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(nc_host_voice_reply_set_destination, 25u, 34u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_phone_subscribe, 26u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_phone_request_phone_call_availability, 26u, 5u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_tts_internal_deinitialize, 24u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_parking_subscribe, 29u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_apogee_subscribe, 30u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_agw_subscribe, 31u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_31_6_raw, 31u, 6u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_5(mock_pas_31_10_raw, 31u, 10u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(mock_pas_31_13_raw, 31u, 13u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_agw_reply_graphical_weather_map_mode, 31u, 8u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_root_request_user_storage, UINT32_MAX, 9u,
             MOCK_CALLBACK_OPTIONAL)

/* Concrete QR39.6/QR40 fixed-word dormant seams. */
DEFINE_PAS_1(nc_host_connectivity_signal_connection_needed, 20u, 5u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(nc_host_voice_signal_my_dest_deleted_event, 25u, 5u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_voice_signal_gui_selection, 25u, 6u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_voice_reply_route_guidance_status_completion, 25u, 31u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(nc_host_voice_signal_terminate_vr, 25u, 32u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(nc_host_voice_signal_initiate_vr, 25u, 33u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_agw_reply_enter_graphical_weather_map, 31u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_agw_reply_exit_graphical_weather_map, 31u, 5u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_agw_request_agw_panning_mode, 31u, 7u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_agw_request_agw_set_filter, 31u, 9u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(nc_host_agw_request_agw_set_radius_of_interest, 31u, 11u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(nc_host_agw_signal_exit_graphical_weather_map, 31u, 14u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_rds_subscribe, 14u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_rds_request_status, 14u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_rds_request_selection_filter_capabilities, 14u, 5u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_rds_request_selectable_filter_list, 14u, 6u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(nc_host_tpeg_adapter_ready, 27u, 2u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_tpeg_subscribe, 27u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_tpeg_request_station_list, 27u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_gps_request_gps_position, 9u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_root_subscribe, UINT32_MAX, 2u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(nc_host_root_signal_disable_watchdog, UINT32_MAX, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_root_reply_shutdown, UINT32_MAX, 7u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_root_request_power_status, UINT32_MAX, 8u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_diagnosis_subscribe_diagnosis_driver_distraction_status, 12u, 3u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_diagnosis_request_diagnosis_driver_distraction_status, 12u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_diagnosis_signal_error_code, 12u, 7u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_rotary_subscribe, 23u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(nc_host_live_traffic_adapter_ready, 21u, 2u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_live_traffic_subscribe, 21u, 3u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(nc_host_live_traffic_signal_start_authentication, 21u, 5u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(nc_host_vics_adapter_ready, 28u, 2u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(nc_host_vics_subscribe, 28u, 3u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_vics_request_tune_to_frequency, 28u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_4(nc_host_data_exchange_request_offer_data_exchange_object_produced_by_client,
             19u, 10u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_vehicle_subscribe_can_message, 8u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_vehicle_request_odometer, 8u, 7u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_vehicle_request_vehicle_speed_displayed_on_icd, 8u, 8u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_vehicle_request_can_message_synchronization, 8u, 9u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(nc_host_vehicle_request_ignition_status, 8u, 12u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_apogee_request_apogee_remove_collection_mbr, 30u, 6u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(nc_host_nav_diagnosis_reply_status_navigation_database, 13u, 12u,
             MOCK_CALLBACK_OPTIONAL)

uint32_t nc_host_agw_request_wsa_set_alert_location(
    void *proxy, uint32_t handle, MockPasSxmLocation location,
    uint32_t radius, uint32_t radius_unit)
{
    typedef uint32_t (*Callback)(void *, uint32_t, MockPasSxmLocation,
                                 uint32_t, uint32_t);
    Callback callback = (Callback)nc_host_callback_at(proxy, 12u);
    uint32_t status = callback != NULL
                          ? callback(proxy, handle, location, radius,
                                     radius_unit)
                          : 1u;
    return nc_host_record_callback_status(31u, 12u, status, MOCK_CALLBACK_OPTIONAL);
}

static uint32_t nc_host_apogee_set_mbr(
    void *proxy, uint32_t slot, uint32_t handle, uint16_t type,
    MockPasSxmMbr mbr, uint32_t collection_index)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint16_t,
                                 MockPasSxmMbr, uint32_t);
    Callback callback = (Callback)nc_host_callback_at(proxy, slot);
    uint32_t status = callback != NULL
                          ? callback(proxy, handle, type, mbr,
                                     collection_index)
                          : 1u;
    return nc_host_record_callback_status(30u, slot, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_apogee_request_apogee_set_type_collection_mbr(
    void *proxy, uint32_t handle, uint16_t type, MockPasSxmMbr mbr,
    uint32_t collection_index)
{
    return nc_host_apogee_set_mbr(proxy, 4u, handle, type, mbr,
                                   collection_index);
}

uint32_t nc_host_apogee_request_apogee_set_type_extraction_mbr(
    void *proxy, uint32_t handle, uint16_t type, MockPasSxmMbr mbr,
    uint32_t collection_index)
{
    return nc_host_apogee_set_mbr(proxy, 5u, handle, type, mbr,
                                   collection_index);
}

uint32_t nc_host_parking_request_parking_set_collection_mbr(
    void *proxy, uint32_t handle, MockPasSxmMbr mbr,
    uint16_t collection_index)
{
    typedef uint32_t (*Callback)(void *, uint32_t, MockPasSxmMbr, uint16_t);
    Callback callback = (Callback)nc_host_callback_at(proxy, 4u);
    uint32_t status = callback != NULL
                          ? callback(proxy, handle, mbr, collection_index)
                          : 1u;
    return nc_host_record_callback_status(29u, 4u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_root_supplier_specific_server_function(
    void *proxy, uint32_t word0, uint32_t word1, const void *opaque)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, const void *);
    Callback callback = (Callback)nc_host_callback_at(proxy, 11u);
    uint32_t status = callback != NULL
                          ? callback(proxy, word0, word1, opaque)
                          : 1u;

    /* The concrete consumer only logs the pointer during this call. */
    return nc_host_record_callback_status(UINT32_MAX, 11u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_ews_reply_ews_area(void *proxy, uint32_t handle,
                                    const void *area_10)
{
    typedef uint32_t (*Callback)(void *, uint32_t, const void *);
    uint8_t owned[MOCK_PAS_EWS_AREA_SIZE];
    Callback callback = (Callback)nc_host_callback_at(proxy, 3u);
    uint32_t status = 1u;

    if (callback != NULL && area_10 != NULL) {
        memcpy(owned, area_10, sizeof(owned));
        status = callback(proxy, handle, owned);
    }
    return nc_host_record_callback_status(32u, 3u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_rds_signal_selection_filter(void *proxy, uint32_t count,
                                             const void *filters_6x20)
{
    typedef uint32_t (*Callback)(void *, uint32_t, const void *);
    uint8_t owned[MOCK_PAS_RDS_FILTER_RECORD_SIZE *
                  MOCK_PAS_RDS_FILTER_MAX_RECORDS];
    uint32_t copied_count = count < MOCK_PAS_RDS_FILTER_MAX_RECORDS
                                ? count
                                : MOCK_PAS_RDS_FILTER_MAX_RECORDS;
    const void *argument = NULL;
    Callback callback = (Callback)nc_host_callback_at(proxy, 7u);
    uint32_t status = 1u;

    if (callback != NULL && (copied_count == 0u || filters_6x20 != NULL)) {
        if (copied_count != 0u) {
            memcpy(owned, filters_6x20,
                   (size_t)copied_count * MOCK_PAS_RDS_FILTER_RECORD_SIZE);
            argument = owned;
        }
        status = callback(proxy, count, argument);
    }
    return nc_host_record_callback_status(14u, 7u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_tpeg_signal_select_station(void *proxy,
                                           const void *station_0x30)
{
    typedef uint32_t (*Callback)(void *, const void *);
    uint8_t owned[MOCK_PAS_TPEG_STATION_SIZE];
    Callback callback = (Callback)nc_host_callback_at(proxy, 5u);
    uint32_t status = 1u;

    if (callback != NULL && station_0x30 != NULL) {
        memcpy(owned, station_0x30, sizeof(owned));
        status = callback(proxy, owned);
    }
    return nc_host_record_callback_status(27u, 5u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_tpeg_signal_select_station_list(
    void *proxy, uint32_t count, const void *stations_0x30x4)
{
    typedef uint32_t (*Callback)(void *, uint32_t, const void *);
    uint8_t owned[MOCK_PAS_TPEG_STATION_SIZE *
                  MOCK_PAS_TPEG_STATION_MAX_RECORDS];
    const void *argument = NULL;
    Callback callback = (Callback)nc_host_callback_at(proxy, 6u);
    uint32_t status = 1u;

    if (callback != NULL && count <= MOCK_PAS_TPEG_STATION_MAX_RECORDS &&
        (count == 0u || stations_0x30x4 != NULL)) {
        if (count != 0u) {
            memcpy(owned, stations_0x30x4,
                   (size_t)count * MOCK_PAS_TPEG_STATION_SIZE);
            argument = owned;
        }
        status = callback(proxy, count, argument);
    }
    return nc_host_record_callback_status(27u, 6u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_gps_signal_map_matched_position(void *proxy,
                                               const void *position_0x38)
{
    typedef uint32_t (*Callback)(void *, const void *);
    uint8_t owned[MOCK_PAS_MAP_MATCHED_POSITION_SIZE];
    Callback callback = (Callback)nc_host_callback_at(proxy, 5u);
    uint32_t status = 1u;

    if (callback != NULL && position_0x38 != NULL) {
        memcpy(owned, position_0x38, sizeof(owned));
        status = callback(proxy, owned);
    }
    return nc_host_record_callback_status(9u, 5u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_vics_signal_vics_wide_emergency_warning(
    void *proxy, uint32_t word0, uint32_t word1, uint32_t word2,
    uint32_t minute, const void *warning_0x180)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, uint32_t,
                                 uint32_t, const void *);
    uint8_t owned[MOCK_PAS_VICS_WARNING_SIZE];
    Callback callback = (Callback)nc_host_callback_at(proxy, 5u);
    uint32_t status = 1u;

    if (callback != NULL && warning_0x180 != NULL) {
        memcpy(owned, warning_0x180, sizeof(owned));
        status = callback(proxy, word0, word1, word2, minute, owned);
    }
    return nc_host_record_callback_status(28u, 5u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_vehicle_signal_can_message(void *proxy, uint32_t word0,
                                        uint32_t word1,
                                        const void *payload_8)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, const void *);
    uint8_t owned[MOCK_PAS_CAN_MESSAGE_SIZE];
    Callback callback = (Callback)nc_host_callback_at(proxy, 10u);
    uint32_t status = 1u;

    if (callback != NULL && payload_8 != NULL) {
        memcpy(owned, payload_8, sizeof(owned));
        status = callback(proxy, word0, word1, owned);
    }
    return nc_host_record_callback_status(8u, 10u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_i18n_reply_time_zone_information(
    void *proxy, uint32_t handle, const void *timezone_0x22c)
{
    typedef uint32_t (*Callback)(void *, uint32_t, const void *);
    uint8_t owned[MOCK_PAS_TIMEZONE_SIZE];
    Callback callback = (Callback)nc_host_callback_at(proxy, 5u);
    uint32_t status = 1u;

    if (callback != NULL && timezone_0x22c != NULL) {
        memcpy(owned, timezone_0x22c, sizeof(owned));
        status = callback(proxy, handle, owned);
    }
    return nc_host_record_callback_status(7u, 5u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_logging_signal_write_log_data(void *proxy,
                                        uint32_t methods,
                                        uint32_t reserved_ignored,
                                        uint32_t encoding,
                                        uint8_t severity,
                                        uint32_t byte_count,
                                        const void *text)
{
    typedef uint32_t (*SignalWriteLogData)(void *, uint32_t, uint32_t,
                                           uint32_t, uint8_t, uint32_t,
                                           const void *);
    SignalWriteLogData callback =
        (SignalWriteLogData)nc_host_callback_at(proxy, 4u);
    uint32_t status = callback != NULL
                          ? callback(proxy, methods, reserved_ignored,
                                     encoding, severity, byte_count, text)
                          : 1u;

    return nc_host_record_callback_status(6u, 4u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_voice_reply_location_get(
    void *proxy, uint32_t handle_ignored, uint32_t result,
    const uint32_t location_types[7], const char *location_ids,
    const char *location_values, const char *orthographic,
    const char *phonetic)
{
    typedef uint32_t (*ReplyLocationGet)(
        void *, uint32_t, uint32_t, const uint32_t *, const char *,
        const char *, const char *, const char *);
    ReplyLocationGet callback =
        (ReplyLocationGet)nc_host_callback_at(proxy, 19u);
    uint32_t status = callback != NULL
                          ? callback(proxy, handle_ignored, result,
                                     location_types, location_ids,
                                     location_values, orthographic, phonetic)
                          : 1u;

    return nc_host_record_callback_status(25u, 19u, status, MOCK_CALLBACK_OPTIONAL);
}

static void nc_host_copy_voice_string(char owned[MOCK_PAS_VR_STRING_SIZE],
                                    const char *source)
{
    size_t length = 0u;

    while (length + 1u < MOCK_PAS_VR_STRING_SIZE && source[length] != '\0') {
        owned[length] = source[length];
        ++length;
    }
    owned[length] = '\0';
}

uint32_t nc_host_voice_reply_file_get(void *proxy, uint32_t handle,
                                    uint32_t result, const char *filename)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, const char *);
    char owned[MOCK_PAS_VR_STRING_SIZE];
    Callback callback = (Callback)nc_host_callback_at(proxy, 13u);
    uint32_t status = 1u;

    if (callback != NULL && filename != NULL) {
        nc_host_copy_voice_string(owned, filename);
        status = callback(proxy, handle, result, owned);
    }
    return nc_host_record_callback_status(25u, 13u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_voice_reply_location_set(
    void *proxy, uint32_t handle, uint32_t result, uint32_t location_status,
    const char *location, uint32_t location_type, uint32_t entries)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, uint32_t,
                                 const char *, uint32_t, uint32_t);
    char owned[MOCK_PAS_VR_STRING_SIZE];
    Callback callback = (Callback)nc_host_callback_at(proxy, 20u);
    uint32_t status = 1u;

    if (callback != NULL && location != NULL) {
        nc_host_copy_voice_string(owned, location);
        status = callback(proxy, handle, result, location_status, owned,
                          location_type, entries);
    }
    return nc_host_record_callback_status(25u, 20u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_voice_reply_next_available_location_get(
    void *proxy, uint32_t handle, uint32_t result,
    const uint32_t location_types[MOCK_PAS_VR_LOCATION_TYPE_COUNT])
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t,
                                 const uint32_t *);
    uint32_t owned[MOCK_PAS_VR_LOCATION_TYPE_COUNT];
    Callback callback = (Callback)nc_host_callback_at(proxy, 21u);
    uint32_t status = 1u;

    if (callback != NULL && location_types != NULL) {
        memcpy(owned, location_types, sizeof(owned));
        status = callback(proxy, handle, result, owned);
    }
    return nc_host_record_callback_status(25u, 21u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_agw_signal_wsa_warning_popup(
    void *proxy, uint32_t alert_id, uint32_t is_manual,
    const void *alert_info_0x2d0)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, const void *);
    uint8_t owned[MOCK_PAS_WSA_ALERT_INFO_SIZE];
    Callback callback = (Callback)nc_host_callback_at(proxy, 15u);
    uint32_t status = 1u;

    if (callback != NULL && alert_info_0x2d0 != NULL) {
        memcpy(owned, alert_info_0x2d0, sizeof(owned));
        status = callback(proxy, alert_id, is_manual, owned);
    }
    return nc_host_record_callback_status(31u, 15u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_hmi_signal_press_state_animation(void *proxy,
                                                 uint32_t press_state,
                                                 uint32_t unused_r2,
                                                 uint32_t x,
                                                 uint32_t y)
{
    typedef uint32_t (*SignalPressStateAnimation)(void *, uint32_t, uint32_t,
                                                   uint32_t, uint32_t);
    SignalPressStateAnimation callback =
        (SignalPressStateAnimation)nc_host_callback_at(proxy, 13u);
    uint32_t status = callback != NULL
                          ? callback(proxy, press_state, unused_r2, x, y)
                          : 1u;

    return nc_host_record_callback_status(1u, 13u, status, MOCK_CALLBACK_CRITICAL);
}

uint32_t nc_host_i18n_signal_time_zone(void *proxy, const void *timezone_0x22c)
{
    typedef uint32_t (*Callback)(void *, const void *);
    uint8_t owned[MOCK_PAS_TIMEZONE_SIZE];
    Callback callback = (Callback)nc_host_callback_at(proxy, 6u);
    uint32_t status = 1u;

    if (callback != NULL && timezone_0x22c != NULL) {
        memcpy(owned, timezone_0x22c, sizeof(owned));
        status = callback(proxy, owned);
    }
    return nc_host_record_callback_status(7u, 6u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_diagnosis_reply_software_component_version(void *proxy, int32_t handle, const void *version4)
{
    typedef uint32_t (*Callback)(void *, int32_t, const void *);
    uint8_t owned[MOCK_PAS_SOFTWARE_VERSION_SIZE];
    Callback callback = (Callback)nc_host_callback_at(proxy, 5u);
    uint32_t status = 1u;

    if (callback != NULL && version4 != NULL) {
        memcpy(owned, version4, sizeof(owned));
        status = callback(proxy, handle, owned);
    }
    return nc_host_record_callback_status(12u, 5u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_diagnosis_reply_software_component_supplier_identification(void *proxy, int32_t handle, uint16_t supplier)
{
    typedef uint32_t (*Callback)(void *, int32_t, uint16_t);
    Callback callback = (Callback)nc_host_callback_at(proxy, 6u);
    uint32_t status = callback != NULL ? callback(proxy, handle, supplier) : 1u;

    return nc_host_record_callback_status(12u, 6u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_nav_diagnosis_reply_navigation_database_status(void *proxy, int32_t handle, uint32_t packed_status)
{
    typedef uint32_t (*Callback)(void *, int32_t, uint32_t);
    Callback callback = (Callback)nc_host_callback_at(proxy, 13u);
    uint32_t status = callback != NULL
                          ? callback(proxy, handle, packed_status)
                          : 1u;

    return nc_host_record_callback_status(13u, 13u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_nav_diagnosis_reply_navigation_database_version_string(void *proxy, int32_t handle, const wchar_t *text)
{
    typedef uint32_t (*Callback)(void *, int32_t, const wchar_t *);
    wchar_t owned[MOCK_PAS_DIAG_WIDE_TEXT_MAX + 1u];
    Callback callback = (Callback)nc_host_callback_at(proxy, 11u);
    size_t length = 0u;
    uint32_t status = 1u;

    if (callback != NULL && text != NULL) {
        while (length < MOCK_PAS_DIAG_WIDE_TEXT_MAX && text[length] != L'\0') {
            owned[length] = text[length];
            ++length;
        }
        owned[length] = L'\0';
        status = callback(proxy, handle, owned);
    }
    return nc_host_record_callback_status(13u, 11u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_cluster_signal_instrument_cluster_display_data(void *proxy, const void *display_data_0xa5c)
{
    typedef uint32_t (*Callback)(void *, const void *);
    uint8_t owned[MOCK_CLUSTER_RECORD_SIZE];
    Callback callback = (Callback)nc_host_callback_at(proxy, 3u);
    uint32_t status = 1u;

    if (callback != NULL && display_data_0xa5c != NULL) {
        memcpy(owned, display_data_0xa5c, sizeof(owned));
        status = callback(proxy, owned);
    }
    return nc_host_record_callback_status(17u, 3u, status, MOCK_CALLBACK_CRITICAL);
}

static uint32_t nc_host_data_exchange_at(void *proxy, uint32_t slot,
                                       uint16_t count, const uint32_t *types)
{
    typedef uint32_t (*Callback)(void *, uint16_t, const uint32_t *);
    uint32_t owned[MOCK_PAS_DATA_EXCHANGE_MAX_TYPES];
    const uint32_t *argument = NULL;
    Callback callback = (Callback)nc_host_callback_at(proxy, slot);
    uint32_t status = 1u;

    if (callback != NULL && count <= MOCK_PAS_DATA_EXCHANGE_MAX_TYPES &&
        (count == 0u || types != NULL)) {
        if (count != 0u) {
            memcpy(owned, types, (size_t)count * sizeof(owned[0]));
            argument = owned;
        }
        status = callback(proxy, count, argument);
    }
    return nc_host_record_callback_status(19u, slot, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_data_exchange_produce_data_exchange_object_types(void *proxy, uint16_t count, const uint32_t *types)
{
    return nc_host_data_exchange_at(proxy, 3u, count, types);
}

uint32_t nc_host_data_exchange_consume_data_exchange_object_types(void *proxy, uint16_t count, const uint32_t *types)
{
    return nc_host_data_exchange_at(proxy, 4u, count, types);
}

uint32_t nc_host_root_signal_thread_status(void *proxy, uint32_t thread_id,
                         const wchar_t *thread_name, uint32_t thread_status)
{
    typedef uint32_t (*Callback)(void *, uint32_t, const wchar_t *, uint32_t);
    wchar_t owned[MOCK_THREAD_NAME_LIMIT + 1u];
    Callback callback = (Callback)nc_host_callback_at(proxy, 10u);
    size_t length = 0u;
    uint32_t status = 1u;

    if (callback != NULL && thread_name != NULL) {
        while (length < MOCK_THREAD_NAME_LIMIT &&
               thread_name[length] != L'\0' &&
               (uint32_t)thread_name[length] < 0x80u) {
            owned[length] = thread_name[length];
            ++length;
        }
        owned[length] = L'\0';
        status = callback(proxy, thread_id, owned, thread_status);
    }
    return nc_host_record_callback_status(UINT32_MAX, 10u, status, MOCK_CALLBACK_CRITICAL);
}

uint32_t nc_host_tts_request_play_tts(void *proxy, uint32_t handle,
                                      uint32_t source, uint32_t text_type,
                                      const char *tts_text)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, uint32_t,
                                 const char *);
    char owned[0x404];
    Callback callback = (Callback)nc_host_callback_at(proxy, 3u);
    size_t length = 0u;
    uint32_t status = 1u;

    if (callback != NULL && nc_host_cstr8_within(tts_text, 0x403u)) {
        while (tts_text[length] != '\0') {
            ++length;
        }
        if (length != 0u) {
            memcpy(owned, tts_text, length + 1u);
            status = callback(proxy, handle, source, text_type, owned);
        }
    }
    return nc_host_record_callback_status(24u, 3u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_ews_signal_ews_area_change(void *proxy,
                                           const void *old_area_10,
                                           const void *new_area_10)
{
    typedef uint32_t (*Callback)(void *, const void *, const void *);
    uint8_t old_owned[MOCK_PAS_EWS_AREA_SIZE];
    uint8_t new_owned[MOCK_PAS_EWS_AREA_SIZE];
    Callback callback = (Callback)nc_host_callback_at(proxy, 4u);
    uint32_t status = 1u;

    if (callback != NULL && old_area_10 != NULL && new_area_10 != NULL) {
        memcpy(old_owned, old_area_10, sizeof(old_owned));
        memcpy(new_owned, new_area_10, sizeof(new_owned));
        status = callback(proxy, old_owned, new_owned);
    }
    return nc_host_record_callback_status(32u, 4u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_connectivity_request_http_message(
    void *proxy, uint32_t handle, uint32_t service, const wchar_t *url,
    uint32_t method, uint32_t request_size, const void *request_data,
    uint32_t response_size, const void *response_data)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t,
                                 const wchar_t *, uint32_t, uint32_t,
                                 const void *, uint32_t, const void *);
    Callback callback = (Callback)nc_host_callback_at(proxy, 7u);
    uint32_t status = 1u;

    if (callback != NULL && url != NULL &&
        (request_size == 0u || request_data != NULL) &&
        (response_size == 0u || response_data != NULL)) {
        status = callback(proxy, handle, service, url, method, request_size,
                          request_data, response_size, response_data);
    }
    return nc_host_record_callback_status(20u, 7u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_root_signal_persist(void *proxy, const wchar_t *path)
{
    typedef uint32_t (*Callback)(void *, const wchar_t *);
    Callback callback = (Callback)nc_host_callback_at(proxy, 6u);
    uint32_t status = callback != NULL && path != NULL
                          ? callback(proxy, path) : 1u;
    return nc_host_record_callback_status(UINT32_MAX, 6u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_live_traffic_request_http_message(
    void *proxy, uint32_t handle, const wchar_t *url, uint32_t method,
    uint32_t request_size, const void *request_data,
    uint32_t response_size, const void *response_data)
{
    typedef uint32_t (*Callback)(void *, uint32_t, const wchar_t *, uint32_t,
                                 uint32_t, const void *, uint32_t,
                                 const void *);
    Callback callback = (Callback)nc_host_callback_at(proxy, 4u);
    uint32_t status = 1u;

    if (callback != NULL && url != NULL &&
        (request_size == 0u || request_data != NULL) &&
        (response_size == 0u || response_data != NULL)) {
        status = callback(proxy, handle, url, method, request_size,
                          request_data, response_size, response_data);
    }
    return nc_host_record_callback_status(21u, 4u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_data_exchange_reply_data_exchange_object_required_by_server(
    void *proxy, uint32_t handle, uint32_t object_id, uint32_t object_type,
    const wchar_t *description, uint64_t size, uint32_t reply_type)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, uint32_t,
                                 const wchar_t *, uint64_t, uint32_t);
    Callback callback = (Callback)nc_host_callback_at(proxy, 8u);
    uint32_t status = callback != NULL
                          ? callback(proxy, handle, object_id, object_type,
                                     description, size, reply_type)
                          : 1u;
    return nc_host_record_callback_status(19u, 8u, status, MOCK_CALLBACK_OPTIONAL);
}

static uint32_t nc_host_data_exchange_transfer_at(
    void *proxy, uint32_t slot, uint32_t handle, uint32_t object_id,
    uint64_t size, const void *data, uint64_t total_size, uint32_t abort,
    uint64_t maximum_size)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, uint64_t,
                                 const void *, uint64_t, uint32_t);
    Callback callback = (Callback)nc_host_callback_at(proxy, slot);
    uint32_t status = 1u;

    if (callback != NULL && size <= maximum_size &&
        total_size <= UINT32_MAX && (size == 0u || data != NULL)) {
        status = callback(proxy, handle, object_id, size, data, total_size,
                          abort);
    }
    return nc_host_record_callback_status(19u, slot, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_data_exchange_request_send_data_exchange_object_required_by_server(
    void *proxy, uint32_t handle, uint32_t object_id, uint64_t size,
    const void *data, uint64_t total_size, uint32_t abort)
{
    return nc_host_data_exchange_transfer_at(proxy, 9u, handle, object_id,
                                           size, data, total_size, abort,
                                           0xfc0u);
}

uint32_t nc_host_data_exchange_data_exchange_transfer(
    void *proxy, uint32_t handle, uint32_t object_id, uint64_t size,
    const void *data, uint64_t total_size, uint32_t abort)
{
    return nc_host_data_exchange_transfer_at(proxy, 11u, handle, object_id,
                                           size, data, total_size, abort,
                                           UINT32_MAX);
}

uint32_t nc_host_nav_diagnosis_reply_approved_map_versions(
    void *proxy, int32_t handle, const wchar_t *versions)
{
    typedef uint32_t (*Callback)(void *, int32_t, const wchar_t *);
    Callback callback = (Callback)nc_host_callback_at(proxy, 10u);
    uint32_t status = callback != NULL
                          ? callback(proxy, handle, versions) : 1u;
    return nc_host_record_callback_status(13u, 10u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_gps_reply_map_matched_address(
    void *proxy, uint32_t handle, const MockPasMapMatchedAddress *address)
{
    typedef uint32_t (*Callback)(void *, uint32_t,
                                 const MockPasMapMatchedAddress *);
    Callback callback = (Callback)nc_host_callback_at(proxy, 6u);
    uint32_t status = callback != NULL && address != NULL
                          ? callback(proxy, handle, address) : 1u;
    return nc_host_record_callback_status(9u, 6u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_phone_make_phone_call(void *proxy, uint32_t handle,
                                     const wchar_t *phone_number,
                                     const wchar_t *display_text)
{
    typedef uint32_t (*Callback)(void *, uint32_t, const wchar_t *,
                                 const wchar_t *);
    Callback callback = (Callback)nc_host_callback_at(proxy, 4u);
    uint32_t status = 1u;

    if (callback != NULL && nc_host_cstr16_within(phone_number, 0x1ffu) &&
        nc_host_cstr16_within(display_text, 0x13u) &&
        phone_number[0] != L'\0' && display_text[0] != L'\0') {
        status = callback(proxy, handle, phone_number, display_text);
    }
    return nc_host_record_callback_status(26u, 4u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_voice_reply_file_info(
    void *proxy, uint32_t handle, uint32_t result,
    const char *language_list, uint32_t file_type, uint32_t entries)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, const char *,
                                 uint32_t, uint32_t);
    Callback callback = (Callback)nc_host_callback_at(proxy, 14u);
    uint32_t status = callback != NULL && language_list != NULL
                          ? callback(proxy, handle, result, language_list,
                                     file_type, entries)
                          : 1u;
    return nc_host_record_callback_status(25u, 14u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t nc_host_voice_reply_convert_ortho_to_phonetic(
    void *proxy, uint32_t handle, uint32_t result, uint32_t count,
    const uint32_t *location_types, const char *const *strings)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, uint32_t,
                                 const uint32_t *, const char *const *);
    Callback callback = (Callback)nc_host_callback_at(proxy, 35u);
    uint32_t index;
    uint32_t status = 1u;

    if (callback == NULL || count == 0u ||
        count > MOCK_PAS_VR_LOCATION_TYPE_COUNT || location_types == NULL ||
        strings == NULL) {
        return nc_host_record_callback_status(25u, 35u, status, MOCK_CALLBACK_OPTIONAL);
    }
    for (index = 0u; index < count; ++index) {
        if (strings[index] == NULL) {
            return nc_host_record_callback_status(25u, 35u, status,
                                   MOCK_CALLBACK_OPTIONAL);
        }
    }
    status = callback(proxy, handle, result, count, location_types, strings);
    return nc_host_record_callback_status(25u, 35u, status, MOCK_CALLBACK_OPTIONAL);
}
