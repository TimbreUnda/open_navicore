#include "mock_navicore.h"
#include "mock_api_telemetry.h"
#include "mock_callback_backend.h"
#include "mock_gps_backend.h"
#include "mock_pointer_backend.h"
#include "mock_renderer.h"
#include "mock_scalar_backend.h"

#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#define TEST_ADAPTER_COUNT 256u
#define TEST_DUMMY_SLOTS 128u

typedef struct {
    const PanaGenericVirtual *vptr;
    void *host_proxy;
    uint32_t adapter_id;
} TestAdapterObject;

typedef uint32_t (*TestGPSDataVirtual)(void *self, const void *gps_record);
typedef uint32_t (*TestByteVirtual)(void *self, uint8_t value);
typedef uint32_t (*TestFloatVirtual)(void *self, float value);
typedef uint32_t (*TestU32Virtual)(void *self, uint32_t value);
typedef uint32_t (*TestAudioVirtual)(void *self,
                                     uint32_t source,
                                     uint8_t volume);
typedef uint32_t (*TestNoArgVirtual)(void *self);
typedef uint32_t (*TestTwoU32Virtual)(void *, uint32_t, uint32_t);
typedef uint32_t (*TestThreeU32Virtual)(void *, uint32_t, uint32_t, uint32_t);
typedef uint32_t (*TestU32U32U8Virtual)(void *, uint32_t, uint32_t, uint8_t);
typedef uint32_t (*TestConnectivityStatusVirtual)(void *, uint32_t, uint32_t,
                                                  uint8_t, uint32_t);
typedef uint32_t (*TestConnectivitySubscriptionVirtual)(void *, uint32_t,
                                                        uint8_t, uint32_t);
typedef uint32_t (*TestLoggingVirtual)(void *, uint32_t, uint32_t,
                                       const uint8_t *, const uint32_t *);
typedef uint32_t (*TestHandlePointerVirtual)(void *, uint32_t, const void *);
typedef uint32_t (*TestCountPointerVirtual)(void *, uint32_t, uint32_t,
                                            const void *);
typedef uint32_t (*TestConnectionVirtual)(
    void *, uint32_t, uint32_t, const MockConnectionDeviceInfoInput *);
typedef uint32_t (*TestTefVirtual)(void *, uint32_t, uint32_t, const char *);
typedef uint32_t (*TestStatic0)(void *);
typedef uint32_t (*TestStatic1)(void *, uintptr_t);
typedef uint32_t (*TestStatic2)(void *, uintptr_t, uintptr_t);
typedef uint32_t (*TestStatic3)(void *, uintptr_t, uintptr_t, uintptr_t);
typedef uint32_t (*TestStatic4)(void *, uintptr_t, uintptr_t, uintptr_t,
                                uintptr_t);
typedef uint32_t (*TestStatic5)(void *, uintptr_t, uintptr_t, uintptr_t,
                                uintptr_t, uintptr_t);
typedef uint32_t (*TestStatic6)(void *, uintptr_t, uintptr_t, uintptr_t,
                                uintptr_t, uintptr_t, uintptr_t);
typedef uint32_t (*TestStatic7)(void *, uintptr_t, uintptr_t, uintptr_t,
                                uintptr_t, uintptr_t, uintptr_t, uintptr_t);

#define TEST_STATIC_CALL_0(function, object) \
    ((TestStatic0)(function))((object))
#define TEST_STATIC_CALL_1(function, object) \
    ((TestStatic1)(function))((object), 0u)
#define TEST_STATIC_CALL_2(function, object) \
    ((TestStatic2)(function))((object), 0u, 0u)
#define TEST_STATIC_CALL_3(function, object) \
    ((TestStatic3)(function))((object), 0u, 0u, 0u)
#define TEST_STATIC_CALL_4(function, object) \
    ((TestStatic4)(function))((object), 0u, 0u, 0u, 0u)
#define TEST_STATIC_CALL_5(function, object) \
    ((TestStatic5)(function))((object), 0u, 0u, 0u, 0u, 0u)
#define TEST_STATIC_CALL_6(function, object) \
    ((TestStatic6)(function))((object), 0u, 0u, 0u, 0u, 0u, 0u)
#define TEST_STATIC_CALL_7(function, object) \
    ((TestStatic7)(function))((object), 0u, 0u, 0u, 0u, 0u, 0u, 0u)

static void *static_slot_object(const char *adapter,
                                PanaRootObject *root,
                                void **interfaces)
{
    uint32_t value = 0u;
    const char *cursor = adapter;
    if (strcmp(adapter, "root") == 0) return root;
    while (*cursor != '\0') {
        assert(*cursor >= '0' && *cursor <= '9');
        value = value * 10u + (uint32_t)(*cursor - '0');
        ++cursor;
    }
    assert(value < TEST_ADAPTER_COUNT);
    assert(interfaces[value] != NULL);
    return interfaces[value];
}

static void exercise_generated_static_slots(PanaRootObject *root,
                                             void **interfaces,
                                             uint32_t expected_status)
{
#define MOCK_STATIC_SLOT(adapter, slot, words) do {                           \
        void *object = static_slot_object(#adapter, root, interfaces);         \
        const PanaGenericVirtual *vtable =                                    \
            *(const PanaGenericVirtual * const *)object;                      \
        assert(TEST_STATIC_CALL_##words(vtable[slot], object) ==              \
               expected_status);                                             \
    } while (0);
#include "../src/compat/qr39/provider_slots.def"
#undef MOCK_STATIC_SLOT
}

static int is_typed_provider_slot(uint32_t adapter, uint32_t slot)
{
    if (adapter == 1u)
        return slot == 3u || (slot >= 5u && slot <= 10u) || slot == 12u;
    if (adapter == 4u) return slot == 3u || slot == 4u;
    if (adapter == 6u) return slot == 3u;
    if (adapter == 7u) return slot >= 3u && slot <= 5u;
    if (adapter == 8u) {
        return (slot >= 3u && slot <= 9u) || (slot >= 12u && slot <= 14u);
    }
    if (adapter == 9u) return slot >= 3u && slot <= 6u;
    if (adapter == 10u)
        return slot == 3u || slot == 5u || slot == 7u || slot == 8u ||
               (slot >= 17u && slot <= 19u);
    if (adapter == 12u) return slot >= 3u && slot <= 7u;
    if (adapter == 13u) return slot >= 3u && slot <= 14u;
    if (adapter == 14u) return slot >= 3u && slot <= 9u;
    if (adapter == 17u) return slot == 4u || slot == 6u;
    if (adapter == 19u) return slot >= 3u && slot <= 9u;
    if (adapter == 20u) return slot >= 3u && slot <= 8u;
    if (adapter == 23u || adapter == 24u || adapter == 32u) return slot == 3u;
    if (adapter == 25u)
        return (slot >= 3u && slot <= 5u) || (slot >= 7u && slot <= 24u) ||
               (slot >= 26u && slot <= 30u);
    if (adapter == 26u) return slot >= 3u && slot <= 5u;
    if (adapter == 29u) return slot >= 3u && slot <= 6u;
    if (adapter == 30u) return slot >= 3u && slot <= 11u;
    if (adapter == 31u) return slot >= 3u && slot <= 16u;
    return 0;
}

static void assert_telemetry(uint32_t adapter,
                             uint32_t slot,
                             uint32_t calls,
                             uint32_t argument_count,
                             uint32_t argument0,
                             uint32_t argument1,
                             uint32_t argument2,
                             uint32_t argument3)
{
    MockApiTelemetryRecord record;

    memset(&record, 0xa5, sizeof(record));
    assert(mock_api_telemetry_test_get(adapter, slot, &record) == 1);
    assert(record.call_count == calls);
    assert(record.argument_count == argument_count);
    assert(record.argument[0] == argument0);
    assert(record.argument[1] == argument1);
    assert(record.argument[2] == argument2);
    assert(record.argument[3] == argument3);
}

static uint32_t event_order[64];
static volatile uint32_t event_count;
static void *hmi_interface;
static uint32_t transition_reply_handles[16];
static volatile uint32_t transition_reply_count;
static int32_t subscription_value;
static volatile uint32_t subscription_count;
static uint32_t state_handles[3];
static uint32_t state_displays[3];
static uint32_t state_values[3];
static volatile uint32_t state_request_count;
static uint32_t window_handles[3];
static uint32_t window_displays[3];
static volatile uint32_t window_request_count;
static uint32_t navigation_displays[3];
static volatile uint32_t navigation_ready_count;
static volatile uint32_t hmi_ready_count;
static volatile uint32_t inside_hmi_ready;
static volatile uint32_t screen_change_reply_count;
static volatile uint32_t screen_theme_reply_count;
static uint32_t screen_change_reply_handles[4];
static uint32_t screen_change_reply_screens[4];
static uint32_t screen_theme_reply_handles[4];
static uint32_t test_mode;
static uint32_t overflow_rejected;
static uint32_t voice_reply_handle;
static uint32_t voice_reply_result;
static uint32_t voice_reply_status;
static uint32_t voice_reply_count;
static uint32_t thread_callback_length;
static uint32_t audio_callback_status;
static uint32_t cluster_callback_status;
static pthread_mutex_t cluster_block_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cluster_block_condition = PTHREAD_COND_INITIALIZER;
static int cluster_callback_block;
static uint32_t cluster_callback_entries;
static int cluster_callback_entered;
static int cluster_callback_release;
static int cluster_callback_reentrant_deinitialize;
static int cluster_reentrant_deinitialize_entered;
static int cluster_reentrant_deinitialize_returned;

typedef struct {
    const PanaGenericVirtual *vptr;
    uint32_t adapter_id;
} TestReadyProxy;

static TestReadyProxy adapter_ready_proxies[TEST_ADAPTER_COUNT];

typedef struct {
    uint32_t adapter;
    uint32_t slot;
    uint32_t value_a;
    uint32_t value_b;
} TestCallbackRecord;

static TestCallbackRecord callback_records[64];
static volatile uint32_t callback_count;

static void record_event(uint32_t event)
{
    event_order[event_count++] = event;
}

static void put_le16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static void put_le64(uint8_t *destination, uint64_t value)
{
    put_le32(destination, (uint32_t)value);
    put_le32(destination + 4, (uint32_t)(value >> 32));
}

static void put_float_le(uint8_t *destination, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    put_le32(destination, bits);
}

static void put_double_le(uint8_t *destination, double value)
{
    uint64_t bits;

    memcpy(&bits, &value, sizeof(bits));
    put_le64(destination, bits);
}

void mock_renderer_test_observe(enum mock_renderer_test_event event)
{
    record_event(event == MOCK_RENDERER_TEST_CREATE ? 406u : 500u);
}

static uint32_t test_proxy_noop(void *self, ...)
{
    (void)self;
    return 0u;
}

static uint32_t test_si_ready(void *self, ...)
{
    (void)self;
    record_event(1u);
    return 0u;
}
static uint32_t test_thread_status(void *self,uint32_t id,const wchar_t *name,uint32_t status)
{(void)self;assert(id==77u);assert(status==3u);thread_callback_length=(uint32_t)wcslen(name);return 0u;}

static uint32_t test_hmi_ready(void *self, ...)
{
    uint32_t index;

    (void)self;
    record_event(20u);
    ++hmi_ready_count;
    inside_hmi_ready = 1u;
    assert(hmi_interface != NULL);
    if (test_mode == 0u) {
        assert((*(const PanaGenericVirtual **)hmi_interface)[5](
                   hmi_interface, 0x12345678u, 0u, 1u, 2u,
                   (const void *)(uintptr_t)0xabcdef00u) == 0u);
        assert((*(const PanaGenericVirtual **)hmi_interface)[9](
                   hmi_interface, 2u, 0u, 2u, 0u, 0u) == 0u);
        assert((*(const PanaGenericVirtual **)hmi_interface)[10](
                   hmi_interface, 3u,
                   (const void *)(uintptr_t)0x12340000u) == 0u);
    } else if (test_mode == 1u) {
        for (index = 0u; index < 8u; ++index) {
            assert((*(const PanaGenericVirtual **)hmi_interface)[5](
                       hmi_interface, 0x1000u + index, 0u, index + 1u,
                       index + 2u,
                       (const void *)(uintptr_t)(0x2000u + index)) == 0u);
        }
        overflow_rejected =
            (*(const PanaGenericVirtual **)hmi_interface)[5](
                hmi_interface, 0x9999u, 0u, 9u, 9u,
                (const void *)(uintptr_t)0x99990000u);
    }
    inside_hmi_ready = 0u;
    return 0u;
}

static uint32_t test_adapter_ready(void *self, ...)
{
    TestReadyProxy *proxy = (TestReadyProxy *)self;

    assert(proxy != NULL);
    record_event(100u + proxy->adapter_id);
    return 0u;
}

static uint32_t record_adapter_callback(void *self,
                                        uint32_t slot,
                                        uint32_t value_a,
                                        uint32_t value_b)
{
    TestReadyProxy *proxy = (TestReadyProxy *)self;
    uint32_t index = callback_count;

    assert(proxy != NULL);
    assert(index < sizeof(callback_records) / sizeof(callback_records[0]));
    callback_records[index].adapter = proxy->adapter_id;
    callback_records[index].slot = slot;
    callback_records[index].value_a = value_a;
    callback_records[index].value_b = value_b;
    ++callback_count;
    return 0u;
}

#define DEFINE_TEST_ADAPTER_CALLBACK(name, slot_value)                         \
    static uint32_t name(void *self, ...)                                     \
    {                                                                          \
        va_list arguments;                                                     \
        uint32_t value_a;                                                      \
        uint32_t value_b;                                                      \
        va_start(arguments, self);                                             \
        value_a = va_arg(arguments, uint32_t);                                 \
        value_b = va_arg(arguments, uint32_t);                                 \
        va_end(arguments);                                                     \
        return record_adapter_callback(self, slot_value, value_a, value_b);    \
    }

DEFINE_TEST_ADAPTER_CALLBACK(test_adapter_slot4, 4u)
DEFINE_TEST_ADAPTER_CALLBACK(test_adapter_slot8, 8u)
DEFINE_TEST_ADAPTER_CALLBACK(test_adapter_slot9, 9u)

static uint32_t test_adapter_slot3(void *self, ...)
{
    TestReadyProxy *proxy=(TestReadyProxy *)self; va_list ap; uint32_t a,b=0u;
    uint32_t status;
    va_start(ap,self);
    if(proxy->adapter_id==17u){
        uint32_t entry;
        (void)va_arg(ap,const void *);a=0u;
        (void)pthread_mutex_lock(&cluster_block_lock);
        entry=++cluster_callback_entries;
        if(cluster_callback_block!=0&&entry==1u){
            cluster_callback_entered=1;
            (void)pthread_cond_broadcast(&cluster_block_condition);
            while(cluster_callback_release==0){
                (void)pthread_cond_wait(&cluster_block_condition,
                                        &cluster_block_lock);
            }
        }
        (void)pthread_mutex_unlock(&cluster_block_lock);
        if(cluster_callback_reentrant_deinitialize!=0&&entry==2u){
            (void)pthread_mutex_lock(&cluster_block_lock);
            cluster_reentrant_deinitialize_entered=1;
            (void)pthread_cond_broadcast(&cluster_block_condition);
            (void)pthread_mutex_unlock(&cluster_block_lock);
            panaDeinitialize();
            (void)pthread_mutex_lock(&cluster_block_lock);
            cluster_reentrant_deinitialize_returned=1;
            (void)pthread_cond_broadcast(&cluster_block_condition);
            (void)pthread_mutex_unlock(&cluster_block_lock);
        }
    }
    else {a=va_arg(ap,uint32_t);if(proxy->adapter_id==10u)b=va_arg(ap,uint32_t);}
    va_end(ap);
    status=record_adapter_callback(self,3u,a,b);
    return proxy->adapter_id==17u?cluster_callback_status:status;
}

static void *test_cluster_send_thread(void *opaque)
{
    uint32_t *result = (uint32_t *)opaque;
    uint8_t record[MOCK_CLUSTER_RECORD_SIZE];
    memset(record, 0x3c, sizeof(record));
    *result = mock_callback_send_cluster(record);
    return NULL;
}

static uint32_t test_adapter_slot5(void *self, uint32_t a){return record_adapter_callback(self,5u,a,0u);}
static uint32_t test_adapter_slot6(void *self, ...)
{
    TestReadyProxy *proxy=(TestReadyProxy *)self; uint32_t a=0u;
    if(proxy->adapter_id!=17u){va_list ap;va_start(ap,self);a=va_arg(ap,uint32_t);va_end(ap);}
    return record_adapter_callback(self,6u,a,0u);
}
static uint32_t test_adapter_slot7(void *self,uint32_t a){return record_adapter_callback(self,7u,a,0u);}
static uint32_t test_adapter_slot11(void *self,uint32_t a){return record_adapter_callback(self,11u,a,0u);}
static uint32_t test_adapter_slot19(void *self,uint32_t a,uint32_t b){return record_adapter_callback(self,19u,a,b);}
static uint32_t test_adapter_slot10(void *self, uint32_t value)
{
    (void)record_adapter_callback(self, 10u, value, 0u);
    return audio_callback_status;
}
static uint32_t test_adapter_slot23(void *self,uint32_t handle,uint32_t result,uint32_t status)
{(void)self;voice_reply_handle=handle;voice_reply_result=result;voice_reply_status=status;++voice_reply_count;return 0u;}

static uint32_t test_subscribe(void *self, ...)
{
    va_list arguments;

    (void)self;
    va_start(arguments, self);
    subscription_value = va_arg(arguments, int32_t);
    va_end(arguments);
    ++subscription_count;
    record_event(2u);
    return 0u;
}

static uint32_t test_request_hmi_state(void *self, ...)
{
    va_list arguments;
    uint32_t index = state_request_count;

    (void)self;
    assert(index < 3u);
    va_start(arguments, self);
    state_handles[index] = va_arg(arguments, uint32_t);
    state_displays[index] = va_arg(arguments, uint32_t);
    state_values[index] = va_arg(arguments, uint32_t);
    va_end(arguments);
    ++state_request_count;
    record_event(3u + index * 3u);
    return 0u;
}

static uint32_t test_request_screen_window_identifier(void *self, ...)
{
    va_list arguments;
    uint32_t index = window_request_count;

    (void)self;
    assert(index < 3u);
    va_start(arguments, self);
    window_handles[index] = va_arg(arguments, uint32_t);
    window_displays[index] = va_arg(arguments, uint32_t);
    va_end(arguments);
    ++window_request_count;
    record_event(4u + index * 3u);
    return 0u;
}

static uint32_t test_navigation_ready(void *self, ...)
{
    va_list arguments;
    uint32_t index = navigation_ready_count;

    (void)self;
    assert(index < 3u);
    va_start(arguments, self);
    navigation_displays[index] = va_arg(arguments, uint32_t);
    va_end(arguments);
    ++navigation_ready_count;
    record_event(5u + index * 3u);
    return 0u;
}

static uint32_t test_transition_reply(void *self, ...)
{
    va_list arguments;

    (void)self;
    va_start(arguments, self);
    transition_reply_handles[transition_reply_count] =
        va_arg(arguments, uint32_t);
    va_end(arguments);
    assert(inside_hmi_ready == 0u);
    record_event(21u + transition_reply_count);
    ++transition_reply_count;
    return 0u;
}

static uint32_t test_reply_screen_change(void *self,
                                         uint32_t handle,
                                         uint32_t result,
                                         uint32_t screen_type)
{
    uint32_t index = screen_change_reply_count;

    (void)self;
    assert(index < 4u);
    assert(result == 0u);
    screen_change_reply_handles[index] = handle;
    screen_change_reply_screens[index] = screen_type;
    ++screen_change_reply_count;
    record_event(510u);

    /* This is the host reaction observed in the stock v9 trace. */
    if (test_mode == 0u) {
        assert((*(const PanaGenericVirtual **)hmi_interface)[5](
                   hmi_interface, handle == 2u ? 4u : 7u,
                   0u, 0x11u, 1u,
                   (const void *)(uintptr_t)0x3000u) == 0u);
    }
    return 0u;
}

static uint32_t test_reply_screen_theme(void *self, uint32_t handle)
{
    uint32_t index = screen_theme_reply_count;

    (void)self;
    assert(index < 4u);
    screen_theme_reply_handles[index] = handle;
    ++screen_theme_reply_count;
    record_event(512u);
    return 0u;
}

static void test_delay(unsigned int milliseconds)
{
    struct timespec delay;

    delay.tv_sec = (time_t)(milliseconds / 1000u);
    delay.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    (void)nanosleep(&delay, NULL);
}

static void reset_observations(uint32_t mode)
{
    memset(event_order, 0, sizeof(event_order));
    memset(transition_reply_handles, 0, sizeof(transition_reply_handles));
    memset(state_handles, 0, sizeof(state_handles));
    memset(state_displays, 0, sizeof(state_displays));
    memset(state_values, 0, sizeof(state_values));
    memset(window_handles, 0, sizeof(window_handles));
    memset(window_displays, 0, sizeof(window_displays));
    memset(navigation_displays, 0, sizeof(navigation_displays));
    memset(screen_change_reply_handles, 0,
           sizeof(screen_change_reply_handles));
    memset(screen_change_reply_screens, 0,
           sizeof(screen_change_reply_screens));
    memset(screen_theme_reply_handles, 0,
           sizeof(screen_theme_reply_handles));
    memset(callback_records, 0, sizeof(callback_records));
    event_count = 0u;
    transition_reply_count = 0u;
    subscription_value = 0;
    subscription_count = 0u;
    state_request_count = 0u;
    window_request_count = 0u;
    navigation_ready_count = 0u;
    callback_count = 0u;
    hmi_ready_count = 0u;
    inside_hmi_ready = 0u;
    screen_change_reply_count = 0u;
    screen_theme_reply_count = 0u;
    test_mode = mode;
    overflow_rejected = 0u;
    voice_reply_handle = voice_reply_result = voice_reply_status = 0u;
    voice_reply_count = 0u;
    thread_callback_length = 0u;
    audio_callback_status = 0u;
}

int main(void)
{
    PanaInitializationData initialization;
    PanaServerVersionHeadUnit server = {{ 24u, 7u, 2u, 3u, 4u, 5u }};
    PanaClientVersionNavigationSoftwareModule client;
    PanaRootObject *root;
    PanaGetAdapterVirtual get_adapter;
    void *first_interfaces[TEST_ADAPTER_COUNT];
    uint32_t compatibility = UINT32_MAX;
    uint32_t adapter;
    uint32_t slot;
    PanaGenericVirtual si_vtable[15];
    PanaGenericVirtual hmi_vtable[15];
    PanaGenericVirtual adapter_ready_vtable[26];
    uint8_t gps_record[MOCK_GPS_RECORD_SIZE];
    MockGpsSnapshot gps_snapshot;
    MockGpsSnapshot gps_snapshot_after_mutation;
    MockScalarBackendTestState scalar_state;
    MockPointerBackendTestState pointer_state;
    MockCallbackTestState callback_state;
    uint8_t cluster_record[MOCK_CLUSTER_RECORD_SIZE];
    char thread_name[100];
    uint8_t logging_levels[4];
    uint32_t logging_sizes[4];
    uint8_t intl_settings[4];
    char vin17[17];
    uint8_t transport_item[16];
    MockConnectionDeviceInfoInput connection_info;
    char imei[128];
    char msisdn[128];
    char tef_filename[512];
    uint32_t speed_bits;
    uint32_t request_handle;
    uint32_t older_audio_attempt;
    uint32_t newer_audio_attempt;
    uint32_t lifecycle_cluster_result = UINT32_MAX;
    uint32_t lifecycle_cluster_result_second = UINT32_MAX;
    pthread_t lifecycle_cluster_thread;
    pthread_t lifecycle_cluster_thread_second;
    struct { const PanaGenericVirtual *vptr; } si_proxy;
    struct { const PanaGenericVirtual *vptr; } hmi_proxy;

    mock_renderer_stub_reset();
    memset(&client, 0xa5, sizeof(client));
    assert(panaVersionCheck(server, &client, &compatibility) == 0u);
    assert(client.api_major == 24u);
    assert(client.api_minor == 7u);
    assert(client.version_a == 0u);
    assert(client.version_b == 0u);
    assert(client.packed_version == 0u);
    assert(client.version_text != 0);
    assert(compatibility == 1u);

    memset(&initialization, 0, sizeof(initialization));
    assert(panaInitialize(&initialization) == 0u);
    root = (PanaRootObject *)panaGetSoftwareInfrastructure();
    assert(root != 0);
    assert(root == (PanaRootObject *)panaGetSoftwareInfrastructure());
    assert(root->vptr != 0);
    assert(root->vptr->slot[3] != 0);
    assert(root->host_proxy == 0);

    get_adapter = (PanaGetAdapterVirtual)root->vptr->slot[3];
    for (adapter = 0u; adapter < TEST_ADAPTER_COUNT; ++adapter) {
        const PanaGenericVirtual *dummy_vtable;
        void *again = 0;

        first_interfaces[adapter] = 0;
        if (adapter == 21u || adapter == 27u || adapter == 28u) {
            first_interfaces[adapter] = (void *)(uintptr_t)1u;
            again = (void *)(uintptr_t)1u;
            assert(get_adapter(root, adapter,
                               &first_interfaces[adapter]) == 1u);
            assert(first_interfaces[adapter] == 0);
            assert(get_adapter(root, adapter, &again) == 1u);
            assert(again == 0);
            continue;
        }
        assert(get_adapter(root, adapter, &first_interfaces[adapter]) == 0u);
        assert(first_interfaces[adapter] != 0);
        assert(get_adapter(root, adapter, &again) == 0u);
        assert(again == first_interfaces[adapter]);
        assert(((TestAdapterObject *)first_interfaces[adapter])->host_proxy == 0);
        assert(((TestAdapterObject *)first_interfaces[adapter])->adapter_id == adapter);

        dummy_vtable = *(const PanaGenericVirtual **)first_interfaces[adapter];
        assert(dummy_vtable != 0);
        for (slot = 0u; slot < TEST_DUMMY_SLOTS; ++slot) {
            assert(dummy_vtable[slot] != 0);
            if (!is_typed_provider_slot(adapter, slot)) {
                assert(dummy_vtable[slot](first_interfaces[adapter]) == 0u);
            }
        }
    }
    for (adapter = 1u; adapter < TEST_ADAPTER_COUNT; ++adapter) {
        if (first_interfaces[adapter] != 0 &&
            first_interfaces[adapter - 1u] != 0) {
            assert(first_interfaces[adapter] != first_interfaces[adapter - 1u]);
        }
    }

    /* Build a synthetic
     * little-endian host record, including signed coordinates and opaque
     * satellite bytes, and verify its normalized backend value copy. */
    assert(*(const PanaGenericVirtual **)first_interfaces[8] !=
           *(const PanaGenericVirtual **)first_interfaces[14]);
    assert(*(const PanaGenericVirtual **)first_interfaces[9] !=
           *(const PanaGenericVirtual **)first_interfaces[14]);
    assert(*(const PanaGenericVirtual **)first_interfaces[10] !=
           *(const PanaGenericVirtual **)first_interfaces[14]);
    assert(*(const PanaGenericVirtual **)first_interfaces[14] !=
           *(const PanaGenericVirtual **)first_interfaces[15]);
    assert(mock_gps_test_call_count() == 0u);
    assert(mock_gps_test_last_record_address() == (uintptr_t)0u);
    assert(mock_gps_backend_test_generation() == 0u);
    assert(mock_gps_backend_test_latest(&gps_snapshot) == 0);
    memset(gps_record, 0, sizeof(gps_record));
    put_le16(gps_record + MOCK_GPS_ALTITUDE_RAW_OFFSET, 0x1234u);
    gps_record[MOCK_GPS_FIX_TYPE_OFFSET] = 3u;
    put_le16(gps_record + MOCK_GPS_HEADING_RAW_OFFSET, 0xabcdu);
    put_le16(gps_record + MOCK_GPS_HDOP_RAW_OFFSET, 0x1122u);
    put_le32(gps_record + MOCK_GPS_ACCURACY_RAW_OFFSET, 0x89abcdefu);
    put_le32(gps_record + MOCK_GPS_LATITUDE_E7_OFFSET,
             (uint32_t)(int32_t)-123456789);
    put_le32(gps_record + MOCK_GPS_LONGITUDE_E7_OFFSET,
             (uint32_t)(int32_t)-987654321);
    gps_record[MOCK_GPS_SATS_USED_OFFSET] = 9u;
    gps_record[MOCK_GPS_SATS_IN_VIEW_OFFSET] = 14u;
    put_le16(gps_record + MOCK_GPS_PDOP_RAW_OFFSET, 0x3344u);
    for (adapter = 0u; adapter < MOCK_GPS_SATELLITE_COUNT; ++adapter) {
        for (slot = 0u; slot < MOCK_GPS_SATELLITE_SIZE; ++slot) {
            gps_record[MOCK_GPS_SATELLITES_OFFSET +
                       adapter * MOCK_GPS_SATELLITE_SIZE + slot] =
                (uint8_t)(adapter * 16u + slot);
        }
    }
    put_le16(gps_record + MOCK_GPS_SPEED_RAW_MASKED_OFFSET, 0x8f01u);
    put_le16(gps_record + MOCK_GPS_VDOP_RAW_OFFSET, 0x5566u);
    gps_record[MOCK_GPS_QUALITY_STATUS_OFFSET] = 0xa5u;
    gps_record[MOCK_GPS_SYNC_COUNTER_OFFSET] = 0x5au;
    put_le16(gps_record + MOCK_GPS_ELLIPSOID_ALTITUDE_RAW_OFFSET, 0x7788u);
    put_le16(gps_record + MOCK_GPS_UNKNOWN_100_OFFSET, 0x99aau);
    put_float_le(gps_record + MOCK_GPS_TRAVEL_DISTANCE_RAW_OFFSET, 1234.5f);
    put_double_le(gps_record + MOCK_GPS_ALTITUDE_CHANGE_RAW_OFFSET, -42.25);
    put_double_le(gps_record + MOCK_GPS_NOISE_RAW_OFFSET, 0.03125);
    assert(((TestGPSDataVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[9])[4])(
               first_interfaces[9], gps_record) == 0u);
    assert(mock_gps_test_call_count() == 1u);
    assert(mock_gps_test_last_record_address() ==
           (uintptr_t)gps_record);
    assert(mock_gps_backend_test_generation() == 1u);
    assert(mock_gps_backend_test_latest(&gps_snapshot) == 1);
    assert(gps_snapshot.altitude_raw == 0x1234u);
    assert(gps_snapshot.fix_type == 3u);
    assert(gps_snapshot.heading_raw == 0xabcdu);
    assert(gps_snapshot.hdop_raw == 0x1122u);
    assert(gps_snapshot.accuracy_raw == 0x89abcdefu);
    assert(gps_snapshot.latitude_e7 == -123456789);
    assert(gps_snapshot.longitude_e7 == -987654321);
    assert(gps_snapshot.sats_used == 9u);
    assert(gps_snapshot.sats_in_view == 14u);
    assert(gps_snapshot.pdop_raw == 0x3344u);
    assert(gps_snapshot.satellites[15][11] == (uint8_t)0xfbu);
    assert(gps_snapshot.speed_raw_masked == 0x8f01u);
    assert(gps_snapshot.vdop_raw == 0x5566u);
    assert(gps_snapshot.quality_status == 0xa5u);
    assert(gps_snapshot.sync_counter == 0x5au);
    assert(gps_snapshot.ellipsoid_altitude_raw == 0x7788u);
    assert(gps_snapshot.unknown_100 == 0x99aau);
    assert(gps_snapshot.travel_distance_raw == 1234.5f);
    assert(gps_snapshot.altitude_change_raw == -42.25);
    assert(gps_snapshot.noise_raw == 0.03125);

    memset(gps_record, 0xff, sizeof(gps_record));
    assert(mock_gps_backend_test_latest(&gps_snapshot_after_mutation) == 1);
    assert(memcmp(&gps_snapshot, &gps_snapshot_after_mutation,
                  sizeof(gps_snapshot)) == 0);
    assert(((TestGPSDataVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[9])[4])(
               first_interfaces[9], NULL) == 0u);
    assert(mock_gps_test_call_count() == 2u);
    assert(mock_gps_test_last_record_address() == (uintptr_t)0u);
    assert(mock_gps_backend_test_generation() == 1u);
    assert(mock_gps_backend_test_latest(&gps_snapshot_after_mutation) == 1);
    assert(memcmp(&gps_snapshot, &gps_snapshot_after_mutation,
                  sizeof(gps_snapshot)) == 0);
    assert((*(const PanaGenericVirtual **)first_interfaces[14])[4](
               first_interfaces[14]) == 0u);
    assert(mock_gps_test_call_count() == 2u);

    /* Typed scalar slots route only to their adapter-specific backend state.
     * Exercise both captured values and structurally valid representatives. */
    memset(&scalar_state, 0xa5, sizeof(scalar_state));
    mock_scalar_backend_test_state(&scalar_state);
    assert(scalar_state.vehicle_generation == 0u);
    assert(scalar_state.gps_interval_generation == 0u);
    assert(scalar_state.audio_generation == 0u);
    assert(((TestByteVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[8])[4])(
               first_interfaces[8], 1u) == 0u);
    assert(((TestByteVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[8])[4])(
               first_interfaces[8], 0xfeu) == 0u);
    assert(((TestByteVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[8])[6])(
               first_interfaces[8], 1u) == 0u);
    assert(((TestByteVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[8])[6])(
               first_interfaces[8], 0x7fu) == 0u);
    assert(((TestFloatVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[8])[8])(
               first_interfaces[8], -13.25f) == 0u);
    assert(((TestU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[9])[6])(
               first_interfaces[9], 1000u) == 0u);
    assert(((TestU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[9])[6])(
               first_interfaces[9], 250u) == 0u);
    assert(((TestAudioVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[10])[19])(
               first_interfaces[10], 1u, 15u) == 0u);
    assert(((TestAudioVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[10])[19])(
               first_interfaces[10], 0xdeadbeefu, 0xffu) == 0u);
    mock_scalar_backend_test_state(&scalar_state);
    memcpy(&speed_bits, &scalar_state.displayed_vehicle_speed,
           sizeof(speed_bits));
    assert(scalar_state.day_night_status == 0xfeu);
    assert(scalar_state.day_night_setting == 0x7fu);
    assert(scalar_state.displayed_vehicle_speed == -13.25f);
    assert(scalar_state.displayed_vehicle_speed_bits == speed_bits);
    assert(scalar_state.gps_interval_ms == 250u);
    assert(scalar_state.audio_source == 0xdeadbeefu);
    assert(scalar_state.volume == 0xffu);
    assert(scalar_state.vehicle_generation == 5u);
    assert(scalar_state.gps_interval_generation == 2u);
    assert(scalar_state.audio_generation == 2u);
    assert(scalar_state.day_night_status_calls == 2u);
    assert(scalar_state.day_night_setting_calls == 2u);
    assert(scalar_state.vehicle_speed_calls == 1u);
    assert(scalar_state.gps_interval_calls == 2u);
    assert(scalar_state.audio_calls == 2u);
    assert((*(const PanaGenericVirtual **)first_interfaces[14])[4](
               first_interfaces[14], 0x1234u) == 0u);
    mock_scalar_backend_test_state(&scalar_state);
    assert(scalar_state.vehicle_generation == 5u);
    assert(scalar_state.gps_interval_generation == 2u);
    assert(scalar_state.audio_generation == 2u);

    assert(((TestAudioVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[8])[3])(
               first_interfaces[8], 0x80000003u, 0xf3u) == 0u);
    assert(((TestAudioVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[8])[5])(
               first_interfaces[8], 0x80000005u, 0xf5u) == 0u);
    assert(((TestU32U32U8Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[10])[17])(
               first_interfaces[10], 0x10u, 0xfeed0011u, 0xfdu) == 0u);
    assert(((TestU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[12])[5])(
               first_interfaces[12], 0x1205u) == 0u);
    assert(((TestU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[12])[6])(
               first_interfaces[12], 0x1206u) == 0u);
    assert(((TestThreeU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[12])[7])(
               first_interfaces[12], 0x1207u, 0xffffffffu,
               0x80000000u) == 0u);
    assert(((TestTwoU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[13])[3])(
               first_interfaces[13], 0x1303u, 0xffffffffu) == 0u);
    assert(((TestU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[13])[4])(
               first_interfaces[13], 0x80000004u) == 0u);
    assert(((TestU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[13])[12])(
               first_interfaces[13], 0x130cu) == 0u);
    assert(((TestU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[13])[13])(
               first_interfaces[13], 0x130du) == 0u);
    assert(((TestU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[13])[14])(
               first_interfaces[13], 0x130eu) == 0u);
    assert(((TestAudioVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[17])[6])(
               first_interfaces[17], 0x1706u, 0xffu) == 0u);
    assert(((TestConnectivityStatusVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[20])[3])(
               first_interfaces[20], 0x2003u, 0xabcdef01u, 0xfeu,
               0x87654321u) == 0u);
    assert(((TestConnectivitySubscriptionVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[20])[4])(
               first_interfaces[20], 0xabcdef04u, 0xfcu,
               0x76543210u) == 0u);
    assert(((TestU32U32U8Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[20])[7])(
               first_interfaces[20], 0x2007u, 0x70000002u, 0xf7u) == 0u);
    assert(((TestU32U32U8Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[20])[8])(
               first_interfaces[20], 0x2008u, 0x80000002u, 0xf8u) == 0u);
    assert(((TestU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[25])[19])(
               first_interfaces[25], 0x2519u) == 0u);
    assert(((TestU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[25])[19])(
               first_interfaces[25], 0xffffffffu) == 0u);
    assert(((TestAudioVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[26])[5])(
               first_interfaces[26], 0x2605u, 0xf5u) == 0u);
    assert(((TestU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[29])[3])(
               first_interfaces[29], 0xffffffffu) == 0u);
    assert(((TestNoArgVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[29])[4])(
               first_interfaces[29]) == 0u);
    assert(((TestU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[30])[3])(
               first_interfaces[30], 0x3003u) == 0u);
    assert(((TestU32Virtual)
               (*(const PanaGenericVirtual **)first_interfaces[30])[4])(
               first_interfaces[30], 0x3004u) == 0u);
    assert(((TestByteVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[31])[3])(
               first_interfaces[31], 0xf3u) == 0u);
    assert(((TestByteVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[31])[5])(
               first_interfaces[31], 0xf5u) == 0u);

    /* Enumerate and validate all 29 scalar-input rows, including the five
     * high-value slots that were typed in the previous milestone. */
    assert_telemetry(8u, 3u, 1u, 2u, 0x80000003u, 0xf3u, 0u, 0u);
    assert_telemetry(8u, 4u, 2u, 1u, 0xfeu, 0u, 0u, 0u);
    assert_telemetry(8u, 5u, 1u, 2u, 0x80000005u, 0xf5u, 0u, 0u);
    assert_telemetry(8u, 6u, 2u, 1u, 0x7fu, 0u, 0u, 0u);
    assert_telemetry(8u, 8u, 1u, 1u, speed_bits, 0u, 0u, 0u);
    assert_telemetry(9u, 6u, 2u, 1u, 250u, 0u, 0u, 0u);
    assert_telemetry(10u, 17u, 1u, 3u, 0x10u, 0xfeed0011u, 0xfdu, 0u);
    assert_telemetry(10u, 19u, 2u, 2u, 0xdeadbeefu, 0xffu, 0u, 0u);
    assert_telemetry(12u, 5u, 1u, 1u, 0x1205u, 0u, 0u, 0u);
    assert_telemetry(12u, 6u, 1u, 1u, 0x1206u, 0u, 0u, 0u);
    assert_telemetry(12u, 7u, 1u, 3u, 0x1207u, 0xffffffffu,
                     0x80000000u, 0u);
    assert_telemetry(13u, 3u, 1u, 2u, 0x1303u, 0xffffffffu, 0u, 0u);
    assert_telemetry(13u, 4u, 1u, 1u, 0x80000004u, 0u, 0u, 0u);
    assert_telemetry(13u, 12u, 1u, 1u, 0x130cu, 0u, 0u, 0u);
    assert_telemetry(13u, 13u, 1u, 1u, 0x130du, 0u, 0u, 0u);
    assert_telemetry(13u, 14u, 1u, 1u, 0x130eu, 0u, 0u, 0u);
    assert_telemetry(17u, 6u, 1u, 2u, 0x1706u, 0xffu, 0u, 0u);
    assert_telemetry(20u, 3u, 1u, 4u, 0x2003u, 0xabcdef01u, 0xfeu,
                     0x87654321u);
    assert_telemetry(20u, 4u, 1u, 3u, 0xabcdef04u, 0xfcu,
                     0x76543210u, 0u);
    assert_telemetry(20u, 7u, 1u, 3u, 0x2007u, 0x70000002u, 0xf7u, 0u);
    assert_telemetry(20u, 8u, 1u, 3u, 0x2008u, 0x80000002u, 0xf8u, 0u);
    assert_telemetry(25u, 19u, 2u, 1u, 0xffffffffu, 0u, 0u, 0u);
    assert_telemetry(26u, 5u, 1u, 2u, 0x2605u, 0xf5u, 0u, 0u);
    assert_telemetry(29u, 3u, 1u, 1u, 0xffffffffu, 0u, 0u, 0u);
    assert_telemetry(29u, 4u, 1u, 0u, 0u, 0u, 0u, 0u);
    assert_telemetry(30u, 3u, 1u, 1u, 0x3003u, 0u, 0u, 0u);
    assert_telemetry(30u, 4u, 1u, 1u, 0x3004u, 0u, 0u, 0u);
    assert_telemetry(31u, 3u, 1u, 1u, 0xf3u, 0u, 0u, 0u);
    assert_telemetry(31u, 5u, 1u, 1u, 0xf5u, 0u, 0u, 0u);
    mock_scalar_backend_test_state(&scalar_state);
    assert(scalar_state.day_night_status == 0xf3u);
    assert(scalar_state.day_night_setting == 0xf5u);
    assert(scalar_state.audio_source == 0xfeed0011u);
    assert(scalar_state.volume == 0xfdu);
    assert(scalar_state.vehicle_generation == 7u);
    assert(scalar_state.audio_generation == 3u);

    /* A statically reachable, dynamically unobserved slot has its own typed
     * lifecycle-gated implementation and records the recovered ABI word. */
    assert((*(const PanaGenericVirtual **)first_interfaces[12])[4](
               first_interfaces[12], 0xaaaaaaaau) == 0u);
    assert_telemetry(12u, 4u, 1u, 1u, 0xaaaaaaaau, 0u, 0u, 0u);

    /* Exercise every recovered pointer-input contract. Each backend result is
     * owned; caller and nested storage may be changed immediately on return. */
    assert(*(const PanaGenericVirtual **)first_interfaces[6] !=
           *(const PanaGenericVirtual **)first_interfaces[14]);
    assert(*(const PanaGenericVirtual **)first_interfaces[7] !=
           *(const PanaGenericVirtual **)first_interfaces[14]);
    assert(*(const PanaGenericVirtual **)first_interfaces[19] !=
           *(const PanaGenericVirtual **)first_interfaces[14]);
    mock_pointer_backend_test_state(&pointer_state);
    assert(pointer_state.generation == 0u);

    logging_levels[0] = 1u;
    logging_levels[1] = 2u;
    logging_levels[2] = 3u;
    logging_levels[3] = 4u;
    logging_sizes[0] = 0x11111111u;
    logging_sizes[1] = 0x22222222u;
    logging_sizes[2] = 0x33333333u;
    logging_sizes[3] = 0x44444444u;
    assert(((TestLoggingVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[6])[3])(
               first_interfaces[6], 0x6003u, 4u,
               logging_levels, logging_sizes) == 0u);
    memset(logging_levels, 0xff, sizeof(logging_levels));
    memset(logging_sizes, 0xff, sizeof(logging_sizes));
    mock_pointer_backend_test_state(&pointer_state);
    assert(pointer_state.logging_present == 1u);
    assert(pointer_state.logging_supported == 1u);
    assert(pointer_state.logging_levels[3] == 4u);
    assert(pointer_state.logging_sizes[3] == 0x44444444u);
    assert(((TestLoggingVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[6])[3])(
               first_interfaces[6], 0x6004u, 3u,
               (const uint8_t *)(uintptr_t)1u,
               (const uint32_t *)(uintptr_t)1u) == 0u);
    mock_pointer_backend_test_state(&pointer_state);
    assert(pointer_state.logging_supported == 0u);
    assert(pointer_state.logging_present == 0u);
    assert(((TestLoggingVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[6])[3])(
               first_interfaces[6], 0x6005u, 4u, NULL, NULL) == 0u);

    intl_settings[0] = 0x34u;
    intl_settings[1] = 0x12u;
    intl_settings[2] = 0xfeu;
    intl_settings[3] = 0xfdu;
    assert(((TestHandlePointerVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[7])[3])(
               first_interfaces[7], 0x7003u, intl_settings) == 0u);
    memset(intl_settings, 0, sizeof(intl_settings));
    mock_pointer_backend_test_state(&pointer_state);
    assert(pointer_state.intl_present == 1u);
    assert(pointer_state.intl_language == 0x1234u);
    assert(pointer_state.intl_date_format == 0xfeu);
    assert(pointer_state.intl_time_format == 0xfdu);
    assert(((TestHandlePointerVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[7])[3])(
               first_interfaces[7], 0x7004u, NULL) == 0u);

    for (slot = 0u; slot < sizeof(vin17); ++slot) {
        vin17[slot] = (char)('A' + slot);
    }
    assert(((TestHandlePointerVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[8])[12])(
               first_interfaces[8], 0x8012u, vin17) == 0u);
    memset(vin17, 'Z', sizeof(vin17));
    mock_pointer_backend_test_state(&pointer_state);
    assert(pointer_state.vin_present == 1u);
    assert(pointer_state.vin[0] == 'A');
    assert(pointer_state.vin[16] == 'Q');
    assert(pointer_state.vin[17] == '\0');
    assert(((TestHandlePointerVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[8])[12])(
               first_interfaces[8], 0x8013u, NULL) == 0u);

    memset(transport_item, 0xa5, sizeof(transport_item));
    transport_item[0] = 1u;
    put_le32(transport_item + 8, 4000u);
    put_le32(transport_item + 12, 0x89abcdefu);
    assert(((TestCountPointerVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[19])[3])(
               first_interfaces[19], 0x1903u, 1u, transport_item) == 0u);
    memset(transport_item, 0, sizeof(transport_item));
    mock_pointer_backend_test_state(&pointer_state);
    assert(pointer_state.transport_supported == 1u);
    assert(pointer_state.transport_present == 1u);
    assert(pointer_state.transport_kind == 1u);
    assert(pointer_state.transport_buffer_size == 4000u);
    assert(pointer_state.transport_field_0c == 0x89abcdefu);
    assert(((TestCountPointerVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[19])[3])(
               first_interfaces[19], 0x1904u, 2u,
               (const void *)(uintptr_t)1u) == 0u);
    mock_pointer_backend_test_state(&pointer_state);
    assert(pointer_state.transport_supported == 0u);
    assert(pointer_state.transport_present == 0u);
    assert(((TestCountPointerVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[19])[3])(
               first_interfaces[19], 0x1905u, 0u,
               (const void *)(uintptr_t)1u) == 0u);

    memset(imei, 'I', sizeof(imei));
    memset(msisdn, 0, sizeof(msisdn));
    memcpy(msisdn, "123456789", 9u);
    connection_info.imei = imei;
    connection_info.field_04 = 0xabcdef04u;
    connection_info.msisdn = msisdn;
    connection_info.sim_iccid = NULL;
    assert(((TestConnectionVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[20])[6])(
               first_interfaces[20], 0x2006u, 0x76543210u,
               &connection_info) == 0u);
    memset(imei, 'X', sizeof(imei));
    memset(msisdn, 'Y', sizeof(msisdn));
    connection_info.field_04 = 0u;
    connection_info.imei = NULL;
    mock_pointer_backend_test_state(&pointer_state);
    assert(pointer_state.connection_info_present == 1u);
    assert(pointer_state.connection_field_04 == 0xabcdef04u);
    assert(pointer_state.imei_present == 1u);
    assert(pointer_state.imei_length == 128u);
    assert(pointer_state.imei[0] == 'I');
    assert(pointer_state.imei[127] == 'I');
    assert(pointer_state.imei[128] == '\0');
    assert(pointer_state.msisdn_length == 9u);
    assert(strcmp(pointer_state.msisdn, "123456789") == 0);
    assert(pointer_state.sim_iccid_present == 0u);
    assert(((TestConnectionVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[20])[6])(
               first_interfaces[20], 0x2007u, 0xffffffffu, NULL) == 0u);

    memset(tef_filename, 'T', sizeof(tef_filename));
    assert(((TestTefVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[25])[21])(
               first_interfaces[25], 0x2521u, 0xffffffffu,
               tef_filename) == 0u);
    memset(tef_filename, 'X', sizeof(tef_filename));
    mock_pointer_backend_test_state(&pointer_state);
    assert(pointer_state.tef_present == 1u);
    assert(pointer_state.tef_length == 512u);
    assert(pointer_state.tef_filename[0] == 'T');
    assert(pointer_state.tef_filename[511] == 'T');
    assert(pointer_state.tef_filename[512] == '\0');
    assert(((TestTefVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[25])[21])(
               first_interfaces[25], 0x2522u, 0u, NULL) == 0u);
    mock_pointer_backend_test_state(&pointer_state);
    assert(pointer_state.generation == 14u);
    assert(pointer_state.logging_calls == 3u);
    assert(pointer_state.intl_calls == 2u);
    assert(pointer_state.vin_calls == 2u);
    assert(pointer_state.transport_calls == 3u);
    assert(pointer_state.connection_calls == 2u);
    assert(pointer_state.tef_calls == 2u);

    /* Generic neighbor slots do not affect pointer telemetry. */
    assert((*(const PanaGenericVirtual **)first_interfaces[6])[4](
               first_interfaces[6], 0x1234u) == 0u);
    mock_pointer_backend_test_state(&pointer_state);
    assert(pointer_state.generation == 14u);

    for (slot = 0u; slot < 15u; ++slot) {
        si_vtable[slot] = test_proxy_noop;
        hmi_vtable[slot] = test_proxy_noop;
    }
    for (slot = 0u; slot < 26u; ++slot) {
        adapter_ready_vtable[slot] = test_proxy_noop;
    }
    adapter_ready_vtable[2] = test_adapter_ready;
    adapter_ready_vtable[3] = test_adapter_slot3;
    adapter_ready_vtable[4] = test_adapter_slot4;
    adapter_ready_vtable[5] = (PanaGenericVirtual)test_adapter_slot5;
    adapter_ready_vtable[6] = test_adapter_slot6;
    adapter_ready_vtable[7] = (PanaGenericVirtual)test_adapter_slot7;
    adapter_ready_vtable[8] = test_adapter_slot8;
    adapter_ready_vtable[9] = test_adapter_slot9;
    adapter_ready_vtable[10] = (PanaGenericVirtual)test_adapter_slot10;
    adapter_ready_vtable[11] = (PanaGenericVirtual)test_adapter_slot11;
    adapter_ready_vtable[19] = (PanaGenericVirtual)test_adapter_slot19;
    adapter_ready_vtable[23] = (PanaGenericVirtual)test_adapter_slot23;
    si_vtable[12] = test_si_ready;
    si_vtable[10] = (PanaGenericVirtual)test_thread_status;
    hmi_vtable[2] = test_hmi_ready;
    hmi_vtable[3] = test_subscribe;
    hmi_vtable[6] = test_request_screen_window_identifier;
    hmi_vtable[7] = test_request_hmi_state;
    hmi_vtable[9] = test_transition_reply;
    hmi_vtable[10] = (PanaGenericVirtual)test_reply_screen_change;
    hmi_vtable[12] = (PanaGenericVirtual)test_reply_screen_theme;
    hmi_vtable[14] = test_navigation_ready;
    si_proxy.vptr = si_vtable;
    hmi_proxy.vptr = hmi_vtable;
    hmi_interface = first_interfaces[1];
    for (adapter = 0u; adapter < TEST_ADAPTER_COUNT; ++adapter) {
        if (first_interfaces[adapter] != 0 && adapter != 1u) {
            adapter_ready_proxies[adapter].vptr = adapter_ready_vtable;
            adapter_ready_proxies[adapter].adapter_id = adapter;
            ((TestAdapterObject *)first_interfaces[adapter])->host_proxy =
                &adapter_ready_proxies[adapter];
        }
    }
    root->host_proxy = &si_proxy;

    mock_callback_test_set_next_handle(UINT32_MAX);
    assert(mock_callback_allocate_handle() == UINT32_MAX);
    assert(mock_callback_allocate_handle() == 1u);
    mock_callback_backend_reset();

    /* Availability is committed only after PAS accepts the callback. A
     * missing proxy and a nonzero status must both leave the state retryable. */
    ((TestAdapterObject *)first_interfaces[10])->host_proxy = NULL;
    assert(mock_callback_signal_audio_availability(2u) == 1u);
    assert(callback_count == 0u);
    ((TestAdapterObject *)first_interfaces[10])->host_proxy =
        &adapter_ready_proxies[10];
    audio_callback_status = 5u;
    assert(mock_callback_signal_audio_availability(2u) == 5u);
    assert(callback_count == 1u);
    mock_callback_test_state(&callback_state);
    assert(callback_state.audio_transitions == 0u);
    audio_callback_status = 0u;
    assert(mock_callback_signal_audio_availability(2u) == 0u);
    assert(callback_count == 2u);
    mock_callback_test_state(&callback_state);
    assert(callback_state.audio_transitions == 1u);
    assert(mock_callback_signal_audio_availability(2u) == 0u);
    assert(callback_count == 2u);
    assert(mock_callback_signal_audio_availability(3u) == 0u);
    assert(callback_count == 3u);
    assert(mock_callback_audio_begin(4u, &older_audio_attempt) != 0);
    assert(mock_callback_audio_begin(5u, &newer_audio_attempt) != 0);
    mock_callback_audio_complete(5u, newer_audio_attempt, 1);
    mock_callback_audio_complete(4u, older_audio_attempt, 1);
    assert(mock_callback_signal_audio_availability(5u) == 0u);
    assert(callback_count == 3u);
    memset(cluster_record, 0x5a, sizeof(cluster_record));
    cluster_callback_status = 9u;
    assert(mock_callback_send_cluster(cluster_record) == 9u);
    mock_callback_test_state(&callback_state);
    assert(callback_state.cluster_generation == 0u);
    cluster_callback_status = 0u;
    assert(mock_callback_send_cluster(cluster_record) == 0u);
    memset(cluster_record, 0, sizeof(cluster_record));
    memset(thread_name, 'N', sizeof(thread_name));
    thread_name[1] = (char)0xc3;
    thread_name[sizeof(thread_name)-1u] = '\0';
    assert(mock_callback_signal_thread_status(77u,thread_name,3u) == 0u);
    mock_callback_set_voice_state(9u,7u);
    assert((*(const PanaGenericVirtual **)first_interfaces[25])[19](
               first_interfaces[25],0x252519u) == 0u);
    assert(voice_reply_count == 1u);
    assert(voice_reply_handle == 0x252519u);
    assert(voice_reply_result == 9u && voice_reply_status == 7u);
    mock_callback_test_state(&callback_state);
    assert(callback_state.audio_transitions == 3u);
    assert(callback_state.cluster_generation == 1u);
    assert(callback_state.cluster_record[0] == 0x5au);
    assert(callback_state.cluster_record[MOCK_CLUSTER_RECORD_SIZE-1u] == 0x5au);
    assert(callback_state.thread_generation == 1u);
    assert(strlen(callback_state.thread_name) == MOCK_THREAD_NAME_LIMIT);
    assert(callback_state.thread_name[1] == '?');
    assert(thread_callback_length == MOCK_THREAD_NAME_LIMIT);
    assert(callback_state.thread_name[MOCK_THREAD_NAME_LIMIT] == '\0');
    assert(callback_state.correlation_successes == 1u);
    assert(callback_state.optional_failures == 1u);
    mock_callback_record_status(13u,5u,5u,MOCK_CALLBACK_OPTIONAL);
    mock_callback_record_status(8u,5u,6u,MOCK_CALLBACK_CRITICAL);
    mock_callback_test_state(&callback_state);
    /* Raw PAS telemetry is accepted only while a callback transport lease is
     * held.  Direct backend calls cannot attach stale status to this epoch. */
    assert(callback_state.optional_failures == 1u);
    assert(callback_state.critical_failures == 1u);
    mock_callback_backend_reset();

    /* Match the observed stock registration state at signalStart.  SI
     * adapterReady is nested synchronously inside that call. */
    reset_observations(0u);
    root->host_proxy = &si_proxy;
    ((TestAdapterObject *)first_interfaces[1])->host_proxy = &hmi_proxy;
    assert(root->vptr->slot[4](root) == 0u);
    assert(event_count == 1u);
    assert(event_order[0] == 1u);

    /* Both adapter-ready notifications occur promptly and in SI/HMI order. */
    for (slot = 0u; slot < 180u && hmi_ready_count == 0u; ++slot) {
        test_delay(1u);
    }
    assert(event_count >= 2u);
    assert(event_order[0] == 1u);

    /* A transition requested synchronously from adapterReady remains queued
     * through subscribe and display initialization, then receives slot 9. */
    for (slot = 0u; slot < 1000u && transition_reply_count != 2u; ++slot) {
        test_delay(1u);
    }
    assert(callback_count == 10u);
    assert(callback_records[0].adapter == 8u && callback_records[0].slot == 3u);
    assert(callback_records[0].value_a == UINT32_MAX);
    assert(callback_records[1].adapter == 8u && callback_records[1].slot == 5u);
    assert(callback_records[2].adapter == 8u && callback_records[2].slot == 6u);
    assert(callback_records[3].adapter == 8u && callback_records[3].slot == 11u);
    assert(callback_records[4].adapter == 10u && callback_records[4].slot == 3u);
    assert(callback_records[4].value_a == 1u);
    assert(callback_records[4].value_b == 0x100u);
    assert(callback_records[5].adapter == 10u && callback_records[5].slot == 19u);
    assert(callback_records[5].value_b == 1u);
    assert(callback_records[6].adapter == 17u && callback_records[6].slot == 5u);
    assert(callback_records[6].value_a == UINT32_MAX);
    assert(callback_records[7].adapter == 17u && callback_records[7].slot == 6u);
    assert(callback_records[7].value_a == 0u);
    assert(callback_records[8].adapter == 17u && callback_records[8].slot == 7u);
    assert(callback_records[9].adapter == 9u && callback_records[9].slot == 3u);
    assert(callback_records[9].value_a == UINT32_MAX);
    assert(callback_records[1].value_a != 0u);
    assert(callback_records[2].value_a != 0u);
    assert(callback_records[3].value_a != 0u);
    assert(callback_records[5].value_a != 0u);
    assert(callback_records[8].value_a != 0u);
    assert(callback_records[1].value_a != callback_records[2].value_a);
    assert(callback_records[2].value_a != callback_records[3].value_a);
    assert(callback_records[3].value_a != callback_records[5].value_a);
    assert(callback_records[5].value_a != callback_records[8].value_a);
    request_handle = callback_records[1].value_a;
    assert(request_handle != 0u);
    assert(((TestAudioVirtual)
               (*(const PanaGenericVirtual **)first_interfaces[8])[3])(
               first_interfaces[8],request_handle,1u) == 0u);
    mock_callback_test_state(&callback_state);
    assert(callback_state.correlation_successes >= 1u);
    assert(subscription_count == 1u);
    assert(subscription_value == -1);
    assert(state_request_count == 3u);
    assert(window_request_count == 3u);
    assert(navigation_ready_count == 3u);
    assert(hmi_ready_count == 1u);
    for (slot = 0u; slot < 3u; ++slot) {
        assert(state_handles[slot] != 0u);
        if (slot != 0u) assert(state_handles[slot] != state_handles[slot-1u]);
        assert(state_displays[slot] == slot);
        assert(state_values[slot] == 1u);
        assert(window_handles[slot] == state_handles[slot]);
        assert(window_displays[slot] == slot);
        assert(navigation_displays[slot] == slot);
    }
    assert(transition_reply_count == 2u);
    assert(transition_reply_handles[0] == 0x12345678u);
    assert(transition_reply_handles[1] == 4u);
    assert(screen_change_reply_count == 1u);
    assert(screen_change_reply_handles[0] == 2u);
    assert(screen_change_reply_screens[0] == 2u);
    assert(screen_theme_reply_count == 1u);
    assert(screen_theme_reply_handles[0] == 3u);

    /* Exercise the recurrent away/back path. The provider must acknowledge
     * every transition and echo each newly generated screen/theme handle;
     * no startup-only fixed handle may suppress the second cycle. */
    assert((*(const PanaGenericVirtual **)hmi_interface)[5](
               hmi_interface, 5u, 0u, 2u, 2u,
               (const void *)(uintptr_t)0x5000u) == 0u);
    for (slot = 0u; slot < 1000u && transition_reply_count != 3u; ++slot) {
        test_delay(1u);
    }
    assert(transition_reply_handles[2] == 5u);
    assert((*(const PanaGenericVirtual **)hmi_interface)[9](
               hmi_interface, 6u, 0u, 2u, 0u, 0u) == 0u);
    assert((*(const PanaGenericVirtual **)hmi_interface)[10](
               hmi_interface, 8u,
               (const void *)(uintptr_t)0x56780000u) == 0u);
    for (slot = 0u; slot < 1000u && transition_reply_count != 4u; ++slot) {
        test_delay(1u);
    }
    assert(screen_change_reply_count == 2u);
    assert(screen_change_reply_handles[1] == 6u);
    assert(screen_change_reply_screens[1] == 2u);
    assert(screen_theme_reply_count == 2u);
    assert(screen_theme_reply_handles[1] == 8u);
    assert(transition_reply_handles[3] == 7u);
    assert(root->vptr->slot[4](root) == 0u);
    test_delay(300u);
    assert(mock_renderer_stub_create_count() == 1u);
    assert(mock_renderer_stub_destroy_count() == 0u);
    assert(navigation_ready_count == 3u);
    assert(hmi_ready_count == 1u);
    assert(transition_reply_count == 4u);
    assert(get_adapter(root, 0u, 0) != 0u);
    /* A callback-side teardown waits for the foreign callback, returns
     * without waiting on itself, then performs all reset/clear work from the
     * final outer release. */
    (void)pthread_mutex_lock(&cluster_block_lock);
    cluster_callback_block = 1;
    cluster_callback_entries = 0u;
    cluster_callback_entered = 0;
    cluster_callback_release = 0;
    cluster_callback_reentrant_deinitialize = 1;
    cluster_reentrant_deinitialize_entered = 0;
    cluster_reentrant_deinitialize_returned = 0;
    (void)pthread_mutex_unlock(&cluster_block_lock);
    assert(pthread_create(&lifecycle_cluster_thread, NULL,
                          test_cluster_send_thread,
                          &lifecycle_cluster_result) == 0);
    (void)pthread_mutex_lock(&cluster_block_lock);
    while (cluster_callback_entered == 0) {
        (void)pthread_cond_wait(&cluster_block_condition,
                                &cluster_block_lock);
    }
    (void)pthread_mutex_unlock(&cluster_block_lock);
    assert(pthread_create(&lifecycle_cluster_thread_second, NULL,
                          test_cluster_send_thread,
                          &lifecycle_cluster_result_second) == 0);
    (void)pthread_mutex_lock(&cluster_block_lock);
    while (cluster_reentrant_deinitialize_entered == 0) {
        (void)pthread_cond_wait(&cluster_block_condition,
                                &cluster_block_lock);
    }
    (void)pthread_mutex_unlock(&cluster_block_lock);
    test_delay(20u);
    (void)pthread_mutex_lock(&cluster_block_lock);
    assert(cluster_reentrant_deinitialize_returned == 0);
    cluster_callback_release = 1;
    (void)pthread_cond_broadcast(&cluster_block_condition);
    (void)pthread_mutex_unlock(&cluster_block_lock);
    assert(pthread_join(lifecycle_cluster_thread, NULL) == 0);
    assert(pthread_join(lifecycle_cluster_thread_second, NULL) == 0);
    assert(lifecycle_cluster_result == 0u);
    assert(lifecycle_cluster_result_second == 0u);
    assert(cluster_reentrant_deinitialize_returned == 1);
    cluster_callback_block = 0;
    cluster_callback_reentrant_deinitialize = 0;
    assert(root->host_proxy == NULL);
    assert(((TestAdapterObject *)first_interfaces[17])->host_proxy == NULL);
    memset(cluster_record, 0, sizeof(cluster_record));
    assert(mock_callback_send_cluster(cluster_record) == 1u);
    mock_scalar_backend_test_state(&scalar_state);
    assert(scalar_state.vehicle_generation == 0u);
    assert(scalar_state.gps_interval_generation == 0u);
    assert(scalar_state.audio_generation == 0u);
    assert(scalar_state.day_night_status_calls == 0u);
    assert(scalar_state.gps_interval_calls == 0u);
    assert(scalar_state.audio_calls == 0u);
    assert(mock_api_telemetry_test_get(8u, 3u, NULL) == 0);
    assert(mock_api_telemetry_test_get(20u, 3u, NULL) == 0);
    assert(mock_api_telemetry_test_get(31u, 5u, NULL) == 0);
    mock_pointer_backend_test_state(&pointer_state);
    assert(pointer_state.generation == 0u);
    assert(pointer_state.logging_calls == 0u);
    assert(pointer_state.connection_calls == 0u);
    assert(pointer_state.tef_calls == 0u);
    mock_callback_test_state(&callback_state);
    assert(callback_state.allocated_count == 0u);
    assert(callback_state.pending_count == 0u);
    assert(callback_state.callback_count == 0u);
    assert(callback_state.correlation_successes == 0u);
    assert(callback_state.correlation_failures == 0u);
    assert(callback_state.audio_transitions == 0u);
    assert(callback_state.cluster_generation == 0u);
    assert(callback_state.thread_generation == 0u);
    assert(mock_renderer_stub_create_count() == 1u);
    assert(mock_renderer_stub_destroy_count() == 1u);

    /* Synchronous production during adapterReady makes the ninth request
     * deterministically encounter the full eight-entry FIFO. */
    reset_observations(1u);
    assert(panaInitialize(&initialization) == 0u);
    root->host_proxy = &si_proxy;
    ((TestAdapterObject *)first_interfaces[1])->host_proxy = &hmi_proxy;
    for (adapter = 0u; adapter < TEST_ADAPTER_COUNT; ++adapter) {
        if (first_interfaces[adapter] != 0 && adapter != 1u) {
            ((TestAdapterObject *)first_interfaces[adapter])->host_proxy =
                &adapter_ready_proxies[adapter];
        }
    }
    assert(root->vptr->slot[4](root) == 0u);
    for (slot = 0u; slot < 1000u && transition_reply_count != 8u; ++slot) {
        test_delay(1u);
    }
    assert(overflow_rejected == 1u);
    assert(transition_reply_count == 8u);
    assert(subscription_count == 1u);
    assert(state_request_count == 3u);
    assert(window_request_count == 3u);
    assert(navigation_ready_count == 3u);
    assert(hmi_ready_count == 1u);
    for (slot = 0u; slot < 8u; ++slot) {
        assert(transition_reply_handles[slot] == 0x1000u + slot);
    }
    for (slot = 0u; slot < 3u; ++slot) {
        assert(state_handles[slot] != 0u);
        assert(window_handles[slot] == state_handles[slot]);
    }
    panaDeinitialize();
    assert(mock_renderer_stub_create_count() == 2u);
    assert(mock_renderer_stub_destroy_count() == 2u);

    /* Shutdown during the worker's scheduling defer joins it and prevents any
     * asynchronous host callback after deinitialization returns. */
    reset_observations(2u);
    assert(panaInitialize(&initialization) == 0u);
    root->host_proxy = &si_proxy;
    ((TestAdapterObject *)first_interfaces[1])->host_proxy = &hmi_proxy;
    for (adapter = 0u; adapter < TEST_ADAPTER_COUNT; ++adapter) {
        if (first_interfaces[adapter] != 0 && adapter != 1u) {
            ((TestAdapterObject *)first_interfaces[adapter])->host_proxy =
                &adapter_ready_proxies[adapter];
        }
    }
    assert(root->vptr->slot[4](root) == 0u);
    assert(event_count == 1u);
    assert(event_order[0] == 1u);
    panaDeinitialize();
    test_delay(300u);
    assert(event_count == 1u);
    assert(subscription_count == 0u);
    assert(navigation_ready_count == 0u);
    assert(hmi_ready_count == 0u);
    assert(transition_reply_count == 0u);
    assert(mock_renderer_stub_create_count() == 2u);
    assert(mock_renderer_stub_destroy_count() == 2u);

    /* A graphics failure is logged by the provider but does not truncate the
     * protocol worker or manufacture a renderer teardown. */
    reset_observations(2u);
    mock_renderer_stub_set_create_result(-7);
    assert(panaInitialize(&initialization) == 0u);
    root->host_proxy = &si_proxy;
    ((TestAdapterObject *)first_interfaces[1])->host_proxy = &hmi_proxy;
    for (adapter = 0u; adapter < TEST_ADAPTER_COUNT; ++adapter) {
        if (first_interfaces[adapter] != 0 && adapter != 1u) {
            ((TestAdapterObject *)first_interfaces[adapter])->host_proxy =
                &adapter_ready_proxies[adapter];
        }
    }
    assert(root->vptr->slot[4](root) == 0u);
    for (slot = 0u; slot < 1000u && hmi_ready_count == 0u; ++slot) {
        test_delay(1u);
    }
    assert(hmi_ready_count == 1u);
    assert(subscription_count == 1u);
    assert(navigation_ready_count == 3u);
    assert(mock_renderer_stub_create_count() == 3u);
    assert(mock_renderer_stub_destroy_count() == 2u);
    exercise_generated_static_slots(root, first_interfaces, 0u);
    panaDeinitialize();
    exercise_generated_static_slots(root, first_interfaces, 1u);
    assert(mock_renderer_stub_create_count() == 3u);
    assert(mock_renderer_stub_destroy_count() == 2u);
    mock_renderer_stub_set_create_result(0);

    panaDeinitialize();
    return 0;
}
