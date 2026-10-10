/* Handwritten typed ABI adapters, checked against the callback manifests.
 * The generated raw-capture trampolines are a separate diagnostic path; they
 * do not supply prototypes or decide which callbacks the provider emits. */
#include "mock_pas_callbacks.h"

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

static const void *const *mock_pas_vtable(void *proxy)
{
    return proxy != NULL ? *(const void *const *const *)proxy : NULL;
}

static void *mock_pas_slot(void *proxy, uint32_t slot)
{
    const void *const *vtable = mock_pas_vtable(proxy);

    return vtable != NULL ? (void *)vtable[slot] : NULL;
}

static uint32_t mock_pas_status(uint32_t adapter, uint32_t slot,
                                uint32_t status, MockCallbackClass kind)
{
    mock_callback_record_status(adapter, slot, status, kind);
    return status;
}

static int mock_pas_cstr8_within(const char *text, size_t maximum)
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

static int mock_pas_cstr16_within(const wchar_t *text, size_t maximum)
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
        PasCallback0 callback = (PasCallback0)mock_pas_slot(proxy, slot);      \
        uint32_t status = callback != NULL ? callback(proxy) : 1u;             \
        return mock_pas_status(adapter, slot, status, kind);                   \
    }

#define DEFINE_PAS_1(name, adapter, slot, kind)                               \
    uint32_t name(void *proxy, uint32_t value)                                 \
    {                                                                          \
        PasCallback1U32 callback =                                             \
            (PasCallback1U32)mock_pas_slot(proxy, slot);                       \
        uint32_t status = callback != NULL ? callback(proxy, value) : 1u;      \
        return mock_pas_status(adapter, slot, status, kind);                   \
    }

#define DEFINE_PAS_2(name, adapter, slot, kind)                               \
    uint32_t name(void *proxy, uint32_t a, uint32_t b)                         \
    {                                                                          \
        PasCallback2U32 callback =                                             \
            (PasCallback2U32)mock_pas_slot(proxy, slot);                       \
        uint32_t status = callback != NULL ? callback(proxy, a, b) : 1u;       \
        return mock_pas_status(adapter, slot, status, kind);                   \
    }

#define DEFINE_PAS_3(name, adapter, slot, kind)                               \
    uint32_t name(void *proxy, uint32_t a, uint32_t b, uint32_t c)             \
    {                                                                          \
        PasCallback3U32 callback =                                             \
            (PasCallback3U32)mock_pas_slot(proxy, slot);                       \
        uint32_t status = callback != NULL ? callback(proxy, a, b, c) : 1u;    \
        return mock_pas_status(adapter, slot, status, kind);                   \
    }

#define DEFINE_PAS_4(name, adapter, slot, kind)                               \
    uint32_t name(void *proxy, uint32_t a, uint32_t b, uint32_t c,             \
                  uint32_t d)                                                  \
    {                                                                          \
        PasCallback4U32 callback =                                             \
            (PasCallback4U32)mock_pas_slot(proxy, slot);                       \
        uint32_t status = callback != NULL                                     \
                              ? callback(proxy, a, b, c, d)                    \
                              : 1u;                                            \
        return mock_pas_status(adapter, slot, status, kind);                   \
    }

#define DEFINE_PAS_5(name, adapter, slot, kind)                               \
    uint32_t name(void *proxy, uint32_t a, uint32_t b, uint32_t c,             \
                  uint32_t d, uint32_t e)                                      \
    {                                                                          \
        PasCallback5U32 callback =                                             \
            (PasCallback5U32)mock_pas_slot(proxy, slot);                       \
        uint32_t status = callback != NULL                                     \
                              ? callback(proxy, a, b, c, d, e)                 \
                              : 1u;                                            \
        return mock_pas_status(adapter, slot, status, kind);                   \
    }

DEFINE_PAS_0(mock_pas_root_12, UINT32_MAX, 12u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_0(mock_pas_root_3, UINT32_MAX, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_root_5_signalInitiateRestartOnce, UINT32_MAX, 5u,
             MOCK_CALLBACK_OPTIONAL)

uint32_t mock_pas_all_2(void *proxy, uint32_t adapter)
{
    PasCallback0 callback = (PasCallback0)mock_pas_slot(proxy, 2u);
    uint32_t status = callback != NULL ? callback(proxy) : 1u;

    return mock_pas_status(adapter, 2u, status, MOCK_CALLBACK_CRITICAL);
}

DEFINE_PAS_0(mock_pas_1_2, 1u, 2u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(mock_pas_1_3, 1u, 3u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(mock_pas_1_4_requestCreateRequestCode, 1u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_1_5_requestCreateFingerPrint, 1u, 5u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_1_6, 1u, 6u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_3(mock_pas_1_7, 1u, 7u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(mock_pas_1_9, 1u, 9u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_3(mock_pas_1_10, 1u, 10u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(mock_pas_1_12, 1u, 12u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_1_14, 1u, 14u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_2(mock_pas_1_8_signalHMIStateChange, 1u, 8u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_4(mock_pas_1_11_signalScreenChange, 1u, 11u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_4_3, 4u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_6_3, 6u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_7_3, 7u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_7_4, 7u, 4u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_8_3, 8u, 3u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(mock_pas_8_5, 8u, 5u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(mock_pas_8_6, 8u, 6u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(mock_pas_8_11, 8u, 11u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_9_3, 9u, 3u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_2(mock_pas_10_3, 10u, 3u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_2(mock_pas_10_4_requestAudioOutSourceDescriptor, 10u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_10_5_requestAudioInSourceDescriptor, 10u, 5u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_10_6_requestAudioSourceAllocation, 10u, 6u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_10_7_requestAudioSourcePreparation, 10u, 7u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_10_8_signalDeallocateAudioSource, 10u, 8u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(mock_pas_10_9_replyAudioSourceDeactivation, 10u, 9u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_10_10, 10u, 10u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_10_11_requestAudioSourceStatus, 10u, 11u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_10_12_signalEnableServerAudioSource, 10u, 12u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_10_13_signalDisableServerAudioSource, 10u, 13u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_10_14_requestAudioSourceEnableStatus, 10u, 14u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_10_15_signalAudioSourceComfortVolume, 10u, 15u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_10_16_signalEnableAudioSourceVolumeSeparation, 10u, 16u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_10_17_signalDisableAudioSourceVolumeSeparation, 10u, 17u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_10_18_requestAudioSourceVolumeSeparationStatus, 10u, 18u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_10_19, 10u, 19u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_3(mock_pas_10_20_requestAudioSourceVolumeChange, 10u, 20u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_12_8, 12u, 8u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_13_3, 13u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_13_4, 13u, 4u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_13_5, 13u, 5u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_13_6, 13u, 6u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_13_7, 13u, 7u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_13_8, 13u, 8u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_13_9, 13u, 9u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_17_5, 17u, 5u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(mock_pas_17_4_raw, 17u, 4u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(mock_pas_17_6, 17u, 6u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(mock_pas_17_7, 17u, 7u, MOCK_CALLBACK_CRITICAL)
DEFINE_PAS_1(mock_pas_19_5, 19u, 5u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(mock_pas_19_6_raw, 19u, 6u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(mock_pas_19_7_requestSendDataExchangeObjectProducedByServer,
             19u, 7u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_20_3, 20u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_20_4, 20u, 4u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_20_8, 20u, 8u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_20_9, 20u, 9u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_20_6_releaseConnectionForService, 20u, 6u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(mock_pas_25_3, 25u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(mock_pas_25_4, 25u, 4u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(mock_pas_25_7_replyAlphabetGet, 25u, 7u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_25_8_replyDestinationAdbEntrySet, 25u, 8u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_25_9_replyDestinationHomeSet, 25u, 9u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_25_10_replyDestinationWorkSet, 25u, 10u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(mock_pas_25_11_replyDestinationStatus, 25u, 11u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(mock_pas_25_12_replyFileFromDBIDGet, 25u, 12u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_25_15_replyGuidanceInstructionsRepeat, 25u, 15u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_25_16_replyGuidanceStop, 25u, 16u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_25_17_replyGuidanceStart, 25u, 17u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(mock_pas_25_18_replyLocationCheck, 25u, 18u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(mock_pas_25_23, 25u, 23u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(mock_pas_25_24, 25u, 24u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_25_25, 25u, 25u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_4(mock_pas_25_22_replyPOISet, 25u, 22u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_25_26_replyChangeRouteGuidance, 25u, 26u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_25_27_replyChangeMapDirection, 25u, 27u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_25_28_replyChangeMapDimensionalView, 25u, 28u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_25_29_replyZoomIn, 25u, 29u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_25_30_replyZoomOut, 25u, 30u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(mock_pas_25_34_replySetDestination, 25u, 34u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_26_3, 26u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_26_5, 26u, 5u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_24_4_internalDeinitialize, 24u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_29_3, 29u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_30_3, 30u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_31_3, 31u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_31_6_raw, 31u, 6u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_5(mock_pas_31_10_raw, 31u, 10u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(mock_pas_31_13_raw, 31u, 13u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_31_8_replyGraphicalWeatherMapMode, 31u, 8u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_root_9_requestUserStorage, UINT32_MAX, 9u,
             MOCK_CALLBACK_OPTIONAL)

/* Concrete QR39.6/QR40 fixed-word dormant seams. */
DEFINE_PAS_1(mock_pas_20_5_signalConnectionNeeded, 20u, 5u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(mock_pas_25_5_signalMyDestDeletedEvent, 25u, 5u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_25_6_signalGUISelection, 25u, 6u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_25_31_replyRouteGuidanceStatus, 25u, 31u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(mock_pas_25_32_signalTerminateVR, 25u, 32u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(mock_pas_25_33_signalInitiateVR, 25u, 33u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_31_4_replyEnterGraphicalWeatherMap, 31u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_31_5_replyExitGraphicalWeatherMap, 31u, 5u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_31_7_requestAgwPanningMode, 31u, 7u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_31_9_requestAgwSetFilter, 31u, 9u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_3(mock_pas_31_11_requestAgwSetRadiusOfInterest, 31u, 11u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(mock_pas_31_14_signalExitGraphicalWeatherMap, 31u, 14u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_14_3_subscribe, 14u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_14_4_requestStatus, 14u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_14_5_requestSelectionFilterCapabilities, 14u, 5u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_14_6_requestSelectableFilterList, 14u, 6u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(mock_pas_27_2_adapterReady, 27u, 2u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_27_3_subscribe, 27u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_27_4_requestStationList, 27u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_9_4_requestGPSPosition, 9u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_root_2_subscribe, UINT32_MAX, 2u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(mock_pas_root_4_signalDisableWatchdog, UINT32_MAX, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_root_7_replyShutdown, UINT32_MAX, 7u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_root_8_requestPowerStatus, UINT32_MAX, 8u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_12_3_subscribeDiagnosisDriverDistractionStatus, 12u, 3u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_12_4_requestDiagnosisDriverDistractionStatus, 12u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_12_7_signalErrorCode, 12u, 7u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_23_3_subscribe, 23u, 3u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(mock_pas_21_2_adapterReady, 21u, 2u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_21_3_subscribe, 21u, 3u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(mock_pas_21_5_signalStartAuthentication, 21u, 5u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(mock_pas_28_2_adapterReady, 28u, 2u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_0(mock_pas_28_3_subscribe, 28u, 3u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_28_4_requestTuneToFrequency, 28u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_4(mock_pas_19_10_requestOfferDataExchangeObjectProducedByClient,
             19u, 10u, MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_8_4_subscribeCANMessage, 8u, 4u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_8_7_requestOdometer, 8u, 7u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_8_8_requestVehicleSpeedDisplayedOnICD, 8u, 8u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_8_9_requestCANMessageSynchronization, 8u, 9u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_1(mock_pas_8_12_requestIgnitionStatus, 8u, 12u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_30_6_requestApogeeRemoveCollectionMBR, 30u, 6u,
             MOCK_CALLBACK_OPTIONAL)
DEFINE_PAS_2(mock_pas_13_12_replyStatusNavigationDatabase, 13u, 12u,
             MOCK_CALLBACK_OPTIONAL)

uint32_t mock_pas_31_12_requestWsaSetAlertLocation(
    void *proxy, uint32_t handle, MockPasSxmLocation location,
    uint32_t radius, uint32_t radius_unit)
{
    typedef uint32_t (*Callback)(void *, uint32_t, MockPasSxmLocation,
                                 uint32_t, uint32_t);
    Callback callback = (Callback)mock_pas_slot(proxy, 12u);
    uint32_t status = callback != NULL
                          ? callback(proxy, handle, location, radius,
                                     radius_unit)
                          : 1u;
    return mock_pas_status(31u, 12u, status, MOCK_CALLBACK_OPTIONAL);
}

static uint32_t mock_pas_apogee_set_mbr(
    void *proxy, uint32_t slot, uint32_t handle, uint16_t type,
    MockPasSxmMbr mbr, uint32_t collection_index)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint16_t,
                                 MockPasSxmMbr, uint32_t);
    Callback callback = (Callback)mock_pas_slot(proxy, slot);
    uint32_t status = callback != NULL
                          ? callback(proxy, handle, type, mbr,
                                     collection_index)
                          : 1u;
    return mock_pas_status(30u, slot, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_30_4_requestApogeeSetTypeCollectionMBR(
    void *proxy, uint32_t handle, uint16_t type, MockPasSxmMbr mbr,
    uint32_t collection_index)
{
    return mock_pas_apogee_set_mbr(proxy, 4u, handle, type, mbr,
                                   collection_index);
}

uint32_t mock_pas_30_5_requestApogeeSetTypeExtractionMBR(
    void *proxy, uint32_t handle, uint16_t type, MockPasSxmMbr mbr,
    uint32_t collection_index)
{
    return mock_pas_apogee_set_mbr(proxy, 5u, handle, type, mbr,
                                   collection_index);
}

uint32_t mock_pas_29_4_requestParkingSetCollectionMBR(
    void *proxy, uint32_t handle, MockPasSxmMbr mbr,
    uint16_t collection_index)
{
    typedef uint32_t (*Callback)(void *, uint32_t, MockPasSxmMbr, uint16_t);
    Callback callback = (Callback)mock_pas_slot(proxy, 4u);
    uint32_t status = callback != NULL
                          ? callback(proxy, handle, mbr, collection_index)
                          : 1u;
    return mock_pas_status(29u, 4u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_root_11_supplierSpecificServerFunction(
    void *proxy, uint32_t word0, uint32_t word1, const void *opaque)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, const void *);
    Callback callback = (Callback)mock_pas_slot(proxy, 11u);
    uint32_t status = callback != NULL
                          ? callback(proxy, word0, word1, opaque)
                          : 1u;

    /* The concrete consumer only logs the pointer during this call. */
    return mock_pas_status(UINT32_MAX, 11u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_32_3_replyEWSArea(void *proxy, uint32_t handle,
                                    const void *area_10)
{
    typedef uint32_t (*Callback)(void *, uint32_t, const void *);
    uint8_t owned[MOCK_PAS_EWS_AREA_SIZE];
    Callback callback = (Callback)mock_pas_slot(proxy, 3u);
    uint32_t status = 1u;

    if (callback != NULL && area_10 != NULL) {
        memcpy(owned, area_10, sizeof(owned));
        status = callback(proxy, handle, owned);
    }
    return mock_pas_status(32u, 3u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_14_7_signalSelectionFilter(void *proxy, uint32_t count,
                                             const void *filters_6x20)
{
    typedef uint32_t (*Callback)(void *, uint32_t, const void *);
    uint8_t owned[MOCK_PAS_RDS_FILTER_RECORD_SIZE *
                  MOCK_PAS_RDS_FILTER_MAX_RECORDS];
    uint32_t copied_count = count < MOCK_PAS_RDS_FILTER_MAX_RECORDS
                                ? count
                                : MOCK_PAS_RDS_FILTER_MAX_RECORDS;
    const void *argument = NULL;
    Callback callback = (Callback)mock_pas_slot(proxy, 7u);
    uint32_t status = 1u;

    if (callback != NULL && (copied_count == 0u || filters_6x20 != NULL)) {
        if (copied_count != 0u) {
            memcpy(owned, filters_6x20,
                   (size_t)copied_count * MOCK_PAS_RDS_FILTER_RECORD_SIZE);
            argument = owned;
        }
        status = callback(proxy, count, argument);
    }
    return mock_pas_status(14u, 7u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_27_5_signalSelectStation(void *proxy,
                                           const void *station_0x30)
{
    typedef uint32_t (*Callback)(void *, const void *);
    uint8_t owned[MOCK_PAS_TPEG_STATION_SIZE];
    Callback callback = (Callback)mock_pas_slot(proxy, 5u);
    uint32_t status = 1u;

    if (callback != NULL && station_0x30 != NULL) {
        memcpy(owned, station_0x30, sizeof(owned));
        status = callback(proxy, owned);
    }
    return mock_pas_status(27u, 5u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_27_6_signalSelectStationList(
    void *proxy, uint32_t count, const void *stations_0x30x4)
{
    typedef uint32_t (*Callback)(void *, uint32_t, const void *);
    uint8_t owned[MOCK_PAS_TPEG_STATION_SIZE *
                  MOCK_PAS_TPEG_STATION_MAX_RECORDS];
    const void *argument = NULL;
    Callback callback = (Callback)mock_pas_slot(proxy, 6u);
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
    return mock_pas_status(27u, 6u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_9_5_signalMapMatchedPosition(void *proxy,
                                               const void *position_0x38)
{
    typedef uint32_t (*Callback)(void *, const void *);
    uint8_t owned[MOCK_PAS_MAP_MATCHED_POSITION_SIZE];
    Callback callback = (Callback)mock_pas_slot(proxy, 5u);
    uint32_t status = 1u;

    if (callback != NULL && position_0x38 != NULL) {
        memcpy(owned, position_0x38, sizeof(owned));
        status = callback(proxy, owned);
    }
    return mock_pas_status(9u, 5u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_28_5_signalVICSWideEmergencyWarning(
    void *proxy, uint32_t word0, uint32_t word1, uint32_t word2,
    uint32_t minute, const void *warning_0x180)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, uint32_t,
                                 uint32_t, const void *);
    uint8_t owned[MOCK_PAS_VICS_WARNING_SIZE];
    Callback callback = (Callback)mock_pas_slot(proxy, 5u);
    uint32_t status = 1u;

    if (callback != NULL && warning_0x180 != NULL) {
        memcpy(owned, warning_0x180, sizeof(owned));
        status = callback(proxy, word0, word1, word2, minute, owned);
    }
    return mock_pas_status(28u, 5u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_8_10_signalCANMessage(void *proxy, uint32_t word0,
                                        uint32_t word1,
                                        const void *payload_8)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, const void *);
    uint8_t owned[MOCK_PAS_CAN_MESSAGE_SIZE];
    Callback callback = (Callback)mock_pas_slot(proxy, 10u);
    uint32_t status = 1u;

    if (callback != NULL && payload_8 != NULL) {
        memcpy(owned, payload_8, sizeof(owned));
        status = callback(proxy, word0, word1, owned);
    }
    return mock_pas_status(8u, 10u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_7_5_replyTimeZoneInformation(
    void *proxy, uint32_t handle, const void *timezone_0x22c)
{
    typedef uint32_t (*Callback)(void *, uint32_t, const void *);
    uint8_t owned[MOCK_PAS_TIMEZONE_SIZE];
    Callback callback = (Callback)mock_pas_slot(proxy, 5u);
    uint32_t status = 1u;

    if (callback != NULL && timezone_0x22c != NULL) {
        memcpy(owned, timezone_0x22c, sizeof(owned));
        status = callback(proxy, handle, owned);
    }
    return mock_pas_status(7u, 5u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_6_4_signalWriteLogData(void *proxy,
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
        (SignalWriteLogData)mock_pas_slot(proxy, 4u);
    uint32_t status = callback != NULL
                          ? callback(proxy, methods, reserved_ignored,
                                     encoding, severity, byte_count, text)
                          : 1u;

    return mock_pas_status(6u, 4u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_25_19_replyLocationGet(
    void *proxy, uint32_t handle_ignored, uint32_t result,
    const uint32_t location_types[7], const char *location_ids,
    const char *location_values, const char *orthographic,
    const char *phonetic)
{
    typedef uint32_t (*ReplyLocationGet)(
        void *, uint32_t, uint32_t, const uint32_t *, const char *,
        const char *, const char *, const char *);
    ReplyLocationGet callback =
        (ReplyLocationGet)mock_pas_slot(proxy, 19u);
    uint32_t status = callback != NULL
                          ? callback(proxy, handle_ignored, result,
                                     location_types, location_ids,
                                     location_values, orthographic, phonetic)
                          : 1u;

    return mock_pas_status(25u, 19u, status, MOCK_CALLBACK_OPTIONAL);
}

static void mock_pas_copy_vr_string(char owned[MOCK_PAS_VR_STRING_SIZE],
                                    const char *source)
{
    size_t length = 0u;

    while (length + 1u < MOCK_PAS_VR_STRING_SIZE && source[length] != '\0') {
        owned[length] = source[length];
        ++length;
    }
    owned[length] = '\0';
}

uint32_t mock_pas_25_13_replyFileGet(void *proxy, uint32_t handle,
                                    uint32_t result, const char *filename)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, const char *);
    char owned[MOCK_PAS_VR_STRING_SIZE];
    Callback callback = (Callback)mock_pas_slot(proxy, 13u);
    uint32_t status = 1u;

    if (callback != NULL && filename != NULL) {
        mock_pas_copy_vr_string(owned, filename);
        status = callback(proxy, handle, result, owned);
    }
    return mock_pas_status(25u, 13u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_25_20_replyLocationSet(
    void *proxy, uint32_t handle, uint32_t result, uint32_t location_status,
    const char *location, uint32_t location_type, uint32_t entries)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, uint32_t,
                                 const char *, uint32_t, uint32_t);
    char owned[MOCK_PAS_VR_STRING_SIZE];
    Callback callback = (Callback)mock_pas_slot(proxy, 20u);
    uint32_t status = 1u;

    if (callback != NULL && location != NULL) {
        mock_pas_copy_vr_string(owned, location);
        status = callback(proxy, handle, result, location_status, owned,
                          location_type, entries);
    }
    return mock_pas_status(25u, 20u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_25_21_replyNextAvailableLocationGet(
    void *proxy, uint32_t handle, uint32_t result,
    const uint32_t location_types[MOCK_PAS_VR_LOCATION_TYPE_COUNT])
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t,
                                 const uint32_t *);
    uint32_t owned[MOCK_PAS_VR_LOCATION_TYPE_COUNT];
    Callback callback = (Callback)mock_pas_slot(proxy, 21u);
    uint32_t status = 1u;

    if (callback != NULL && location_types != NULL) {
        memcpy(owned, location_types, sizeof(owned));
        status = callback(proxy, handle, result, owned);
    }
    return mock_pas_status(25u, 21u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_31_15_signalWsaWarningPopup(
    void *proxy, uint32_t alert_id, uint32_t is_manual,
    const void *alert_info_0x2d0)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, const void *);
    uint8_t owned[MOCK_PAS_WSA_ALERT_INFO_SIZE];
    Callback callback = (Callback)mock_pas_slot(proxy, 15u);
    uint32_t status = 1u;

    if (callback != NULL && alert_info_0x2d0 != NULL) {
        memcpy(owned, alert_info_0x2d0, sizeof(owned));
        status = callback(proxy, alert_id, is_manual, owned);
    }
    return mock_pas_status(31u, 15u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_1_13_signalPressStateAnimation(void *proxy,
                                                 uint32_t press_state,
                                                 uint32_t unused_r2,
                                                 uint32_t x,
                                                 uint32_t y)
{
    typedef uint32_t (*SignalPressStateAnimation)(void *, uint32_t, uint32_t,
                                                   uint32_t, uint32_t);
    SignalPressStateAnimation callback =
        (SignalPressStateAnimation)mock_pas_slot(proxy, 13u);
    uint32_t status = callback != NULL
                          ? callback(proxy, press_state, unused_r2, x, y)
                          : 1u;

    return mock_pas_status(1u, 13u, status, MOCK_CALLBACK_CRITICAL);
}

uint32_t mock_pas_7_6(void *proxy, const void *timezone_0x22c)
{
    typedef uint32_t (*Callback)(void *, const void *);
    uint8_t owned[MOCK_PAS_TIMEZONE_SIZE];
    Callback callback = (Callback)mock_pas_slot(proxy, 6u);
    uint32_t status = 1u;

    if (callback != NULL && timezone_0x22c != NULL) {
        memcpy(owned, timezone_0x22c, sizeof(owned));
        status = callback(proxy, owned);
    }
    return mock_pas_status(7u, 6u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_12_5(void *proxy, int32_t handle, const void *version4)
{
    typedef uint32_t (*Callback)(void *, int32_t, const void *);
    uint8_t owned[MOCK_PAS_SOFTWARE_VERSION_SIZE];
    Callback callback = (Callback)mock_pas_slot(proxy, 5u);
    uint32_t status = 1u;

    if (callback != NULL && version4 != NULL) {
        memcpy(owned, version4, sizeof(owned));
        status = callback(proxy, handle, owned);
    }
    return mock_pas_status(12u, 5u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_12_6(void *proxy, int32_t handle, uint16_t supplier)
{
    typedef uint32_t (*Callback)(void *, int32_t, uint16_t);
    Callback callback = (Callback)mock_pas_slot(proxy, 6u);
    uint32_t status = callback != NULL ? callback(proxy, handle, supplier) : 1u;

    return mock_pas_status(12u, 6u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_13_13(void *proxy, int32_t handle, uint32_t packed_status)
{
    typedef uint32_t (*Callback)(void *, int32_t, uint32_t);
    Callback callback = (Callback)mock_pas_slot(proxy, 13u);
    uint32_t status = callback != NULL
                          ? callback(proxy, handle, packed_status)
                          : 1u;

    return mock_pas_status(13u, 13u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_13_11(void *proxy, int32_t handle, const wchar_t *text)
{
    typedef uint32_t (*Callback)(void *, int32_t, const wchar_t *);
    wchar_t owned[MOCK_PAS_DIAG_WIDE_TEXT_MAX + 1u];
    Callback callback = (Callback)mock_pas_slot(proxy, 11u);
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
    return mock_pas_status(13u, 11u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_17_3(void *proxy, const void *display_data_0xa5c)
{
    typedef uint32_t (*Callback)(void *, const void *);
    uint8_t owned[MOCK_CLUSTER_RECORD_SIZE];
    Callback callback = (Callback)mock_pas_slot(proxy, 3u);
    uint32_t status = 1u;

    if (callback != NULL && display_data_0xa5c != NULL) {
        memcpy(owned, display_data_0xa5c, sizeof(owned));
        status = callback(proxy, owned);
    }
    return mock_pas_status(17u, 3u, status, MOCK_CALLBACK_CRITICAL);
}

static uint32_t mock_pas_data_exchange(void *proxy, uint32_t slot,
                                       uint16_t count, const uint32_t *types)
{
    typedef uint32_t (*Callback)(void *, uint16_t, const uint32_t *);
    uint32_t owned[MOCK_PAS_DATA_EXCHANGE_MAX_TYPES];
    const uint32_t *argument = NULL;
    Callback callback = (Callback)mock_pas_slot(proxy, slot);
    uint32_t status = 1u;

    if (callback != NULL && count <= MOCK_PAS_DATA_EXCHANGE_MAX_TYPES &&
        (count == 0u || types != NULL)) {
        if (count != 0u) {
            memcpy(owned, types, (size_t)count * sizeof(owned[0]));
            argument = owned;
        }
        status = callback(proxy, count, argument);
    }
    return mock_pas_status(19u, slot, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_19_3(void *proxy, uint16_t count, const uint32_t *types)
{
    return mock_pas_data_exchange(proxy, 3u, count, types);
}

uint32_t mock_pas_19_4(void *proxy, uint16_t count, const uint32_t *types)
{
    return mock_pas_data_exchange(proxy, 4u, count, types);
}

uint32_t mock_pas_root_10(void *proxy, uint32_t thread_id,
                         const wchar_t *thread_name, uint32_t thread_status)
{
    typedef uint32_t (*Callback)(void *, uint32_t, const wchar_t *, uint32_t);
    wchar_t owned[MOCK_THREAD_NAME_LIMIT + 1u];
    Callback callback = (Callback)mock_pas_slot(proxy, 10u);
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
    return mock_pas_status(UINT32_MAX, 10u, status, MOCK_CALLBACK_CRITICAL);
}

uint32_t mock_pas_24_3_requestPlayTTS(void *proxy, uint32_t handle,
                                      uint32_t source, uint32_t text_type,
                                      const char *tts_text)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, uint32_t,
                                 const char *);
    char owned[0x404];
    Callback callback = (Callback)mock_pas_slot(proxy, 3u);
    size_t length = 0u;
    uint32_t status = 1u;

    if (callback != NULL && mock_pas_cstr8_within(tts_text, 0x403u)) {
        while (tts_text[length] != '\0') {
            ++length;
        }
        if (length != 0u) {
            memcpy(owned, tts_text, length + 1u);
            status = callback(proxy, handle, source, text_type, owned);
        }
    }
    return mock_pas_status(24u, 3u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_32_4_signalEWSAreaChange(void *proxy,
                                           const void *old_area_10,
                                           const void *new_area_10)
{
    typedef uint32_t (*Callback)(void *, const void *, const void *);
    uint8_t old_owned[MOCK_PAS_EWS_AREA_SIZE];
    uint8_t new_owned[MOCK_PAS_EWS_AREA_SIZE];
    Callback callback = (Callback)mock_pas_slot(proxy, 4u);
    uint32_t status = 1u;

    if (callback != NULL && old_area_10 != NULL && new_area_10 != NULL) {
        memcpy(old_owned, old_area_10, sizeof(old_owned));
        memcpy(new_owned, new_area_10, sizeof(new_owned));
        status = callback(proxy, old_owned, new_owned);
    }
    return mock_pas_status(32u, 4u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_20_7_requestHTTPMessage(
    void *proxy, uint32_t handle, uint32_t service, const wchar_t *url,
    uint32_t method, uint32_t request_size, const void *request_data,
    uint32_t response_size, const void *response_data)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t,
                                 const wchar_t *, uint32_t, uint32_t,
                                 const void *, uint32_t, const void *);
    Callback callback = (Callback)mock_pas_slot(proxy, 7u);
    uint32_t status = 1u;

    if (callback != NULL && url != NULL &&
        (request_size == 0u || request_data != NULL) &&
        (response_size == 0u || response_data != NULL)) {
        status = callback(proxy, handle, service, url, method, request_size,
                          request_data, response_size, response_data);
    }
    return mock_pas_status(20u, 7u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_root_6_signalPersist(void *proxy, const wchar_t *path)
{
    typedef uint32_t (*Callback)(void *, const wchar_t *);
    Callback callback = (Callback)mock_pas_slot(proxy, 6u);
    uint32_t status = callback != NULL && path != NULL
                          ? callback(proxy, path) : 1u;
    return mock_pas_status(UINT32_MAX, 6u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_21_4_requestHTTPMessage(
    void *proxy, uint32_t handle, const wchar_t *url, uint32_t method,
    uint32_t request_size, const void *request_data,
    uint32_t response_size, const void *response_data)
{
    typedef uint32_t (*Callback)(void *, uint32_t, const wchar_t *, uint32_t,
                                 uint32_t, const void *, uint32_t,
                                 const void *);
    Callback callback = (Callback)mock_pas_slot(proxy, 4u);
    uint32_t status = 1u;

    if (callback != NULL && url != NULL &&
        (request_size == 0u || request_data != NULL) &&
        (response_size == 0u || response_data != NULL)) {
        status = callback(proxy, handle, url, method, request_size,
                          request_data, response_size, response_data);
    }
    return mock_pas_status(21u, 4u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_19_8_replyDataExchangeObjectRequiredByServer(
    void *proxy, uint32_t handle, uint32_t object_id, uint32_t object_type,
    const wchar_t *description, uint64_t size, uint32_t reply_type)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, uint32_t,
                                 const wchar_t *, uint64_t, uint32_t);
    Callback callback = (Callback)mock_pas_slot(proxy, 8u);
    uint32_t status = callback != NULL
                          ? callback(proxy, handle, object_id, object_type,
                                     description, size, reply_type)
                          : 1u;
    return mock_pas_status(19u, 8u, status, MOCK_CALLBACK_OPTIONAL);
}

static uint32_t mock_pas_data_exchange_transfer(
    void *proxy, uint32_t slot, uint32_t handle, uint32_t object_id,
    uint64_t size, const void *data, uint64_t total_size, uint32_t abort,
    uint64_t maximum_size)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, uint64_t,
                                 const void *, uint64_t, uint32_t);
    Callback callback = (Callback)mock_pas_slot(proxy, slot);
    uint32_t status = 1u;

    if (callback != NULL && size <= maximum_size &&
        total_size <= UINT32_MAX && (size == 0u || data != NULL)) {
        status = callback(proxy, handle, object_id, size, data, total_size,
                          abort);
    }
    return mock_pas_status(19u, slot, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_19_9_requestSendDataExchangeObjectRequiredByServer(
    void *proxy, uint32_t handle, uint32_t object_id, uint64_t size,
    const void *data, uint64_t total_size, uint32_t abort)
{
    return mock_pas_data_exchange_transfer(proxy, 9u, handle, object_id,
                                           size, data, total_size, abort,
                                           0xfc0u);
}

uint32_t mock_pas_19_11_dataExchangeTransfer(
    void *proxy, uint32_t handle, uint32_t object_id, uint64_t size,
    const void *data, uint64_t total_size, uint32_t abort)
{
    return mock_pas_data_exchange_transfer(proxy, 11u, handle, object_id,
                                           size, data, total_size, abort,
                                           UINT32_MAX);
}

uint32_t mock_pas_13_10_replyApprovedMapVersions(
    void *proxy, int32_t handle, const wchar_t *versions)
{
    typedef uint32_t (*Callback)(void *, int32_t, const wchar_t *);
    Callback callback = (Callback)mock_pas_slot(proxy, 10u);
    uint32_t status = callback != NULL
                          ? callback(proxy, handle, versions) : 1u;
    return mock_pas_status(13u, 10u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_9_6_replyMapMatchedAddress(
    void *proxy, uint32_t handle, const MockPasMapMatchedAddress *address)
{
    typedef uint32_t (*Callback)(void *, uint32_t,
                                 const MockPasMapMatchedAddress *);
    Callback callback = (Callback)mock_pas_slot(proxy, 6u);
    uint32_t status = callback != NULL && address != NULL
                          ? callback(proxy, handle, address) : 1u;
    return mock_pas_status(9u, 6u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_26_4_makePhoneCall(void *proxy, uint32_t handle,
                                     const wchar_t *phone_number,
                                     const wchar_t *display_text)
{
    typedef uint32_t (*Callback)(void *, uint32_t, const wchar_t *,
                                 const wchar_t *);
    Callback callback = (Callback)mock_pas_slot(proxy, 4u);
    uint32_t status = 1u;

    if (callback != NULL && mock_pas_cstr16_within(phone_number, 0x1ffu) &&
        mock_pas_cstr16_within(display_text, 0x13u) &&
        phone_number[0] != L'\0' && display_text[0] != L'\0') {
        status = callback(proxy, handle, phone_number, display_text);
    }
    return mock_pas_status(26u, 4u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_25_14_replyFileInfo(
    void *proxy, uint32_t handle, uint32_t result,
    const char *language_list, uint32_t file_type, uint32_t entries)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, const char *,
                                 uint32_t, uint32_t);
    Callback callback = (Callback)mock_pas_slot(proxy, 14u);
    uint32_t status = callback != NULL && language_list != NULL
                          ? callback(proxy, handle, result, language_list,
                                     file_type, entries)
                          : 1u;
    return mock_pas_status(25u, 14u, status, MOCK_CALLBACK_OPTIONAL);
}

uint32_t mock_pas_25_35_replyConvertOrthoToPhonetic(
    void *proxy, uint32_t handle, uint32_t result, uint32_t count,
    const uint32_t *location_types, const char *const *strings)
{
    typedef uint32_t (*Callback)(void *, uint32_t, uint32_t, uint32_t,
                                 const uint32_t *, const char *const *);
    Callback callback = (Callback)mock_pas_slot(proxy, 35u);
    uint32_t index;
    uint32_t status = 1u;

    if (callback == NULL || count == 0u ||
        count > MOCK_PAS_VR_LOCATION_TYPE_COUNT || location_types == NULL ||
        strings == NULL) {
        return mock_pas_status(25u, 35u, status, MOCK_CALLBACK_OPTIONAL);
    }
    for (index = 0u; index < count; ++index) {
        if (strings[index] == NULL) {
            return mock_pas_status(25u, 35u, status,
                                   MOCK_CALLBACK_OPTIONAL);
        }
    }
    status = callback(proxy, handle, result, count, location_types, strings);
    return mock_pas_status(25u, 35u, status, MOCK_CALLBACK_OPTIONAL);
}
