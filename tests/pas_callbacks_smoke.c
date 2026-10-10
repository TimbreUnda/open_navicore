#include "mock_pas_callbacks.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

typedef uint32_t (*GenericCallback)(void *self, ...);

typedef struct {
    const GenericCallback *vptr;
} FakeProxy;

static GenericCallback g_vtable[40];
static FakeProxy g_proxy = { g_vtable };
static uint32_t g_calls;
static uint32_t g_argument[8];
static uint32_t g_argument4;
static const void *g_pointer[5];
static uint32_t g_return_status;
static uint8_t *g_mutate_bytes;
static wchar_t *g_mutate_wide;
static uint32_t *g_mutate_words;
static size_t g_expected_copy_size;

static void observe_unlocked(void)
{
    MockCallbackTestState state;

    mock_callback_test_state(&state);
}

static uint32_t callback0(void *self)
{
    assert(self == &g_proxy);
    observe_unlocked();
    ++g_calls;
    return g_return_status;
}

static uint32_t callback1(void *self, uint32_t a)
{
    assert(self == &g_proxy);
    observe_unlocked();
    ++g_calls;
    g_argument[0] = a;
    return g_return_status;
}

static uint32_t callback2(void *self, uint32_t a, uint32_t b)
{
    assert(self == &g_proxy);
    observe_unlocked();
    ++g_calls;
    g_argument[0] = a;
    g_argument[1] = b;
    return g_return_status;
}

static uint32_t callback3(void *self, uint32_t a, uint32_t b, uint32_t c)
{
    assert(self == &g_proxy);
    observe_unlocked();
    ++g_calls;
    g_argument[0] = a;
    g_argument[1] = b;
    g_argument[2] = c;
    return g_return_status;
}

static uint32_t callback4(void *self, uint32_t a, uint32_t b, uint32_t c,
                          uint32_t d)
{
    assert(self == &g_proxy);
    observe_unlocked();
    ++g_calls;
    g_argument[0] = a;
    g_argument[1] = b;
    g_argument[2] = c;
    g_argument[3] = d;
    return g_return_status;
}

static uint32_t callback5(void *self, uint32_t a, uint32_t b, uint32_t c,
                          uint32_t d, uint32_t e)
{
    assert(self == &g_proxy);
    observe_unlocked();
    ++g_calls;
    g_argument[0] = a;
    g_argument[1] = b;
    g_argument[2] = c;
    g_argument[3] = d;
    g_argument[4] = e;
    return g_return_status;
}

static uint32_t callback_press_state_animation(void *self,
                                               uint32_t press_state,
                                               uint32_t unused_r2,
                                               uint32_t x,
                                               uint32_t y)
{
    assert(self == &g_proxy);
    observe_unlocked();
    ++g_calls;
    g_argument[0] = press_state;
    g_argument[1] = unused_r2;
    g_argument[2] = x;
    g_argument[3] = y;
    return press_state == 1u || press_state == 2u ? g_return_status : 2u;
}

static uint32_t callback_write_log_data(void *self,
                                        uint32_t methods,
                                        uint32_t reserved_ignored,
                                        uint32_t encoding,
                                        uint8_t severity,
                                        uint32_t byte_count,
                                        const void *text)
{
    assert(self == &g_proxy);
    observe_unlocked();
    ++g_calls;
    g_argument[0] = methods;
    g_argument[1] = reserved_ignored;
    g_argument[2] = encoding;
    g_argument[3] = (uint32_t)severity;
    g_argument4 = byte_count;
    g_pointer[0] = text;
    return g_return_status;
}

static uint32_t callback_location_get(
    void *self, uint32_t handle_ignored, uint32_t result,
    const uint32_t *location_types, const char *location_ids,
    const char *location_values, const char *orthographic,
    const char *phonetic)
{
    assert(self == &g_proxy);
    observe_unlocked();
    ++g_calls;
    g_argument[0] = handle_ignored;
    g_argument[1] = result;
    g_pointer[0] = location_types;
    g_pointer[1] = location_ids;
    g_pointer[2] = location_values;
    g_pointer[3] = orthographic;
    g_pointer[4] = phonetic;
    return g_return_status;
}

static void prepare(uint32_t slot, GenericCallback callback)
{
    memset(g_vtable, 0, sizeof(g_vtable));
    memset(g_argument, 0, sizeof(g_argument));
    memset(g_pointer, 0, sizeof(g_pointer));
    g_argument4 = 0u;
    g_vtable[slot] = callback;
    g_calls = 0u;
    g_return_status = 0u;
}

#define TEST_0(function, slot)                                                \
    do {                                                                       \
        prepare(slot, (GenericCallback)callback0);                             \
        assert(function(&g_proxy) == 0u);                                     \
        assert(g_calls == 1u);                                                 \
    } while (0)

#define TEST_1(function, slot, a)                                             \
    do {                                                                       \
        prepare(slot, (GenericCallback)callback1);                             \
        assert(function(&g_proxy, a) == 0u);                                  \
        assert(g_calls == 1u && g_argument[0] == (uint32_t)(a));              \
    } while (0)

#define TEST_2(function, slot, a, b)                                          \
    do {                                                                       \
        prepare(slot, (GenericCallback)callback2);                             \
        assert(function(&g_proxy, a, b) == 0u);                               \
        assert(g_calls == 1u && g_argument[0] == (uint32_t)(a));              \
        assert(g_argument[1] == (uint32_t)(b));                               \
    } while (0)

#define TEST_3(function, slot, a, b, c)                                       \
    do {                                                                       \
        prepare(slot, (GenericCallback)callback3);                             \
        assert(function(&g_proxy, a, b, c) == 0u);                            \
        assert(g_calls == 1u && g_argument[0] == (uint32_t)(a));              \
        assert(g_argument[1] == (uint32_t)(b));                               \
        assert(g_argument[2] == (uint32_t)(c));                               \
    } while (0)

#define TEST_4(function, slot, a, b, c, d)                                    \
    do {                                                                       \
        prepare(slot, (GenericCallback)callback4);                             \
        assert(function(&g_proxy, a, b, c, d) == 0u);                         \
        assert(g_calls == 1u && g_argument[0] == (uint32_t)(a));              \
        assert(g_argument[1] == (uint32_t)(b));                               \
        assert(g_argument[2] == (uint32_t)(c));                               \
        assert(g_argument[3] == (uint32_t)(d));                               \
    } while (0)

#define TEST_5(function, slot, a, b, c, d, e)                                 \
    do {                                                                       \
        prepare(slot, (GenericCallback)callback5);                             \
        assert(function(&g_proxy, a, b, c, d, e) == 0u);                      \
        assert(g_calls == 1u && g_argument[0] == (uint32_t)(a));              \
        assert(g_argument[1] == (uint32_t)(b));                               \
        assert(g_argument[2] == (uint32_t)(c));                               \
        assert(g_argument[3] == (uint32_t)(d));                               \
        assert(g_argument[4] == (uint32_t)(e));                               \
    } while (0)

static uint32_t callback_blob(void *self, const void *blob)
{
    const uint8_t *copy = (const uint8_t *)blob;

    assert(self == &g_proxy && blob != NULL && copy != g_mutate_bytes);
    g_mutate_bytes[0] = 0u;
    assert(copy[0] == 0x5au);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_version(void *self, int32_t handle,
                                 const void *version)
{
    const uint8_t *copy = (const uint8_t *)version;

    assert(self == &g_proxy && handle == -7 && copy != g_mutate_bytes);
    g_mutate_bytes[0] = 0u;
    assert(copy[0] == 0x5au && copy[3] == 0x5au);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_supplier(void *self, int32_t handle,
                                  uint16_t supplier)
{
    assert(self == &g_proxy && handle == -8 && supplier == 0xfedcu);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_wide(void *self, int32_t handle,
                              const wchar_t *text)
{
    assert(self == &g_proxy && handle == -9 && text != g_mutate_wide);
    g_mutate_wide[0] = L'X';
    assert(text[0] == L'A' && text[MOCK_PAS_DIAG_WIDE_TEXT_MAX] == L'\0');
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_data(void *self, uint16_t count,
                              const uint32_t *types)
{
    assert(self == &g_proxy);
    if (count == 0u) {
        assert(types == NULL);
    } else {
        assert(count == 3u && types != g_mutate_words);
        g_mutate_words[0] = 0u;
        assert(types[0] == 0x11223344u && types[2] == 0x99aabbccu);
    }
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_thread(void *self, uint32_t id,
                                const wchar_t *name, uint32_t status)
{
    assert(self == &g_proxy && id == 77u && status == 3u);
    assert(name != g_mutate_wide && wcslen(name) == MOCK_THREAD_NAME_LIMIT);
    g_mutate_wide[0] = L'X';
    assert(name[0] == L'N' && name[MOCK_THREAD_NAME_LIMIT] == L'\0');
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_file_get(void *self, uint32_t handle,
                                  uint32_t result, const char *text)
{
    assert(self == &g_proxy && handle == 1u && result == 2u);
    assert(text != (const char *)g_mutate_bytes && text[0] == 'A');
    g_mutate_bytes[0] = 'X';
    assert(text[0] == 'A');
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_location_set(void *self, uint32_t handle,
                                      uint32_t result, uint32_t status,
                                      const char *text, uint32_t type,
                                      uint32_t entries)
{
    assert(self == &g_proxy && handle == 3u && result == 4u && status == 5u);
    assert(type == 6u && entries == 7u && text[0] == 'B');
    assert(text != (const char *)g_mutate_bytes);
    g_mutate_bytes[0] = 'X';
    assert(text[0] == 'B');
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_location_types(void *self, uint32_t handle,
                                        uint32_t result,
                                        const uint32_t *types)
{
    assert(self == &g_proxy && handle == 8u && result == 9u);
    assert(types != g_mutate_words && types[0] == 1u && types[6] == 7u);
    g_mutate_words[0] = 0u;
    assert(types[0] == 1u);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_wsa(void *self, uint32_t alert_id,
                             uint32_t is_manual, const void *record)
{
    const uint8_t *copy = (const uint8_t *)record;

    assert(self == &g_proxy && alert_id == 10u && is_manual == 1u);
    assert(copy != g_mutate_bytes && copy[0] == 0x5au);
    g_mutate_bytes[0] = 0u;
    assert(copy[0] == 0x5au && copy[MOCK_PAS_WSA_ALERT_INFO_SIZE - 1u] == 0x5au);
    ++g_calls;
    return g_return_status;
}

static void assert_owned_copy(const void *value)
{
    const uint8_t *copy = (const uint8_t *)value;

    assert(copy != NULL && copy != g_mutate_bytes);
    g_mutate_bytes[0] = 0u;
    assert(copy[0] == 0x5au);
    assert(copy[g_expected_copy_size - 1u] == 0x5au);
}

static uint32_t callback_owned_blob(void *self, const void *value)
{
    assert(self == &g_proxy);
    assert_owned_copy(value);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_word_owned_blob(void *self, uint32_t word0,
                                         const void *value)
{
    assert(self == &g_proxy);
    g_argument[0] = word0;
    assert_owned_copy(value);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_2word_owned_blob(void *self, uint32_t word0,
                                          uint32_t word1,
                                          const void *value)
{
    assert(self == &g_proxy);
    g_argument[0] = word0;
    g_argument[1] = word1;
    assert_owned_copy(value);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_4word_owned_blob(void *self, uint32_t word0,
                                          uint32_t word1, uint32_t word2,
                                          uint32_t word3,
                                          const void *value)
{
    assert(self == &g_proxy);
    g_argument[0] = word0;
    g_argument[1] = word1;
    g_argument[2] = word2;
    g_argument[3] = word3;
    assert_owned_copy(value);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_opaque(void *self, uint32_t word0, uint32_t word1,
                                const void *opaque)
{
    assert(self == &g_proxy && opaque == g_mutate_bytes);
    g_argument[0] = word0;
    g_argument[1] = word1;
    g_pointer[0] = opaque;
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_tts(void *self, uint32_t handle, uint32_t source,
                             uint32_t text_type, const char *text)
{
    assert(self == &g_proxy && handle == 0x2403u && source == 2u);
    assert(text_type == 3u && text != (const char *)g_mutate_bytes);
    g_mutate_bytes[0] = 'X';
    assert(strcmp(text, "hello") == 0);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_ews_change(void *self, const void *old_area,
                                    const void *new_area)
{
    const uint8_t *old_copy = (const uint8_t *)old_area;
    const uint8_t *new_copy = (const uint8_t *)new_area;

    assert(self == &g_proxy && old_area != g_pointer[0]);
    assert(new_area != g_pointer[1] && old_copy[9] == 0x11u);
    assert(new_copy[9] == 0x22u);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_http20(void *self, uint32_t handle, uint32_t service,
                                const wchar_t *url, uint32_t method,
                                uint32_t request_size, const void *request,
                                uint32_t response_size, const void *response)
{
    assert(self == &g_proxy && handle == 20u && service == 7u);
    assert(url == g_pointer[0] && method == 2u && request_size == 3u);
    assert(request == g_pointer[1] && response_size == 4u);
    assert(response == g_pointer[2]);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_http21(void *self, uint32_t handle,
                                const wchar_t *url, uint32_t method,
                                uint32_t request_size, const void *request,
                                uint32_t response_size, const void *response)
{
    assert(self == &g_proxy && handle == 21u && url == g_pointer[0]);
    assert(method == 4u && request_size == 3u && request == g_pointer[1]);
    assert(response_size == 4u && response == g_pointer[2]);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_wide_borrowed(void *self, int32_t handle,
                                       const wchar_t *text)
{
    assert(self == &g_proxy && handle == -13 && text == g_pointer[0]);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_persist(void *self, const wchar_t *path)
{
    assert(self == &g_proxy && path == g_pointer[0]);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_exchange_reply(void *self, uint32_t handle,
                                        uint32_t object_id,
                                        uint32_t object_type,
                                        const wchar_t *description,
                                        uint64_t size, uint32_t reply_type)
{
    assert(self == &g_proxy && handle == 19u && object_id == 8u);
    assert(object_type == 3u && description == g_pointer[0]);
    assert(size == UINT64_C(0x1122334455667788) && reply_type == 5u);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_exchange_transfer(void *self, uint32_t handle,
                                            uint32_t object_id, uint64_t size,
                                            const void *data,
                                            uint64_t total_size,
                                            uint32_t abort)
{
    assert(self == &g_proxy && handle == 19u && object_id == g_argument[0]);
    assert(size == UINT64_C(0x123) && data == g_pointer[0]);
    assert(total_size == UINT64_C(0x55667788) && abort == 1u);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_map_address(void *self, uint32_t handle,
                                     const MockPasMapMatchedAddress *address)
{
    assert(self == &g_proxy && handle == 9u && address == g_pointer[0]);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_phone(void *self, uint32_t handle,
                               const wchar_t *phone,
                               const wchar_t *display)
{
    assert(self == &g_proxy && handle == 26u);
    assert(phone == g_pointer[0] && display == g_pointer[1]);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_file_info(void *self, uint32_t handle,
                                   uint32_t result, const char *languages,
                                   uint32_t file_type, uint32_t entries)
{
    assert(self == &g_proxy && handle == 25u && result == 14u);
    assert(languages == g_pointer[0] && file_type == 6u && entries == 7u);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_phonetic(void *self, uint32_t handle,
                                  uint32_t result, uint32_t count,
                                  const uint32_t *location_types,
                                  const char *const *strings)
{
    assert(self == &g_proxy && handle == 25u && result == 35u && count == 2u);
    assert(location_types == g_pointer[0] && strings == g_pointer[1]);
    assert(location_types[1] == 9u && strcmp(strings[1], "beta") == 0);
    ++g_calls;
    return g_return_status;
}

static void assert_mbr(MockPasSxmMbr mbr)
{
    assert(mbr.lower_left.longitude == 1.0);
    assert(mbr.lower_left.latitude == 2.0);
    assert(mbr.upper_right.longitude == 3.0);
    assert(mbr.upper_right.latitude == 4.0);
    assert(mbr.center.longitude == 5.0);
    assert(mbr.center.latitude == 6.0);
}

static uint32_t callback_wsa_location(void *self, uint32_t handle,
                                      MockPasSxmLocation location,
                                      uint32_t radius, uint32_t radius_unit)
{
    assert(self == &g_proxy && handle == 31u);
    assert(location.longitude == 11.0 && location.latitude == 22.0);
    assert(radius == 33u && radius_unit == 2u);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_apogee_mbr(void *self, uint32_t handle,
                                    uint16_t type, MockPasSxmMbr mbr,
                                    uint32_t collection_index)
{
    assert(self == &g_proxy && handle == 30u && type == 0x1234u);
    assert_mbr(mbr);
    assert(collection_index == 7u);
    ++g_calls;
    return g_return_status;
}

static uint32_t callback_parking_mbr(void *self, uint32_t handle,
                                     MockPasSxmMbr mbr,
                                     uint16_t collection_index)
{
    assert(self == &g_proxy && handle == 29u);
    assert_mbr(mbr);
    assert(collection_index == 0x4567u);
    ++g_calls;
    return g_return_status;
}

static void check_pending_request_correlation(void)
{
    MockCallbackTestState state;

    mock_callback_backend_reset();
    /* The same handle may occur in different adapters or provider slots. */
    mock_callback_pending_add(1u, 3u, 7u);
    mock_callback_pending_add(1u, 4u, 7u);
    mock_callback_pending_add(2u, 3u, 7u);
    mock_callback_pending_cancel(1u, 3u, 8u);
    mock_callback_pending_cancel(1u, 3u, 7u);
    mock_callback_pending_cancel(1u, 3u, 7u);
    mock_callback_test_state(&state);
    assert(state.pending_count == 2u);
    assert(state.correlation_successes == 0u);
    assert(state.correlation_failures == 0u);

    assert(mock_callback_pending_complete(1u, 3u, 7u) == 0);
    assert(mock_callback_pending_complete(1u, 4u, 7u) == 1);
    assert(mock_callback_pending_complete(2u, 3u, 7u) == 1);
    assert(mock_callback_pending_complete(2u, 3u, 7u) == 0);
    mock_callback_test_state(&state);
    assert(state.pending_count == 0u);
    assert(state.correlation_successes == 2u);
    assert(state.correlation_failures == 2u);
    mock_callback_backend_reset();
}

int main(void)
{
    MockCallbackTestState state;
    uint8_t timezone[MOCK_PAS_TIMEZONE_SIZE];
    uint8_t cluster[MOCK_CLUSTER_RECORD_SIZE];
    uint8_t version[MOCK_PAS_SOFTWARE_VERSION_SIZE];
    uint8_t wsa[MOCK_PAS_WSA_ALERT_INFO_SIZE];
    uint8_t concrete_copy[MOCK_PAS_TIMEZONE_SIZE];
    char vr_text[MOCK_PAS_VR_STRING_SIZE];
    wchar_t wide[MOCK_PAS_DIAG_WIDE_TEXT_MAX + 2u];
    uint32_t types[3] = { 0x11223344u, 0x55667788u, 0x99aabbccu };
    uint32_t location_types[7] = { 1u, 2u, 3u, 4u, 5u, 6u, 0u };
    uint32_t next_types[MOCK_PAS_VR_LOCATION_TYPE_COUNT] = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u
    };
    const char location_ids[] = "id1;id2";
    const char location_values[] = "value1;value2";
    const char orthographic[] = "ortho1;ortho2";
    const char phonetic[] = "phone1;phone2";
    char tts[] = "hello";
    uint8_t old_area[MOCK_PAS_EWS_AREA_SIZE];
    uint8_t new_area[MOCK_PAS_EWS_AREA_SIZE];
    uint8_t request_data[3] = { 1u, 2u, 3u };
    uint8_t response_data[4] = { 4u, 5u, 6u, 7u };
    const wchar_t url[] = L"https://example.invalid";
    const wchar_t path[] = L"/fs/nav/persist";
    const wchar_t description[] = L"object";
    const wchar_t versions[] = L"2026.10";
    const wchar_t phone_number[] = L"+123456789";
    const wchar_t display_text[] = L"Test";
    const char languages[] = "en-US;uk-UA";
    uint8_t transfer_data[4] = { 8u, 9u, 10u, 11u };
    uint32_t phonetic_types[2] = { 8u, 9u };
    const char *phonetic_strings[2] = { "alpha", "beta" };
    MockPasMapMatchedAddress address = { 0 };
    MockPasSxmLocation wsa_location = { 11.0, 22.0 };
    MockPasSxmMbr mbr = {
        { 1.0, 2.0 }, { 3.0, 4.0 }, { 5.0, 6.0 }
    };

    mock_callback_backend_reset();

    check_pending_request_correlation();

    /* QNX ARM by-value aggregate callback declarations. */
    prepare(12u, (GenericCallback)callback_wsa_location);
    assert(mock_pas_31_12_requestWsaSetAlertLocation(
               &g_proxy, 31u, wsa_location, 33u, 2u) == 0u);
    assert(g_calls == 1u);

    prepare(4u, (GenericCallback)callback_apogee_mbr);
    assert(mock_pas_30_4_requestApogeeSetTypeCollectionMBR(
               &g_proxy, 30u, 0x1234u, mbr, 7u) == 0u);
    assert(g_calls == 1u);
    prepare(5u, (GenericCallback)callback_apogee_mbr);
    assert(mock_pas_30_5_requestApogeeSetTypeExtractionMBR(
               &g_proxy, 30u, 0x1234u, mbr, 7u) == 0u);
    assert(g_calls == 1u);

    prepare(4u, (GenericCallback)callback_parking_mbr);
    assert(mock_pas_29_4_requestParkingSetCollectionMBR(
               &g_proxy, 29u, mbr, 0x4567u) == 0u);
    assert(g_calls == 1u);

    /* Exercise every newly recovered pointer-bearing ABI with typed callees. */
    g_mutate_bytes = (uint8_t *)tts;
    prepare(3u, (GenericCallback)callback_tts);
    assert(mock_pas_24_3_requestPlayTTS(&g_proxy, 0x2403u, 2u, 3u, tts) == 0u);
    assert(g_calls == 1u && tts[0] == 'X');
    prepare(3u, (GenericCallback)callback_tts);
    assert(mock_pas_24_3_requestPlayTTS(&g_proxy, 1u, 2u, 3u, "") == 1u);
    assert(g_calls == 0u);

    memset(old_area, 0x11, sizeof(old_area));
    memset(new_area, 0x22, sizeof(new_area));
    prepare(4u, (GenericCallback)callback_ews_change);
    g_pointer[0] = old_area;
    g_pointer[1] = new_area;
    assert(mock_pas_32_4_signalEWSAreaChange(&g_proxy, old_area, new_area) == 0u);
    assert(g_calls == 1u);

    prepare(7u, (GenericCallback)callback_http20);
    g_pointer[0] = url;
    g_pointer[1] = request_data;
    g_pointer[2] = response_data;
    assert(mock_pas_20_7_requestHTTPMessage(
               &g_proxy, 20u, 7u, url, 2u, 3u, request_data,
               4u, response_data) == 0u);
    assert(g_calls == 1u);
    prepare(7u, (GenericCallback)callback_http20);
    assert(mock_pas_20_7_requestHTTPMessage(
               &g_proxy, 20u, 7u, url, 2u, 1u, NULL,
               0u, NULL) == 1u && g_calls == 0u);

    prepare(6u, (GenericCallback)callback_persist);
    g_pointer[0] = path;
    assert(mock_pas_root_6_signalPersist(&g_proxy, path) == 0u && g_calls == 1u);

    prepare(4u, (GenericCallback)callback_http21);
    g_pointer[0] = url;
    g_pointer[1] = request_data;
    g_pointer[2] = response_data;
    assert(mock_pas_21_4_requestHTTPMessage(
               &g_proxy, 21u, url, 4u, 3u, request_data,
               4u, response_data) == 0u && g_calls == 1u);

    prepare(8u, (GenericCallback)callback_exchange_reply);
    g_pointer[0] = description;
    assert(mock_pas_19_8_replyDataExchangeObjectRequiredByServer(
               &g_proxy, 19u, 8u, 3u, description,
               UINT64_C(0x1122334455667788), 5u) == 0u);
    assert(g_calls == 1u);

    prepare(9u, (GenericCallback)callback_exchange_transfer);
    g_argument[0] = 9u;
    g_pointer[0] = transfer_data;
    assert(mock_pas_19_9_requestSendDataExchangeObjectRequiredByServer(
               &g_proxy, 19u, 9u, UINT64_C(0x123), transfer_data,
               UINT64_C(0x55667788), 1u) == 0u && g_calls == 1u);
    prepare(9u, (GenericCallback)callback_exchange_transfer);
    assert(mock_pas_19_9_requestSendDataExchangeObjectRequiredByServer(
               &g_proxy, 19u, 9u, UINT64_C(0xfc1), transfer_data,
               UINT64_C(0x55667788), 1u) == 1u && g_calls == 0u);

    prepare(10u, (GenericCallback)callback_wide_borrowed);
    g_pointer[0] = versions;
    assert(mock_pas_13_10_replyApprovedMapVersions(
               &g_proxy, -13, versions) == 0u && g_calls == 1u);

    prepare(6u, (GenericCallback)callback_map_address);
    g_pointer[0] = &address;
    assert(mock_pas_9_6_replyMapMatchedAddress(
               &g_proxy, 9u, &address) == 0u && g_calls == 1u);

    prepare(4u, (GenericCallback)callback_phone);
    g_pointer[0] = phone_number;
    g_pointer[1] = display_text;
    assert(mock_pas_26_4_makePhoneCall(
               &g_proxy, 26u, phone_number, display_text) == 0u);
    assert(g_calls == 1u);
    prepare(4u, (GenericCallback)callback_phone);
    assert(mock_pas_26_4_makePhoneCall(
               &g_proxy, 26u, L"", display_text) == 1u && g_calls == 0u);

    prepare(11u, (GenericCallback)callback_exchange_transfer);
    g_argument[0] = 11u;
    g_pointer[0] = transfer_data;
    assert(mock_pas_19_11_dataExchangeTransfer(
               &g_proxy, 19u, 11u, UINT64_C(0x123), transfer_data,
               UINT64_C(0x55667788), 1u) == 0u && g_calls == 1u);

    prepare(14u, (GenericCallback)callback_file_info);
    g_pointer[0] = languages;
    assert(mock_pas_25_14_replyFileInfo(
               &g_proxy, 25u, 14u, languages, 6u, 7u) == 0u);
    assert(g_calls == 1u);

    prepare(35u, (GenericCallback)callback_phonetic);
    g_pointer[0] = phonetic_types;
    g_pointer[1] = phonetic_strings;
    assert(mock_pas_25_35_replyConvertOrthoToPhonetic(
               &g_proxy, 25u, 35u, 2u, phonetic_types,
               phonetic_strings) == 0u && g_calls == 1u);
    prepare(35u, (GenericCallback)callback_phonetic);
    assert(mock_pas_25_35_replyConvertOrthoToPhonetic(
               &g_proxy, 25u, 35u, 8u, phonetic_types,
               phonetic_strings) == 1u && g_calls == 0u);

    TEST_0(mock_pas_root_12, 12u);
    TEST_0(mock_pas_root_3, 3u);
    TEST_1(mock_pas_root_5_signalInitiateRestartOnce, 5u, 0x5001u);
    prepare(2u, (GenericCallback)callback0);
    assert(mock_pas_all_2(&g_proxy, 31u) == 0u && g_calls == 1u);
    TEST_0(mock_pas_1_2, 2u);
    TEST_1(mock_pas_1_3, 3u, UINT32_MAX);
    TEST_1(mock_pas_1_4_requestCreateRequestCode, 4u, 0x104u);
    TEST_1(mock_pas_1_5_requestCreateFingerPrint, 5u, 0x105u);
    TEST_2(mock_pas_1_6, 6u, 1u, 2u);
    TEST_3(mock_pas_1_7, 7u, 3u, 4u, 5u);
    TEST_1(mock_pas_1_9, 9u, 6u);
    TEST_3(mock_pas_1_10, 10u, 7u, 8u, 9u);
    TEST_1(mock_pas_1_12, 12u, 10u);
    prepare(13u, (GenericCallback)callback_press_state_animation);
    assert(mock_pas_1_13_signalPressStateAnimation(
               &g_proxy, 1u, 0x7b275100u, 60u, 400u) == 0u);
    assert(g_calls == 1u && g_argument[0] == 1u);
    assert(g_argument[1] == 0x7b275100u);
    assert(g_argument[2] == 60u && g_argument[3] == 400u);
    prepare(13u, (GenericCallback)callback_press_state_animation);
    assert(mock_pas_1_13_signalPressStateAnimation(
               &g_proxy, 2u, 0xdeadbeefu, UINT32_MAX, 0u) == 0u);
    assert(g_calls == 1u && g_argument[0] == 2u);
    assert(g_argument[1] == 0xdeadbeefu);
    assert(g_argument[2] == UINT32_MAX && g_argument[3] == 0u);
    prepare(13u, (GenericCallback)callback_press_state_animation);
    assert(mock_pas_1_13_signalPressStateAnimation(
               &g_proxy, 3u, 0x12345678u, 390u, 777u) == 2u);
    assert(g_calls == 1u && g_argument[0] == 3u);
    assert(g_argument[1] == 0x12345678u);
    assert(g_argument[2] == 390u && g_argument[3] == 777u);
    TEST_1(mock_pas_1_14, 14u, 2u);
    TEST_2(mock_pas_1_8_signalHMIStateChange, 8u, 1u, 2u);
    TEST_4(mock_pas_1_11_signalScreenChange, 11u, 1u, 2u, 3u, 4u);
    TEST_1(mock_pas_4_3, 3u, 0x43u);
    TEST_1(mock_pas_6_3, 3u, 0x63u);
    prepare(4u, (GenericCallback)callback_write_log_data);
    g_return_status = 7u;
    assert(mock_pas_6_4_signalWriteLogData(
               &g_proxy, 0x11u, 0x22334455u, 3u, 0xfeu,
               0x66778899u, location_ids) == 7u);
    assert(g_calls == 1u && g_argument[0] == 0x11u);
    assert(g_argument[1] == 0x22334455u && g_argument[2] == 3u);
    assert(g_argument[3] == 0xfeu && g_argument4 == 0x66778899u);
    assert(g_pointer[0] == location_ids);
    prepare(4u, (GenericCallback)callback_write_log_data);
    g_return_status = 9u;
    assert(mock_pas_6_4_signalWriteLogData(
               &g_proxy, 0u, UINT32_MAX, 2u, 0u, 0u, NULL) == 9u);
    assert(g_calls == 1u && g_argument[1] == UINT32_MAX);
    assert(g_pointer[0] == NULL);
    TEST_1(mock_pas_7_3, 3u, 0x73u);
    TEST_1(mock_pas_7_4, 4u, 0x74u);
    TEST_1(mock_pas_8_3, 3u, UINT32_MAX);
    TEST_1(mock_pas_8_5, 5u, 0x85u);
    TEST_1(mock_pas_8_6, 6u, 0x86u);
    TEST_1(mock_pas_8_11, 11u, 0x811u);
    TEST_1(mock_pas_9_3, 3u, UINT32_MAX);
    TEST_2(mock_pas_10_3, 3u, 1u, 256u);
    TEST_2(mock_pas_10_4_requestAudioOutSourceDescriptor, 4u, 0x104u, 1u);
    TEST_2(mock_pas_10_5_requestAudioInSourceDescriptor, 5u, 0x105u, 2u);
    TEST_2(mock_pas_10_6_requestAudioSourceAllocation, 6u, 0x106u, 3u);
    TEST_2(mock_pas_10_7_requestAudioSourcePreparation, 7u, 0x107u, 4u);
    TEST_1(mock_pas_10_8_signalDeallocateAudioSource, 8u, 5u);
    TEST_3(mock_pas_10_9_replyAudioSourceDeactivation,
           9u, 0x109u, 6u, 0u);
    TEST_1(mock_pas_10_10, 10u, 4u);
    TEST_2(mock_pas_10_11_requestAudioSourceStatus, 11u, 0x10bu, 7u);
    TEST_1(mock_pas_10_12_signalEnableServerAudioSource, 12u, 8u);
    TEST_1(mock_pas_10_13_signalDisableServerAudioSource, 13u, 9u);
    TEST_2(mock_pas_10_14_requestAudioSourceEnableStatus,
           14u, 0x10eu, 10u);
    TEST_2(mock_pas_10_15_signalAudioSourceComfortVolume, 15u, 11u, 2u);
    TEST_1(mock_pas_10_16_signalEnableAudioSourceVolumeSeparation, 16u, 12u);
    TEST_1(mock_pas_10_17_signalDisableAudioSourceVolumeSeparation, 17u, 13u);
    TEST_2(mock_pas_10_18_requestAudioSourceVolumeSeparationStatus,
           18u, 0x112u, 14u);
    TEST_2(mock_pas_10_19, 19u, 5u, 1u);
    TEST_3(mock_pas_10_20_requestAudioSourceVolumeChange,
           20u, 5u, 1u, 22u);
    TEST_1(mock_pas_12_8, 8u, 12u);
    TEST_1(mock_pas_13_3, 3u, UINT32_MAX);
    TEST_1(mock_pas_13_4, 4u, 1u);
    TEST_1(mock_pas_13_5, 5u, 2u);
    TEST_1(mock_pas_13_6, 6u, 3u);
    TEST_1(mock_pas_13_7, 7u, 4u);
    TEST_1(mock_pas_13_8, 8u, 5u);
    TEST_1(mock_pas_13_9, 9u, 6u);
    TEST_2(mock_pas_13_13, 13u, (int32_t)-10, 0xabcdefu);
    TEST_1(mock_pas_17_4_raw, 4u, 0x174u);
    TEST_1(mock_pas_17_5, 5u, UINT32_MAX);
    TEST_0(mock_pas_17_6, 6u);
    TEST_1(mock_pas_17_7, 7u, 19u);
    TEST_1(mock_pas_19_5, 5u, 17u);
    TEST_3(mock_pas_19_6_raw, 6u, 0x1960u, 0x1961u, 0x1962u);
    TEST_3(mock_pas_19_7_requestSendDataExchangeObjectProducedByServer,
           7u, 1u, 2u, 3u);
    TEST_1(mock_pas_20_3, 3u, 1u);
    TEST_2(mock_pas_20_4, 4u, 16u, 6u);
    TEST_2(mock_pas_20_8, 8u, 14u, 6u);
    TEST_2(mock_pas_20_9, 9u, 15u, 6u);
    TEST_1(mock_pas_20_6_releaseConnectionForService, 6u, 6u);
    TEST_0(mock_pas_25_3, 3u);
    TEST_0(mock_pas_25_4, 4u);
    TEST_3(mock_pas_25_7_replyAlphabetGet, 7u, 1u, 2u, 3u);
    TEST_2(mock_pas_25_8_replyDestinationAdbEntrySet, 8u, 1u, 2u);
    TEST_2(mock_pas_25_9_replyDestinationHomeSet, 9u, 1u, 2u);
    TEST_2(mock_pas_25_10_replyDestinationWorkSet, 10u, 1u, 2u);
    TEST_3(mock_pas_25_11_replyDestinationStatus, 11u, 1u, 2u, 3u);
    TEST_3(mock_pas_25_12_replyFileFromDBIDGet, 12u, 1u, 2u, 3u);
    TEST_2(mock_pas_25_15_replyGuidanceInstructionsRepeat, 15u, 1u, 2u);
    TEST_2(mock_pas_25_16_replyGuidanceStop, 16u, 1u, 2u);
    TEST_2(mock_pas_25_17_replyGuidanceStart, 17u, 1u, 2u);
    TEST_3(mock_pas_25_18_replyLocationCheck, 18u, 1u, 2u, 3u);
    prepare(19u, (GenericCallback)callback_location_get);
    g_return_status = 11u;
    assert(mock_pas_25_19_replyLocationGet(
               &g_proxy, 0x19191919u, 2u, location_types, location_ids,
               location_values, orthographic, phonetic) == 11u);
    assert(g_calls == 1u && g_argument[0] == 0x19191919u);
    assert(g_argument[1] == 2u && g_pointer[0] == location_types);
    assert(g_pointer[1] == location_ids && g_pointer[2] == location_values);
    assert(g_pointer[3] == orthographic && g_pointer[4] == phonetic);
    prepare(19u, (GenericCallback)callback_location_get);
    g_return_status = 12u;
    assert(mock_pas_25_19_replyLocationGet(
               &g_proxy, UINT32_MAX, 0u, NULL, NULL, NULL, NULL, NULL) == 12u);
    assert(g_calls == 1u && g_argument[0] == UINT32_MAX);
    assert(g_argument[1] == 0u && g_pointer[0] == NULL);
    assert(g_pointer[1] == NULL && g_pointer[2] == NULL);
    assert(g_pointer[3] == NULL && g_pointer[4] == NULL);
    TEST_3(mock_pas_25_23, 23u, 1u, 2u, 3u);
    TEST_3(mock_pas_25_24, 24u, 4u, 5u, 6u);
    TEST_2(mock_pas_25_25, 25u, 7u, 8u);
    TEST_4(mock_pas_25_22_replyPOISet, 22u, 1u, 2u, 3u, 4u);
    TEST_2(mock_pas_25_26_replyChangeRouteGuidance, 26u, 1u, 2u);
    TEST_2(mock_pas_25_27_replyChangeMapDirection, 27u, 1u, 2u);
    TEST_2(mock_pas_25_28_replyChangeMapDimensionalView, 28u, 1u, 2u);
    TEST_2(mock_pas_25_29_replyZoomIn, 29u, 1u, 2u);
    TEST_2(mock_pas_25_30_replyZoomOut, 30u, 1u, 2u);
    TEST_3(mock_pas_25_34_replySetDestination, 34u, 1u, 2u, 3u);
    TEST_1(mock_pas_26_3, 3u, 1u);
    TEST_1(mock_pas_26_5, 5u, 13u);
    TEST_1(mock_pas_24_4_internalDeinitialize, 4u, 0x2404u);
    TEST_1(mock_pas_29_3, 3u, UINT32_MAX);
    TEST_1(mock_pas_30_3, 3u, UINT32_MAX);
    TEST_1(mock_pas_31_3, 3u, UINT32_MAX);
    TEST_1(mock_pas_31_6_raw, 6u, 0x316u);
    TEST_5(mock_pas_31_10_raw, 10u, 1u, 2u, 3u, 4u, 5u);
    TEST_3(mock_pas_31_13_raw, 13u, 0x311u, 0x312u, 0x313u);
    TEST_2(mock_pas_31_8_replyGraphicalWeatherMapMode, 8u, 1u, 2u);
    TEST_2(mock_pas_root_9_requestUserStorage, 9u, 1u, 2u);

    /* Every newly recovered fixed-scalar dormant seam is independently callable. */
    TEST_1(mock_pas_20_5_signalConnectionNeeded, 5u, 1u);
    TEST_0(mock_pas_25_5_signalMyDestDeletedEvent, 5u);
    TEST_1(mock_pas_25_6_signalGUISelection, 6u, 2u);
    TEST_2(mock_pas_25_31_replyRouteGuidanceStatus, 31u, 3u, 4u);
    TEST_0(mock_pas_25_32_signalTerminateVR, 32u);
    TEST_0(mock_pas_25_33_signalInitiateVR, 33u);
    TEST_1(mock_pas_31_4_replyEnterGraphicalWeatherMap, 4u, 5u);
    TEST_1(mock_pas_31_5_replyExitGraphicalWeatherMap, 5u, 6u);
    TEST_2(mock_pas_31_7_requestAgwPanningMode, 7u, 7u, 8u);
    TEST_2(mock_pas_31_9_requestAgwSetFilter, 9u, 9u, 10u);
    TEST_3(mock_pas_31_11_requestAgwSetRadiusOfInterest,
           11u, 11u, 12u, 13u);
    TEST_0(mock_pas_31_14_signalExitGraphicalWeatherMap, 14u);
    TEST_1(mock_pas_14_3_subscribe, 3u, 14u);
    TEST_1(mock_pas_14_4_requestStatus, 4u, 15u);
    TEST_1(mock_pas_14_5_requestSelectionFilterCapabilities, 5u, 16u);
    TEST_1(mock_pas_14_6_requestSelectableFilterList, 6u, 17u);
    TEST_0(mock_pas_27_2_adapterReady, 2u);
    TEST_1(mock_pas_27_3_subscribe, 3u, 18u);
    TEST_1(mock_pas_27_4_requestStationList, 4u, 19u);
    TEST_1(mock_pas_9_4_requestGPSPosition, 4u, 20u);
    TEST_1(mock_pas_root_2_subscribe, 2u, 21u);
    TEST_0(mock_pas_root_4_signalDisableWatchdog, 4u);
    TEST_1(mock_pas_root_7_replyShutdown, 7u, 22u);
    TEST_1(mock_pas_root_8_requestPowerStatus, 8u, 23u);
    TEST_1(mock_pas_12_3_subscribeDiagnosisDriverDistractionStatus,
           3u, 24u);
    TEST_1(mock_pas_12_4_requestDiagnosisDriverDistractionStatus,
           4u, 25u);
    TEST_2(mock_pas_12_7_signalErrorCode, 7u, 26u, 27u);
    TEST_1(mock_pas_23_3_subscribe, 3u, 28u);
    TEST_0(mock_pas_21_2_adapterReady, 2u);
    TEST_1(mock_pas_21_3_subscribe, 3u, 29u);
    TEST_0(mock_pas_21_5_signalStartAuthentication, 5u);
    TEST_0(mock_pas_28_2_adapterReady, 2u);
    TEST_0(mock_pas_28_3_subscribe, 3u);
    TEST_2(mock_pas_28_4_requestTuneToFrequency, 4u, 30u, 31u);
    TEST_4(mock_pas_19_10_requestOfferDataExchangeObjectProducedByClient,
           10u, 32u, 33u, 34u, 35u);
    TEST_2(mock_pas_8_4_subscribeCANMessage, 4u, 36u, 37u);
    TEST_1(mock_pas_8_7_requestOdometer, 7u, 38u);
    TEST_1(mock_pas_8_8_requestVehicleSpeedDisplayedOnICD, 8u, 39u);
    TEST_2(mock_pas_8_9_requestCANMessageSynchronization, 9u, 40u, 41u);
    TEST_1(mock_pas_8_12_requestIgnitionStatus, 12u, 42u);
    TEST_2(mock_pas_30_6_requestApogeeRemoveCollectionMBR, 6u, 43u, 44u);
    TEST_2(mock_pas_13_12_replyStatusNavigationDatabase, 12u, 45u, 46u);

    /* Bounded pointer seams copy before dispatch; callbacks mutate the caller. */
    memset(concrete_copy, 0x5a, sizeof(concrete_copy));
    g_mutate_bytes = concrete_copy;
    g_expected_copy_size = MOCK_PAS_EWS_AREA_SIZE;
    prepare(3u, (GenericCallback)callback_word_owned_blob);
    assert(mock_pas_32_3_replyEWSArea(&g_proxy, 47u, concrete_copy) == 0u);
    assert(g_calls == 1u && g_argument[0] == 47u);

    memset(concrete_copy, 0x5a, sizeof(concrete_copy));
    g_mutate_bytes = concrete_copy;
    g_expected_copy_size = MOCK_PAS_RDS_FILTER_RECORD_SIZE *
                           MOCK_PAS_RDS_FILTER_MAX_RECORDS;
    prepare(7u, (GenericCallback)callback_word_owned_blob);
    assert(mock_pas_14_7_signalSelectionFilter(&g_proxy, 25u,
                                                concrete_copy) == 0u);
    assert(g_calls == 1u && g_argument[0] == 25u);

    memset(concrete_copy, 0x5a, sizeof(concrete_copy));
    g_mutate_bytes = concrete_copy;
    g_expected_copy_size = MOCK_PAS_TPEG_STATION_SIZE;
    prepare(5u, (GenericCallback)callback_owned_blob);
    assert(mock_pas_27_5_signalSelectStation(&g_proxy, concrete_copy) == 0u);
    assert(g_calls == 1u);

    memset(concrete_copy, 0x5a, sizeof(concrete_copy));
    g_mutate_bytes = concrete_copy;
    g_expected_copy_size = MOCK_PAS_TPEG_STATION_SIZE *
                           MOCK_PAS_TPEG_STATION_MAX_RECORDS;
    prepare(6u, (GenericCallback)callback_word_owned_blob);
    assert(mock_pas_27_6_signalSelectStationList(
               &g_proxy, MOCK_PAS_TPEG_STATION_MAX_RECORDS,
               concrete_copy) == 0u);
    assert(g_calls == 1u &&
           g_argument[0] == MOCK_PAS_TPEG_STATION_MAX_RECORDS);
    prepare(6u, (GenericCallback)callback_word_owned_blob);
    assert(mock_pas_27_6_signalSelectStationList(
               &g_proxy, MOCK_PAS_TPEG_STATION_MAX_RECORDS + 1u,
               concrete_copy) == 1u);
    assert(g_calls == 0u);

    memset(concrete_copy, 0x5a, sizeof(concrete_copy));
    g_mutate_bytes = concrete_copy;
    g_expected_copy_size = MOCK_PAS_MAP_MATCHED_POSITION_SIZE;
    prepare(5u, (GenericCallback)callback_owned_blob);
    assert(mock_pas_9_5_signalMapMatchedPosition(
               &g_proxy, concrete_copy) == 0u && g_calls == 1u);

    memset(concrete_copy, 0x5a, sizeof(concrete_copy));
    g_mutate_bytes = concrete_copy;
    prepare(11u, (GenericCallback)callback_opaque);
    assert(mock_pas_root_11_supplierSpecificServerFunction(
               &g_proxy, 48u, 49u, concrete_copy) == 0u);
    assert(g_calls == 1u && g_pointer[0] == concrete_copy);
    assert(g_argument[0] == 48u && g_argument[1] == 49u);

    memset(concrete_copy, 0x5a, sizeof(concrete_copy));
    g_mutate_bytes = concrete_copy;
    g_expected_copy_size = MOCK_PAS_VICS_WARNING_SIZE;
    prepare(5u, (GenericCallback)callback_4word_owned_blob);
    assert(mock_pas_28_5_signalVICSWideEmergencyWarning(
               &g_proxy, 50u, 51u, 52u, 53u, concrete_copy) == 0u);
    assert(g_calls == 1u && g_argument[0] == 50u && g_argument[3] == 53u);

    memset(concrete_copy, 0x5a, sizeof(concrete_copy));
    g_mutate_bytes = concrete_copy;
    g_expected_copy_size = MOCK_PAS_CAN_MESSAGE_SIZE;
    prepare(10u, (GenericCallback)callback_2word_owned_blob);
    assert(mock_pas_8_10_signalCANMessage(
               &g_proxy, 54u, 55u, concrete_copy) == 0u);
    assert(g_calls == 1u && g_argument[0] == 54u && g_argument[1] == 55u);

    memset(concrete_copy, 0x5a, sizeof(concrete_copy));
    g_mutate_bytes = concrete_copy;
    g_expected_copy_size = MOCK_PAS_TIMEZONE_SIZE;
    prepare(5u, (GenericCallback)callback_word_owned_blob);
    assert(mock_pas_7_5_replyTimeZoneInformation(
               &g_proxy, 56u, concrete_copy) == 0u);
    assert(g_calls == 1u && g_argument[0] == 56u);

    memset(vr_text, 'A', sizeof(vr_text));
    vr_text[sizeof(vr_text) - 1u] = '\0';
    g_mutate_bytes = (uint8_t *)vr_text;
    prepare(13u, (GenericCallback)callback_file_get);
    assert(mock_pas_25_13_replyFileGet(&g_proxy, 1u, 2u, vr_text) == 0u);
    assert(g_calls == 1u);
    prepare(13u, (GenericCallback)callback_file_get);
    assert(mock_pas_25_13_replyFileGet(&g_proxy, 1u, 2u, NULL) == 1u);
    assert(g_calls == 0u);

    memset(vr_text, 'B', sizeof(vr_text));
    vr_text[sizeof(vr_text) - 1u] = '\0';
    g_mutate_bytes = (uint8_t *)vr_text;
    prepare(20u, (GenericCallback)callback_location_set);
    assert(mock_pas_25_20_replyLocationSet(
               &g_proxy, 3u, 4u, 5u, vr_text, 6u, 7u) == 0u);
    assert(g_calls == 1u);

    g_mutate_words = next_types;
    prepare(21u, (GenericCallback)callback_location_types);
    assert(mock_pas_25_21_replyNextAvailableLocationGet(
               &g_proxy, 8u, 9u, next_types) == 0u);
    assert(g_calls == 1u);

    memset(wsa, 0x5a, sizeof(wsa));
    g_mutate_bytes = wsa;
    prepare(15u, (GenericCallback)callback_wsa);
    assert(mock_pas_31_15_signalWsaWarningPopup(
               &g_proxy, 10u, 1u, wsa) == 0u);
    assert(g_calls == 1u);

    memset(timezone, 0x5a, sizeof(timezone));
    g_mutate_bytes = timezone;
    prepare(6u, (GenericCallback)callback_blob);
    assert(mock_pas_7_6(&g_proxy, timezone) == 0u && g_calls == 1u);
    prepare(6u, (GenericCallback)callback_blob);
    assert(mock_pas_7_6(&g_proxy, NULL) == 1u && g_calls == 0u);

    memset(version, 0x5a, sizeof(version));
    g_mutate_bytes = version;
    prepare(5u, (GenericCallback)callback_version);
    assert(mock_pas_12_5(&g_proxy, -7, version) == 0u && g_calls == 1u);
    prepare(5u, (GenericCallback)callback_version);
    assert(mock_pas_12_5(&g_proxy, -7, NULL) == 1u && g_calls == 0u);
    prepare(6u, (GenericCallback)callback_supplier);
    assert(mock_pas_12_6(&g_proxy, -8, 0xfedcu) == 0u && g_calls == 1u);

    wmemset(wide, L'A', MOCK_PAS_DIAG_WIDE_TEXT_MAX + 1u);
    wide[MOCK_PAS_DIAG_WIDE_TEXT_MAX + 1u] = L'\0';
    g_mutate_wide = wide;
    prepare(11u, (GenericCallback)callback_wide);
    assert(mock_pas_13_11(&g_proxy, -9, wide) == 0u && g_calls == 1u);
    prepare(11u, (GenericCallback)callback_wide);
    assert(mock_pas_13_11(&g_proxy, -9, NULL) == 1u && g_calls == 0u);

    memset(cluster, 0x5a, sizeof(cluster));
    g_mutate_bytes = cluster;
    prepare(3u, (GenericCallback)callback_blob);
    assert(mock_pas_17_3(&g_proxy, cluster) == 0u && g_calls == 1u);
    prepare(3u, (GenericCallback)callback_blob);
    assert(mock_pas_17_3(&g_proxy, NULL) == 1u && g_calls == 0u);

    g_mutate_words = types;
    prepare(3u, (GenericCallback)callback_data);
    assert(mock_pas_19_3(&g_proxy, 0u, NULL) == 0u && g_calls == 1u);
    prepare(4u, (GenericCallback)callback_data);
    assert(mock_pas_19_4(&g_proxy, 3u, types) == 0u && g_calls == 1u);
    prepare(4u, (GenericCallback)callback_data);
    assert(mock_pas_19_4(&g_proxy, 65u, types) == 1u && g_calls == 0u);
    prepare(4u, (GenericCallback)callback_data);
    assert(mock_pas_19_4(&g_proxy, 1u, NULL) == 1u && g_calls == 0u);

    wmemset(wide, L'N', MOCK_THREAD_NAME_LIMIT + 1u);
    wide[MOCK_THREAD_NAME_LIMIT + 1u] = L'\0';
    g_mutate_wide = wide;
    prepare(10u, (GenericCallback)callback_thread);
    assert(mock_pas_root_10(&g_proxy, 77u, wide, 3u) == 0u && g_calls == 1u);
    prepare(10u, (GenericCallback)callback_thread);
    assert(mock_pas_root_10(&g_proxy, 77u, NULL, 3u) == 1u && g_calls == 0u);

    prepare(3u, (GenericCallback)callback1);
    g_return_status = 5u;
    assert(mock_pas_13_3(&g_proxy, 1u) == 5u);
    mock_callback_test_state(&state);
    assert(state.optional_failures >= 1u);
    prepare(3u, (GenericCallback)callback1);
    g_return_status = 6u;
    assert(mock_pas_8_3(&g_proxy, 1u) == 6u);
    mock_callback_test_state(&state);
    assert(state.critical_failures >= 1u);
    mock_callback_backend_reset();
    mock_callback_test_state(&state);
    assert(state.callback_count == 0u);
    assert(state.critical_failures == 0u);
    assert(state.optional_failures == 0u);
    return 0;
}
