#ifndef MOCK_PAS_CALLBACKS_H
#define MOCK_PAS_CALLBACKS_H

#include <stdint.h>
#include <wchar.h>

#include "mock_callback_backend.h"

enum {
    MOCK_PAS_TIMEZONE_SIZE = 0x22c,
    MOCK_PAS_SOFTWARE_VERSION_SIZE = 4,
    MOCK_PAS_VR_STRING_SIZE = 0x200,
    MOCK_PAS_VR_LOCATION_TYPE_COUNT = 7,
    MOCK_PAS_WSA_ALERT_INFO_SIZE = 0x2d0,
    MOCK_PAS_DATA_EXCHANGE_MAX_TYPES = 64,
    MOCK_PAS_DIAG_WIDE_TEXT_MAX = 127,
    MOCK_PAS_EWS_AREA_SIZE = 10,
    MOCK_PAS_RDS_FILTER_RECORD_SIZE = 6,
    MOCK_PAS_RDS_FILTER_MAX_RECORDS = 20,
    MOCK_PAS_TPEG_STATION_SIZE = 0x30,
    MOCK_PAS_TPEG_STATION_MAX_RECORDS = 4,
    MOCK_PAS_MAP_MATCHED_POSITION_SIZE = 0x38,
    MOCK_PAS_VICS_WARNING_SIZE = 0x180,
    MOCK_PAS_CAN_MESSAGE_SIZE = 8
};

/* ARM/QNX wire-side record consumed by SI_GPSData slot 6. */
typedef struct MockPasMapMatchedAddress {
    const wchar_t *country;
    const wchar_t *city;
    const wchar_t *street;
    const wchar_t *district;
    const wchar_t *cross_street;
    const wchar_t *house_number;
    uint8_t scalar_fields[0x38];
} MockPasMapMatchedAddress;

/* ARM/QNX by-value geography records used by the SXM callback interfaces. */
typedef struct MockPasSxmLocation {
    double longitude;
    double latitude;
} MockPasSxmLocation;

typedef struct MockPasSxmMbr {
    MockPasSxmLocation lower_left;
    MockPasSxmLocation upper_right;
    MockPasSxmLocation center;
} MockPasSxmMbr;

uint32_t mock_pas_root_12(void *proxy);
uint32_t mock_pas_root_3(void *proxy);
uint32_t mock_pas_root_5_signalInitiateRestartOnce(void *proxy,
                                                   uint32_t shutdown_reason);
uint32_t mock_pas_all_2(void *proxy, uint32_t adapter);
uint32_t mock_pas_1_2(void *proxy);
uint32_t mock_pas_1_3(void *proxy, uint32_t mask);
uint32_t mock_pas_1_4_requestCreateRequestCode(void *proxy, uint32_t handle);
uint32_t mock_pas_1_5_requestCreateFingerPrint(void *proxy, uint32_t handle);
uint32_t mock_pas_1_6(void *proxy, uint32_t handle, uint32_t display);
uint32_t mock_pas_1_7(void *proxy, uint32_t handle, uint32_t display,
                     uint32_t state);
uint32_t mock_pas_1_9(void *proxy, uint32_t handle);
uint32_t mock_pas_1_10(void *proxy, uint32_t handle, uint32_t result,
                      uint32_t screen_type);
uint32_t mock_pas_1_12(void *proxy, uint32_t handle);
uint32_t mock_pas_1_13_signalPressStateAnimation(void *proxy,
                                                 uint32_t press_state,
                                                 uint32_t unused_r2,
                                                 uint32_t x,
                                                 uint32_t y);
uint32_t mock_pas_1_14(void *proxy, uint32_t display);
uint32_t mock_pas_1_8_signalHMIStateChange(void *proxy, uint32_t display,
                                          uint32_t visibility);
uint32_t mock_pas_1_11_signalScreenChange(void *proxy, uint32_t display,
                                          uint32_t screen,
                                          uint32_t reserved0,
                                          uint32_t reserved1);

uint32_t mock_pas_4_3(void *proxy, uint32_t mask);
uint32_t mock_pas_6_3(void *proxy, uint32_t handle);
uint32_t mock_pas_6_4_signalWriteLogData(void *proxy,
                                        uint32_t methods,
                                        uint32_t reserved_ignored,
                                        uint32_t encoding,
                                        uint8_t severity,
                                        uint32_t byte_count,
                                        const void *text);
uint32_t mock_pas_7_3(void *proxy, uint32_t mask);
uint32_t mock_pas_7_4(void *proxy, uint32_t handle);
uint32_t mock_pas_7_6(void *proxy, const void *timezone_0x22c);
uint32_t mock_pas_8_3(void *proxy, uint32_t mask);
uint32_t mock_pas_8_5(void *proxy, uint32_t handle);
uint32_t mock_pas_8_6(void *proxy, uint32_t handle);
uint32_t mock_pas_8_11(void *proxy, uint32_t handle);
uint32_t mock_pas_9_3(void *proxy, uint32_t mask);
uint32_t mock_pas_10_3(void *proxy, uint32_t mask, uint32_t flags);
uint32_t mock_pas_10_4_requestAudioOutSourceDescriptor(
    void *proxy, uint32_t handle, uint32_t source);
uint32_t mock_pas_10_5_requestAudioInSourceDescriptor(
    void *proxy, uint32_t handle, uint32_t source);
uint32_t mock_pas_10_6_requestAudioSourceAllocation(
    void *proxy, uint32_t handle, uint32_t source);
uint32_t mock_pas_10_7_requestAudioSourcePreparation(
    void *proxy, uint32_t handle, uint32_t source);
uint32_t mock_pas_10_8_signalDeallocateAudioSource(
    void *proxy, uint32_t source);
uint32_t mock_pas_10_9_replyAudioSourceDeactivation(
    void *proxy, uint32_t handle, uint32_t source, uint32_t result);
uint32_t mock_pas_10_10(void *proxy, uint32_t availability);
uint32_t mock_pas_10_11_requestAudioSourceStatus(
    void *proxy, uint32_t handle, uint32_t source);
uint32_t mock_pas_10_12_signalEnableServerAudioSource(
    void *proxy, uint32_t source);
uint32_t mock_pas_10_13_signalDisableServerAudioSource(
    void *proxy, uint32_t source);
uint32_t mock_pas_10_14_requestAudioSourceEnableStatus(
    void *proxy, uint32_t handle, uint32_t source);
uint32_t mock_pas_10_15_signalAudioSourceComfortVolume(
    void *proxy, uint32_t source, uint32_t trigger);
uint32_t mock_pas_10_16_signalEnableAudioSourceVolumeSeparation(
    void *proxy, uint32_t source);
uint32_t mock_pas_10_17_signalDisableAudioSourceVolumeSeparation(
    void *proxy, uint32_t source);
uint32_t mock_pas_10_18_requestAudioSourceVolumeSeparationStatus(
    void *proxy, uint32_t handle, uint32_t source);
uint32_t mock_pas_10_19(void *proxy, uint32_t handle, uint32_t source);
uint32_t mock_pas_10_20_requestAudioSourceVolumeChange(
    void *proxy, uint32_t handle, uint32_t source, uint32_t volume);
uint32_t mock_pas_12_5(void *proxy, int32_t handle, const void *version4);
uint32_t mock_pas_12_6(void *proxy, int32_t handle, uint16_t supplier);
uint32_t mock_pas_12_8(void *proxy, uint32_t handle);
uint32_t mock_pas_13_3(void *proxy, uint32_t mask);
uint32_t mock_pas_13_4(void *proxy, uint32_t handle);
uint32_t mock_pas_13_5(void *proxy, uint32_t handle);
uint32_t mock_pas_13_6(void *proxy, uint32_t handle);
uint32_t mock_pas_13_7(void *proxy, uint32_t handle);
uint32_t mock_pas_13_8(void *proxy, uint32_t handle);
uint32_t mock_pas_13_9(void *proxy, uint32_t handle);
uint32_t mock_pas_13_11(void *proxy, int32_t handle, const wchar_t *text);
uint32_t mock_pas_13_13(void *proxy, int32_t handle, uint32_t packed_status);
uint32_t mock_pas_17_3(void *proxy, const void *display_data_0xa5c);
uint32_t mock_pas_17_4_raw(void *proxy, uint32_t word0);
uint32_t mock_pas_17_5(void *proxy, uint32_t mask);
uint32_t mock_pas_17_6(void *proxy);
uint32_t mock_pas_17_7(void *proxy, uint32_t handle);
uint32_t mock_pas_19_3(void *proxy, uint16_t count, const uint32_t *types);
uint32_t mock_pas_19_4(void *proxy, uint16_t count, const uint32_t *types);
uint32_t mock_pas_19_5(void *proxy, uint32_t handle);
uint32_t mock_pas_19_6_raw(void *proxy, uint32_t word0, uint32_t word1,
                           uint32_t word2);
uint32_t mock_pas_19_7_requestSendDataExchangeObjectProducedByServer(
    void *proxy, uint32_t handle, uint32_t object_id, uint32_t request_word);
uint32_t mock_pas_20_3(void *proxy, uint32_t mask);
uint32_t mock_pas_20_4(void *proxy, uint32_t handle, uint32_t service);
uint32_t mock_pas_20_8(void *proxy, uint32_t handle, uint32_t service);
uint32_t mock_pas_20_9(void *proxy, uint32_t handle, uint32_t service);
uint32_t mock_pas_20_6_releaseConnectionForService(void *proxy,
                                                   uint32_t service);
uint32_t mock_pas_25_3(void *proxy);
uint32_t mock_pas_25_4(void *proxy);
uint32_t mock_pas_25_7_replyAlphabetGet(void *proxy, uint32_t handle,
                                       uint32_t result, uint32_t alphabet);
uint32_t mock_pas_25_8_replyDestinationAdbEntrySet(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t mock_pas_25_9_replyDestinationHomeSet(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t mock_pas_25_10_replyDestinationWorkSet(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t mock_pas_25_11_replyDestinationStatus(
    void *proxy, uint32_t handle, uint32_t result, uint32_t status);
uint32_t mock_pas_25_12_replyFileFromDBIDGet(
    void *proxy, uint32_t handle, uint32_t result, uint32_t entries);
uint32_t mock_pas_25_13_replyFileGet(void *proxy, uint32_t handle,
                                    uint32_t result, const char *filename);
uint32_t mock_pas_25_15_replyGuidanceInstructionsRepeat(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t mock_pas_25_16_replyGuidanceStop(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t mock_pas_25_17_replyGuidanceStart(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t mock_pas_25_18_replyLocationCheck(
    void *proxy, uint32_t handle, uint32_t result, uint32_t status);
uint32_t mock_pas_25_19_replyLocationGet(
    void *proxy, uint32_t handle_ignored, uint32_t result,
    const uint32_t location_types[7], const char *location_ids,
    const char *location_values, const char *orthographic,
    const char *phonetic);
uint32_t mock_pas_25_23(void *proxy, uint32_t handle, uint32_t result,
                       uint32_t guidance_status);
uint32_t mock_pas_25_24(void *proxy, uint32_t handle, uint32_t result,
                       uint32_t states);
uint32_t mock_pas_25_25(void *proxy, uint32_t handle, uint32_t result);
uint32_t mock_pas_25_20_replyLocationSet(
    void *proxy, uint32_t handle, uint32_t result, uint32_t location_status,
    const char *location, uint32_t location_type, uint32_t entries);
uint32_t mock_pas_25_21_replyNextAvailableLocationGet(
    void *proxy, uint32_t handle, uint32_t result,
    const uint32_t location_types[MOCK_PAS_VR_LOCATION_TYPE_COUNT]);
uint32_t mock_pas_25_22_replyPOISet(void *proxy, uint32_t handle,
                                   uint32_t result, uint32_t poi_status,
                                   uint32_t entries);
uint32_t mock_pas_25_26_replyChangeRouteGuidance(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t mock_pas_25_27_replyChangeMapDirection(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t mock_pas_25_28_replyChangeMapDimensionalView(
    void *proxy, uint32_t handle, uint32_t result);
uint32_t mock_pas_25_29_replyZoomIn(void *proxy, uint32_t handle,
                                   uint32_t result);
uint32_t mock_pas_25_30_replyZoomOut(void *proxy, uint32_t handle,
                                    uint32_t result);
uint32_t mock_pas_25_34_replySetDestination(
    void *proxy, uint32_t handle, uint32_t result, uint32_t status);
uint32_t mock_pas_26_3(void *proxy, uint32_t mask);
uint32_t mock_pas_26_5(void *proxy, uint32_t handle);
uint32_t mock_pas_29_3(void *proxy, uint32_t mask);
uint32_t mock_pas_30_3(void *proxy, uint32_t mask);
uint32_t mock_pas_31_3(void *proxy, uint32_t mask);
uint32_t mock_pas_31_6_raw(void *proxy, uint32_t word0);
uint32_t mock_pas_31_10_raw(void *proxy, uint32_t word0, uint32_t word1,
                            uint32_t word2, uint32_t word3, uint32_t word4);
uint32_t mock_pas_31_13_raw(void *proxy, uint32_t word0, uint32_t word1,
                            uint32_t word2);
uint32_t mock_pas_31_8_replyGraphicalWeatherMapMode(
    void *proxy, uint32_t handle, uint32_t mode);
uint32_t mock_pas_31_15_signalWsaWarningPopup(
    void *proxy, uint32_t alert_id, uint32_t is_manual,
    const void *alert_info_0x2d0);
uint32_t mock_pas_31_12_requestWsaSetAlertLocation(
    void *proxy, uint32_t handle, MockPasSxmLocation location,
    uint32_t radius, uint32_t radius_unit);
uint32_t mock_pas_root_9_requestUserStorage(void *proxy, uint32_t handle,
                                           uint32_t storage_id);
uint32_t mock_pas_root_10(void *proxy, uint32_t thread_id,
                         const wchar_t *thread_name, uint32_t thread_status);
uint32_t mock_pas_24_4_internalDeinitialize(void *proxy,
                                            uint32_t request_word);
uint32_t mock_pas_24_3_requestPlayTTS(void *proxy, uint32_t handle,
                                      uint32_t source, uint32_t text_type,
                                      const char *tts_text);
uint32_t mock_pas_32_4_signalEWSAreaChange(void *proxy,
                                           const void *old_area_10,
                                           const void *new_area_10);
uint32_t mock_pas_20_7_requestHTTPMessage(
    void *proxy, uint32_t handle, uint32_t service, const wchar_t *url,
    uint32_t method, uint32_t request_size, const void *request_data,
    uint32_t response_size, const void *response_data);
uint32_t mock_pas_root_6_signalPersist(void *proxy, const wchar_t *path);
uint32_t mock_pas_21_4_requestHTTPMessage(
    void *proxy, uint32_t handle, const wchar_t *url, uint32_t method,
    uint32_t request_size, const void *request_data,
    uint32_t response_size, const void *response_data);
uint32_t mock_pas_19_8_replyDataExchangeObjectRequiredByServer(
    void *proxy, uint32_t handle, uint32_t object_id, uint32_t object_type,
    const wchar_t *description, uint64_t size, uint32_t reply_type);
uint32_t mock_pas_19_9_requestSendDataExchangeObjectRequiredByServer(
    void *proxy, uint32_t handle, uint32_t object_id, uint64_t size,
    const void *data, uint64_t total_size, uint32_t abort);
uint32_t mock_pas_13_10_replyApprovedMapVersions(
    void *proxy, int32_t handle, const wchar_t *versions);
uint32_t mock_pas_9_6_replyMapMatchedAddress(
    void *proxy, uint32_t handle, const MockPasMapMatchedAddress *address);
uint32_t mock_pas_26_4_makePhoneCall(void *proxy, uint32_t handle,
                                     const wchar_t *phone_number,
                                     const wchar_t *display_text);
uint32_t mock_pas_19_11_dataExchangeTransfer(
    void *proxy, uint32_t handle, uint32_t object_id, uint64_t size,
    const void *data, uint64_t total_size, uint32_t abort);
uint32_t mock_pas_25_14_replyFileInfo(
    void *proxy, uint32_t handle, uint32_t result,
    const char *language_list, uint32_t file_type, uint32_t entries);
uint32_t mock_pas_25_35_replyConvertOrthoToPhonetic(
    void *proxy, uint32_t handle, uint32_t result, uint32_t count,
    const uint32_t *location_types, const char *const *strings);

/*
 * Dormant fixed-ABI seams recovered from identical QR39.6/QR40 concrete
 * consumer implementations.  These wrappers are intentionally not called by
 * the provider until a backend has real state for the corresponding service.
 */
uint32_t mock_pas_32_3_replyEWSArea(void *proxy, uint32_t handle,
                                    const void *area_10);
uint32_t mock_pas_20_5_signalConnectionNeeded(void *proxy, uint32_t word0);
uint32_t mock_pas_25_5_signalMyDestDeletedEvent(void *proxy);
uint32_t mock_pas_25_6_signalGUISelection(void *proxy, uint32_t word0);
uint32_t mock_pas_25_31_replyRouteGuidanceStatus(void *proxy, uint32_t word0,
                                                 uint32_t word1);
uint32_t mock_pas_25_32_signalTerminateVR(void *proxy);
uint32_t mock_pas_25_33_signalInitiateVR(void *proxy);
uint32_t mock_pas_31_4_replyEnterGraphicalWeatherMap(void *proxy,
                                                     uint32_t word0);
uint32_t mock_pas_31_5_replyExitGraphicalWeatherMap(void *proxy,
                                                    uint32_t word0);
uint32_t mock_pas_31_7_requestAgwPanningMode(void *proxy, uint32_t word0,
                                             uint32_t word1);
uint32_t mock_pas_31_9_requestAgwSetFilter(void *proxy, uint32_t word0,
                                           uint32_t word1);
uint32_t mock_pas_31_11_requestAgwSetRadiusOfInterest(
    void *proxy, uint32_t word0, uint32_t word1, uint32_t word2);
uint32_t mock_pas_31_14_signalExitGraphicalWeatherMap(void *proxy);
uint32_t mock_pas_14_3_subscribe(void *proxy, uint32_t word0);
uint32_t mock_pas_14_4_requestStatus(void *proxy, uint32_t word0);
uint32_t mock_pas_14_5_requestSelectionFilterCapabilities(void *proxy,
                                                           uint32_t word0);
uint32_t mock_pas_14_6_requestSelectableFilterList(void *proxy,
                                                   uint32_t word0);
uint32_t mock_pas_14_7_signalSelectionFilter(void *proxy, uint32_t count,
                                             const void *filters_6x20);
uint32_t mock_pas_27_2_adapterReady(void *proxy);
uint32_t mock_pas_27_3_subscribe(void *proxy, uint32_t word0);
uint32_t mock_pas_27_4_requestStationList(void *proxy, uint32_t word0);
uint32_t mock_pas_27_5_signalSelectStation(void *proxy,
                                           const void *station_0x30);
uint32_t mock_pas_27_6_signalSelectStationList(
    void *proxy, uint32_t count, const void *stations_0x30x4);
uint32_t mock_pas_9_4_requestGPSPosition(void *proxy, uint32_t word0);
uint32_t mock_pas_9_5_signalMapMatchedPosition(void *proxy,
                                               const void *position_0x38);
uint32_t mock_pas_root_2_subscribe(void *proxy, uint32_t word0);
uint32_t mock_pas_root_4_signalDisableWatchdog(void *proxy);
uint32_t mock_pas_root_7_replyShutdown(void *proxy, uint32_t word0);
uint32_t mock_pas_root_8_requestPowerStatus(void *proxy, uint32_t word0);
uint32_t mock_pas_root_11_supplierSpecificServerFunction(
    void *proxy, uint32_t word0, uint32_t word1, const void *opaque);
uint32_t mock_pas_12_3_subscribeDiagnosisDriverDistractionStatus(
    void *proxy, uint32_t word0);
uint32_t mock_pas_12_4_requestDiagnosisDriverDistractionStatus(
    void *proxy, uint32_t word0);
uint32_t mock_pas_12_7_signalErrorCode(void *proxy, uint32_t word0,
                                       uint32_t word1);
uint32_t mock_pas_23_3_subscribe(void *proxy, uint32_t word0);
uint32_t mock_pas_21_2_adapterReady(void *proxy);
uint32_t mock_pas_21_3_subscribe(void *proxy, uint32_t word0);
uint32_t mock_pas_21_5_signalStartAuthentication(void *proxy);
uint32_t mock_pas_28_2_adapterReady(void *proxy);
uint32_t mock_pas_28_3_subscribe(void *proxy);
uint32_t mock_pas_28_4_requestTuneToFrequency(void *proxy, uint32_t word0,
                                              uint32_t word1);
uint32_t mock_pas_28_5_signalVICSWideEmergencyWarning(
    void *proxy, uint32_t word0, uint32_t word1, uint32_t word2,
    uint32_t minute, const void *warning_0x180);
uint32_t mock_pas_19_10_requestOfferDataExchangeObjectProducedByClient(
    void *proxy, uint32_t word0, uint32_t word1, uint32_t word2,
    uint32_t word3);
uint32_t mock_pas_8_4_subscribeCANMessage(void *proxy, uint32_t word0,
                                          uint32_t word1);
uint32_t mock_pas_8_7_requestOdometer(void *proxy, uint32_t word0);
uint32_t mock_pas_8_8_requestVehicleSpeedDisplayedOnICD(void *proxy,
                                                        uint32_t word0);
uint32_t mock_pas_8_9_requestCANMessageSynchronization(void *proxy,
                                                       uint32_t word0,
                                                       uint32_t word1);
uint32_t mock_pas_8_10_signalCANMessage(void *proxy, uint32_t word0,
                                        uint32_t word1,
                                        const void *payload_8);
uint32_t mock_pas_8_12_requestIgnitionStatus(void *proxy, uint32_t word0);
uint32_t mock_pas_30_6_requestApogeeRemoveCollectionMBR(void *proxy,
                                                       uint32_t word0,
                                                       uint32_t word1);
uint32_t mock_pas_30_4_requestApogeeSetTypeCollectionMBR(
    void *proxy, uint32_t handle, uint16_t type, MockPasSxmMbr mbr,
    uint32_t collection_index);
uint32_t mock_pas_30_5_requestApogeeSetTypeExtractionMBR(
    void *proxy, uint32_t handle, uint16_t type, MockPasSxmMbr mbr,
    uint32_t collection_index);
uint32_t mock_pas_29_4_requestParkingSetCollectionMBR(
    void *proxy, uint32_t handle, MockPasSxmMbr mbr,
    uint16_t collection_index);
uint32_t mock_pas_13_12_replyStatusNavigationDatabase(void *proxy,
                                                      uint32_t word0,
                                                      uint32_t word1);
uint32_t mock_pas_7_5_replyTimeZoneInformation(
    void *proxy, uint32_t handle, const void *timezone_0x22c);

#endif
