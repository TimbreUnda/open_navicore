#ifndef OPEN_NAVICORE_HOST_CALLBACKS_H
#define OPEN_NAVICORE_HOST_CALLBACKS_H

#include <stdint.h>
#include <wchar.h>

#include "open_navi_callback_backend.h"

enum {
    OPEN_NAVI_PAS_TIMEZONE_SIZE = 0x22c,
    OPEN_NAVI_PAS_SOFTWARE_VERSION_SIZE = 4,
    OPEN_NAVI_PAS_VR_STRING_SIZE = 0x200,
    OPEN_NAVI_PAS_VR_LOCATION_TYPE_COUNT = 7,
    OPEN_NAVI_PAS_WSA_ALERT_INFO_SIZE = 0x2d0,
    OPEN_NAVI_PAS_DATA_EXCHANGE_MAX_TYPES = 64,
    OPEN_NAVI_PAS_DIAG_WIDE_TEXT_MAX = 127,
    OPEN_NAVI_PAS_EWS_AREA_SIZE = 10,
    OPEN_NAVI_PAS_RDS_FILTER_RECORD_SIZE = 6,
    OPEN_NAVI_PAS_RDS_FILTER_MAX_RECORDS = 20,
    OPEN_NAVI_PAS_TPEG_STATION_SIZE = 0x30,
    OPEN_NAVI_PAS_TPEG_STATION_MAX_RECORDS = 4,
    OPEN_NAVI_PAS_MAP_MATCHED_POSITION_SIZE = 0x38,
    OPEN_NAVI_PAS_VICS_WARNING_SIZE = 0x180,
    OPEN_NAVI_PAS_CAN_MESSAGE_SIZE = 8
};

/* ARM/QNX wire-side record consumed by SI_GPSData slot 6. */
typedef struct OpenNaviPasMapMatchedAddress {
    const wchar_t *country;
    const wchar_t *city;
    const wchar_t *street;
    const wchar_t *district;
    const wchar_t *cross_street;
    const wchar_t *house_number;
    uint8_t scalar_fields[0x38];
} OpenNaviPasMapMatchedAddress;

/* ARM/QNX by-value geography records used by the SXM callback interfaces. */
typedef struct OpenNaviPasSxmLocation {
    double longitude;
    double latitude;
} OpenNaviPasSxmLocation;

typedef struct OpenNaviPasSxmMbr {
    OpenNaviPasSxmLocation lower_left;
    OpenNaviPasSxmLocation upper_right;
    OpenNaviPasSxmLocation center;
} OpenNaviPasSxmMbr;

uint32_t nc_host_root_adapter_ready(void *proxy);
uint32_t nc_host_root_no_arg_internal_state_cleanup(void *proxy);
uint32_t nc_host_root_signal_initiate_restart_once(void *proxy,
                                                   uint32_t shutdown_reason);
uint32_t nc_host_adapter_ready(void *proxy, uint32_t adapter);
uint32_t nc_host_hmi_adapter_ready(void *proxy);
uint32_t nc_host_hmi_subscribe(void *proxy, uint32_t mask);
uint32_t nc_host_hmi_request_create_request_code(void *proxy, uint32_t handle);
uint32_t nc_host_hmi_request_create_finger_print(void *proxy, uint32_t handle);
uint32_t nc_host_hmi_request_screen_window_identifier(void *proxy, uint32_t handle, uint32_t display);
uint32_t nc_host_hmi_request_hmi_state(void *proxy, uint32_t handle, uint32_t display,
                     uint32_t state);
uint32_t nc_host_hmi_reply_hmi_state_transition(void *proxy, uint32_t handle);
uint32_t nc_host_hmi_reply_screen_change(void *proxy, uint32_t handle, uint32_t result,
                      uint32_t screen_type);
uint32_t nc_host_hmi_reply_screen_theme_change(void *proxy, uint32_t handle);
uint32_t nc_host_hmi_signal_press_state_animation(void *proxy,
                                                 uint32_t press_state,
                                                 uint32_t unused_r2,
                                                 uint32_t x,
                                                 uint32_t y);
uint32_t nc_host_hmi_signal_navigation_hmi_ready(void *proxy, uint32_t display);
uint32_t nc_host_hmi_signal_hmi_state_change(void *proxy, uint32_t display,
                                          uint32_t visibility);
uint32_t nc_host_hmi_signal_screen_change(void *proxy, uint32_t display,
                                          uint32_t screen,
                                          uint32_t reserved0,
                                          uint32_t reserved1);

uint32_t nc_host_hardkey_subscribe(void *proxy, uint32_t mask);
uint32_t nc_host_logging_request_log_buffer_sizes(void *proxy, uint32_t handle);
uint32_t nc_host_logging_signal_write_log_data(void *proxy,
                                        uint32_t methods,
                                        uint32_t reserved_ignored,
                                        uint32_t encoding,
                                        uint8_t severity,
                                        uint32_t byte_count,
                                        const void *text);
uint32_t nc_host_i18n_subscribe(void *proxy, uint32_t mask);
uint32_t nc_host_i18n_request_internationalization_settings(void *proxy, uint32_t handle);
uint32_t nc_host_i18n_signal_time_zone(void *proxy, const void *timezone_0x22c);
uint32_t nc_host_vehicle_subscribe(void *proxy, uint32_t mask);
uint32_t nc_host_vehicle_request_day_night_status(void *proxy, uint32_t handle);
uint32_t nc_host_vehicle_request_day_night_setting(void *proxy, uint32_t handle);
uint32_t nc_host_vehicle_request_vehicle_identification_number(void *proxy, uint32_t handle);
uint32_t nc_host_gps_subscribe(void *proxy, uint32_t mask);
uint32_t nc_host_audio_subscribe_audio_state(void *proxy, uint32_t mask, uint32_t flags);
uint32_t nc_host_audio_request_audio_out_source_descriptor(
    void *proxy, uint32_t handle, uint32_t source);
uint32_t nc_host_audio_request_audio_in_source_descriptor(
    void *proxy, uint32_t handle, uint32_t source);
uint32_t nc_host_audio_request_audio_source_allocation(
    void *proxy, uint32_t handle, uint32_t source);
uint32_t nc_host_audio_request_audio_source_preparation(
    void *proxy, uint32_t handle, uint32_t source);
uint32_t nc_host_audio_signal_deallocate_audio_source(
    void *proxy, uint32_t source);
uint32_t nc_host_audio_reply_audio_source_deactivation(
    void *proxy, uint32_t handle, uint32_t source, uint32_t result);
uint32_t nc_host_audio_signal_audio_source_availability(void *proxy, uint32_t availability);
uint32_t nc_host_audio_request_audio_source_status(
    void *proxy, uint32_t handle, uint32_t source);
uint32_t nc_host_audio_signal_enable_server_audio_source(
    void *proxy, uint32_t source);
uint32_t nc_host_audio_signal_disable_server_audio_source(
    void *proxy, uint32_t source);
uint32_t nc_host_audio_request_audio_source_enable_status(
    void *proxy, uint32_t handle, uint32_t source);
uint32_t nc_host_audio_signal_audio_source_comfort_volume(
    void *proxy, uint32_t source, uint32_t trigger);
uint32_t nc_host_audio_signal_enable_audio_source_volume_separation(
    void *proxy, uint32_t source);
uint32_t nc_host_audio_signal_disable_audio_source_volume_separation(
    void *proxy, uint32_t source);
uint32_t nc_host_audio_request_audio_source_volume_separation_status(
    void *proxy, uint32_t handle, uint32_t source);
uint32_t nc_host_audio_request_audio_source_volume(void *proxy, uint32_t handle, uint32_t source);
uint32_t nc_host_audio_request_audio_source_volume_change(
    void *proxy, uint32_t handle, uint32_t source, uint32_t volume);
uint32_t nc_host_diagnosis_reply_software_component_version(void *proxy, int32_t handle, const void *version4);
uint32_t nc_host_diagnosis_reply_software_component_supplier_identification(void *proxy, int32_t handle, uint16_t supplier);
uint32_t nc_host_diagnosis_request_vehicle_brand(void *proxy, uint32_t handle);
uint32_t nc_host_nav_diagnosis_subscribe_driver_distraction_status(void *proxy, uint32_t mask);
uint32_t nc_host_nav_diagnosis_request_driver_distraction_status(void *proxy, uint32_t handle);
uint32_t nc_host_nav_diagnosis_request_static_setting_11fc(void *proxy, uint32_t handle);
uint32_t nc_host_nav_diagnosis_request_static_setting_11fb(void *proxy, uint32_t handle);
uint32_t nc_host_nav_diagnosis_request_static_setting_11fa(void *proxy, uint32_t handle);
uint32_t nc_host_nav_diagnosis_request_static_setting_11fe(void *proxy, uint32_t handle);
uint32_t nc_host_nav_diagnosis_request_static_setting_11fd(void *proxy, uint32_t handle);
uint32_t nc_host_nav_diagnosis_reply_navigation_database_version_string(void *proxy, int32_t handle, const wchar_t *text);
uint32_t nc_host_nav_diagnosis_reply_navigation_database_status(void *proxy, int32_t handle, uint32_t packed_status);
uint32_t nc_host_cluster_signal_instrument_cluster_display_data(void *proxy, const void *display_data_0xa5c);
uint32_t open_navi_pas_17_4_raw(void *proxy, uint32_t word0);
uint32_t nc_host_cluster_subscribe(void *proxy, uint32_t mask);
uint32_t nc_host_cluster_request_or_signal_no_args(void *proxy);
uint32_t nc_host_cluster_request_tbt_settings_enable_status(void *proxy, uint32_t handle);
uint32_t nc_host_data_exchange_produce_data_exchange_object_types(void *proxy, uint16_t count, const uint32_t *types);
uint32_t nc_host_data_exchange_consume_data_exchange_object_types(void *proxy, uint16_t count, const uint32_t *types);
uint32_t nc_host_data_exchange_request_supported_transport_means_and_buffer_sizes(void *proxy, uint32_t handle);
uint32_t open_navi_pas_19_6_raw(void *proxy, uint32_t word0, uint32_t word1,
                           uint32_t word2);
uint32_t nc_host_data_exchange_request_send_data_exchange_object_produced_by_server(
    void *proxy, uint32_t handle, uint32_t object_id, uint32_t request_word);
uint32_t nc_host_connectivity_subscribe(void *proxy, uint32_t mask);
uint32_t nc_host_connectivity_request_connectivity_status(void *proxy, uint32_t handle, uint32_t service);
uint32_t nc_host_connectivity_request_connection_device_info(void *proxy, uint32_t handle, uint32_t service);
uint32_t nc_host_connectivity_request_signal_strength(void *proxy, uint32_t handle, uint32_t service);
uint32_t nc_host_connectivity_release_connection_for_service(void *proxy,
                                                   uint32_t service);
uint32_t nc_host_voice_initialization_no_op(void *proxy);
uint32_t nc_host_voice_signal_or_request_route_guidance_state(void *proxy);
uint32_t nc_host_voice_reply_alphabet_get(void *proxy, uint32_t handle,
                                       uint32_t result, uint32_t alphabet);
uint32_t nc_host_voice_reply_destination_adb_entry_set(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t nc_host_voice_reply_destination_home_set(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t nc_host_voice_reply_destination_work_set(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t nc_host_voice_reply_destination_status(
    void *proxy, uint32_t handle, uint32_t result, uint32_t status);
uint32_t nc_host_voice_reply_file_from_dbid_get(
    void *proxy, uint32_t handle, uint32_t result, uint32_t entries);
uint32_t nc_host_voice_reply_file_get(void *proxy, uint32_t handle,
                                    uint32_t result, const char *filename);
uint32_t nc_host_voice_reply_guidance_instructions_repeat(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t nc_host_voice_reply_guidance_stop(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t nc_host_voice_reply_guidance_start(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t nc_host_voice_reply_location_check(
    void *proxy, uint32_t handle, uint32_t result, uint32_t status);
uint32_t nc_host_voice_reply_location_get(
    void *proxy, uint32_t handle_ignored, uint32_t result,
    const uint32_t location_types[7], const char *location_ids,
    const char *location_values, const char *orthographic,
    const char *phonetic);
uint32_t nc_host_voice_reply_route_guidance_status(void *proxy, uint32_t handle, uint32_t result,
                       uint32_t guidance_status);
uint32_t nc_host_voice_reply_states_settings(void *proxy, uint32_t handle, uint32_t result,
                       uint32_t states);
uint32_t nc_host_voice_reply_tef_file(void *proxy, uint32_t handle, uint32_t result);
uint32_t nc_host_voice_reply_location_set(
    void *proxy, uint32_t handle, uint32_t result, uint32_t location_status,
    const char *location, uint32_t location_type, uint32_t entries);
uint32_t nc_host_voice_reply_next_available_location_get(
    void *proxy, uint32_t handle, uint32_t result,
    const uint32_t location_types[OPEN_NAVI_PAS_VR_LOCATION_TYPE_COUNT]);
uint32_t nc_host_voice_reply_poi_set(void *proxy, uint32_t handle,
                                   uint32_t result, uint32_t poi_status,
                                   uint32_t entries);
uint32_t nc_host_voice_reply_change_route_guidance(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t nc_host_voice_reply_change_map_direction(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t nc_host_voice_reply_change_map_dimensional_view(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t nc_host_voice_reply_zoom_in(void *proxy, uint32_t handle,
                                   uint32_t result);
uint32_t nc_host_voice_reply_zoom_out(void *proxy, uint32_t handle,
                                    uint32_t result);
uint32_t nc_host_voice_reply_set_destination(
    void *proxy, uint32_t handle, uint32_t result, uint32_t status);
uint32_t nc_host_phone_subscribe(void *proxy, uint32_t mask);
uint32_t nc_host_phone_request_phone_call_availability(void *proxy, uint32_t handle);
uint32_t nc_host_parking_subscribe(void *proxy, uint32_t mask);
uint32_t nc_host_apogee_subscribe(void *proxy, uint32_t mask);
uint32_t nc_host_agw_subscribe(void *proxy, uint32_t mask);
uint32_t open_navi_pas_31_6_raw(void *proxy, uint32_t word0);
uint32_t open_navi_pas_31_10_raw(void *proxy, uint32_t word0, uint32_t word1,
                            uint32_t word2, uint32_t word3, uint32_t word4);
uint32_t open_navi_pas_31_13_raw(void *proxy, uint32_t word0, uint32_t word1,
                            uint32_t word2);
uint32_t nc_host_agw_reply_graphical_weather_map_mode(
    void *proxy, uint32_t handle, uint32_t mode);
uint32_t nc_host_agw_signal_wsa_warning_popup(
    void *proxy, uint32_t alert_id, uint32_t is_manual,
    const void *alert_info_0x2d0);
uint32_t nc_host_agw_request_wsa_set_alert_location(
    void *proxy, uint32_t handle, OpenNaviPasSxmLocation location,
    uint32_t radius, uint32_t radius_unit);
uint32_t nc_host_root_request_user_storage(void *proxy, uint32_t handle,
                                           uint32_t storage_id);
uint32_t nc_host_root_signal_thread_status(void *proxy, uint32_t thread_id,
                         const wchar_t *thread_name, uint32_t thread_status);
uint32_t nc_host_tts_internal_deinitialize(void *proxy,
                                            uint32_t request_word);
uint32_t nc_host_tts_request_play_tts(void *proxy, uint32_t handle,
                                      uint32_t source, uint32_t text_type,
                                      const char *tts_text);
uint32_t nc_host_ews_signal_ews_area_change(void *proxy,
                                           const void *old_area_10,
                                           const void *new_area_10);
uint32_t nc_host_connectivity_request_http_message(
    void *proxy, uint32_t handle, uint32_t service, const wchar_t *url,
    uint32_t method, uint32_t request_size, const void *request_data,
    uint32_t response_size, const void *response_data);
uint32_t nc_host_root_signal_persist(void *proxy, const wchar_t *path);
uint32_t nc_host_live_traffic_request_http_message(
    void *proxy, uint32_t handle, const wchar_t *url, uint32_t method,
    uint32_t request_size, const void *request_data,
    uint32_t response_size, const void *response_data);
uint32_t nc_host_data_exchange_reply_data_exchange_object_required_by_server(
    void *proxy, uint32_t handle, uint32_t object_id, uint32_t object_type,
    const wchar_t *description, uint64_t size, uint32_t reply_type);
uint32_t nc_host_data_exchange_request_send_data_exchange_object_required_by_server(
    void *proxy, uint32_t handle, uint32_t object_id, uint64_t size,
    const void *data, uint64_t total_size, uint32_t abort);
uint32_t nc_host_nav_diagnosis_reply_approved_map_versions(
    void *proxy, int32_t handle, const wchar_t *versions);
uint32_t nc_host_gps_reply_map_matched_address(
    void *proxy, uint32_t handle, const OpenNaviPasMapMatchedAddress *address);
uint32_t nc_host_phone_make_phone_call(void *proxy, uint32_t handle,
                                     const wchar_t *phone_number,
                                     const wchar_t *display_text);
uint32_t nc_host_data_exchange_data_exchange_transfer(
    void *proxy, uint32_t handle, uint32_t object_id, uint64_t size,
    const void *data, uint64_t total_size, uint32_t abort);
uint32_t nc_host_voice_reply_file_info(
    void *proxy, uint32_t handle, uint32_t result,
    const char *language_list, uint32_t file_type, uint32_t entries);
uint32_t nc_host_voice_reply_convert_ortho_to_phonetic(
    void *proxy, uint32_t handle, uint32_t result, uint32_t count,
    const uint32_t *location_types, const char *const *strings);

/*
 * Dormant fixed-ABI seams recovered from identical QR39.6/QR40 concrete
 * consumer implementations.  These wrappers are intentionally not called by
 * the provider until a backend has real state for the corresponding service.
 */
uint32_t nc_host_ews_reply_ews_area(void *proxy, uint32_t handle,
                                    const void *area_10);
uint32_t nc_host_connectivity_signal_connection_needed(void *proxy, uint32_t word0);
uint32_t nc_host_voice_signal_my_dest_deleted_event(void *proxy);
uint32_t nc_host_voice_signal_gui_selection(void *proxy, uint32_t word0);
uint32_t nc_host_voice_reply_route_guidance_status_completion(void *proxy, uint32_t word0,
                                                 uint32_t word1);
uint32_t nc_host_voice_signal_terminate_vr(void *proxy);
uint32_t nc_host_voice_signal_initiate_vr(void *proxy);
uint32_t nc_host_agw_reply_enter_graphical_weather_map(void *proxy,
                                                     uint32_t word0);
uint32_t nc_host_agw_reply_exit_graphical_weather_map(void *proxy,
                                                    uint32_t word0);
uint32_t nc_host_agw_request_agw_panning_mode(void *proxy, uint32_t word0,
                                             uint32_t word1);
uint32_t nc_host_agw_request_agw_set_filter(void *proxy, uint32_t word0,
                                           uint32_t word1);
uint32_t nc_host_agw_request_agw_set_radius_of_interest(
    void *proxy, uint32_t word0, uint32_t word1, uint32_t word2);
uint32_t nc_host_agw_signal_exit_graphical_weather_map(void *proxy);
uint32_t nc_host_rds_subscribe(void *proxy, uint32_t word0);
uint32_t nc_host_rds_request_status(void *proxy, uint32_t word0);
uint32_t nc_host_rds_request_selection_filter_capabilities(void *proxy,
                                                           uint32_t word0);
uint32_t nc_host_rds_request_selectable_filter_list(void *proxy,
                                                   uint32_t word0);
uint32_t nc_host_rds_signal_selection_filter(void *proxy, uint32_t count,
                                             const void *filters_6x20);
uint32_t nc_host_tpeg_adapter_ready(void *proxy);
uint32_t nc_host_tpeg_subscribe(void *proxy, uint32_t word0);
uint32_t nc_host_tpeg_request_station_list(void *proxy, uint32_t word0);
uint32_t nc_host_tpeg_signal_select_station(void *proxy,
                                           const void *station_0x30);
uint32_t nc_host_tpeg_signal_select_station_list(
    void *proxy, uint32_t count, const void *stations_0x30x4);
uint32_t nc_host_gps_request_gps_position(void *proxy, uint32_t word0);
uint32_t nc_host_gps_signal_map_matched_position(void *proxy,
                                               const void *position_0x38);
uint32_t nc_host_root_subscribe(void *proxy, uint32_t word0);
uint32_t nc_host_root_signal_disable_watchdog(void *proxy);
uint32_t nc_host_root_reply_shutdown(void *proxy, uint32_t word0);
uint32_t nc_host_root_request_power_status(void *proxy, uint32_t word0);
uint32_t nc_host_root_supplier_specific_server_function(
    void *proxy, uint32_t word0, uint32_t word1, const void *opaque);
uint32_t nc_host_diagnosis_subscribe_diagnosis_driver_distraction_status(
    void *proxy, uint32_t word0);
uint32_t nc_host_diagnosis_request_diagnosis_driver_distraction_status(
    void *proxy, uint32_t word0);
uint32_t nc_host_diagnosis_signal_error_code(void *proxy, uint32_t word0,
                                       uint32_t word1);
uint32_t nc_host_rotary_subscribe(void *proxy, uint32_t word0);
uint32_t nc_host_live_traffic_adapter_ready(void *proxy);
uint32_t nc_host_live_traffic_subscribe(void *proxy, uint32_t word0);
uint32_t nc_host_live_traffic_signal_start_authentication(void *proxy);
uint32_t nc_host_vics_adapter_ready(void *proxy);
uint32_t nc_host_vics_subscribe(void *proxy);
uint32_t nc_host_vics_request_tune_to_frequency(void *proxy, uint32_t word0,
                                              uint32_t word1);
uint32_t nc_host_vics_signal_vics_wide_emergency_warning(
    void *proxy, uint32_t word0, uint32_t word1, uint32_t word2,
    uint32_t minute, const void *warning_0x180);
uint32_t nc_host_data_exchange_request_offer_data_exchange_object_produced_by_client(
    void *proxy, uint32_t word0, uint32_t word1, uint32_t word2,
    uint32_t word3);
uint32_t nc_host_vehicle_subscribe_can_message(void *proxy, uint32_t word0,
                                          uint32_t word1);
uint32_t nc_host_vehicle_request_odometer(void *proxy, uint32_t word0);
uint32_t nc_host_vehicle_request_vehicle_speed_displayed_on_icd(void *proxy,
                                                        uint32_t word0);
uint32_t nc_host_vehicle_request_can_message_synchronization(void *proxy,
                                                       uint32_t word0,
                                                       uint32_t word1);
uint32_t nc_host_vehicle_signal_can_message(void *proxy, uint32_t word0,
                                        uint32_t word1,
                                        const void *payload_8);
uint32_t nc_host_vehicle_request_ignition_status(void *proxy, uint32_t word0);
uint32_t nc_host_apogee_request_apogee_remove_collection_mbr(void *proxy,
                                                       uint32_t word0,
                                                       uint32_t word1);
uint32_t nc_host_apogee_request_apogee_set_type_collection_mbr(
    void *proxy, uint32_t handle, uint16_t type, OpenNaviPasSxmMbr mbr,
    uint32_t collection_index);
uint32_t nc_host_apogee_request_apogee_set_type_extraction_mbr(
    void *proxy, uint32_t handle, uint16_t type, OpenNaviPasSxmMbr mbr,
    uint32_t collection_index);
uint32_t nc_host_parking_request_parking_set_collection_mbr(
    void *proxy, uint32_t handle, OpenNaviPasSxmMbr mbr,
    uint16_t collection_index);
uint32_t nc_host_nav_diagnosis_reply_status_navigation_database(void *proxy,
                                                      uint32_t word0,
                                                      uint32_t word1);
uint32_t nc_host_i18n_reply_time_zone_information(
    void *proxy, uint32_t handle, const void *timezone_0x22c);

#endif
