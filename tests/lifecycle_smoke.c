#define _POSIX_C_SOURCE 200112L
#include "mock_api_telemetry.h"
#include "mock_callback_backend.h"
#include <open_navicore/cluster.h>
#include "mock_gps_backend.h"
#include "mock_navicore.h"
#include "mock_pointer_backend.h"
#include "mock_renderer.h"
#include "mock_scalar_backend.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

enum { TEST_ADAPTER_COUNT = 256, TEST_VTABLE_SLOTS = 32 };

typedef struct {
    const PanaGenericVirtual *vptr;
    void *host_proxy;
    uint32_t adapter_id;
} TestAdapterObject;

typedef struct {
    const PanaGenericVirtual *vptr;
    int action;
    uint32_t callback_status;
} TestProxy;

enum TestAction {
    ACTION_NONE = 0,
    ACTION_DEINITIALIZE = 1,
    ACTION_INITIALIZE = 2,
    ACTION_BLOCK = 3
};

enum ProducerKind {
    PRODUCER_AUDIO = 1,
    PRODUCER_THREAD = 2,
    PRODUCER_CLUSTER = 3
};

typedef uint32_t (*TestGetAdapter)(void *, uint32_t, void **);
typedef uint32_t (*TestByteCall)(void *, uint8_t);
typedef uint32_t (*TestU32Call)(void *, uint32_t);
typedef uint32_t (*TestU32ByteCall)(void *, uint32_t, uint8_t);
typedef uint32_t (*TestU32U32ByteCall)(void *, uint32_t, uint32_t, uint8_t);
typedef uint32_t (*TestFloatCall)(void *, float);
typedef uint32_t (*TestPointerCall)(void *, uint32_t, const void *);
typedef uint32_t (*TestGpsCall)(void *, const void *);
typedef uint32_t (*TestHmiTransitionCall)(void *, uint32_t, uint32_t,
                                          uint32_t, uint32_t, const void *);
typedef uint32_t (*TestHmiScreenCall)(void *, uint32_t, uint32_t, uint32_t,
                                      uint32_t, uint32_t);

static PanaInitializationData g_initialization;
static PanaRootObject *g_root;
static void *g_interfaces[TEST_ADAPTER_COUNT];
static PanaGenericVirtual g_root_vtable[TEST_VTABLE_SLOTS];
static PanaGenericVirtual g_hmi_vtable[TEST_VTABLE_SLOTS];
static PanaGenericVirtual g_adapter_vtable[TEST_VTABLE_SLOTS];
static TestProxy g_root_proxy;
static TestProxy g_hmi_proxy;
static TestProxy g_adapter_proxies[TEST_ADAPTER_COUNT];

static pthread_mutex_t g_hook_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_hook_condition = PTHREAD_COND_INITIALIZER;
static int g_action_entered;
static int g_action_returned;
static int g_block_release;
static uint32_t g_reentrant_initialize_result;
static uint32_t g_hmi_ready_calls;
static uint32_t g_transition_calls;
static int g_deinitialize_thread_returned;
static uint32_t g_v2_vehicle_calls;
static uint32_t g_v2_gps_interval_calls;
static uint32_t g_v2_audio_calls;
static MockNavBackendVehicleV2 g_v2_vehicle;
static MockNavBackendGpsIntervalV2 g_v2_gps_interval;
static MockNavBackendAudioV2 g_v2_audio;
static int g_v2_deinitialize_on_audio;
static int g_v2_deinitialize_returned;
static MockNavBackendServicesV2 g_v3_services;
static uint32_t g_v3_start_calls;
static uint32_t g_v4_internationalization_calls;
static uint32_t g_v4_cluster_display_calls;
static MockNavBackendInternationalizationV1 g_v4_internationalization;
static MockNavBackendClusterDisplayV1 g_v4_cluster_display;
static uint32_t g_v5_driver_distraction_calls;
static MockNavBackendDriverDistractionV1 g_v5_driver_distraction;

static int v3_start_callback(void *context,
                             const MockNavBackendServicesV2 *services)
{
    (void)context;
    assert(services != NULL);
    assert(services->v1.abi_version == MOCK_NAV_BACKEND_ABI_V2);
    assert(services->v1.struct_size == sizeof(*services));
    assert(services->publish_cluster(
               services->v1.runtime_context, NULL) ==
           MOCK_NAV_BACKEND_SERVICE_STALE);
    (void)pthread_mutex_lock(&g_hook_lock);
    g_v3_services = *services;
    ++g_v3_start_calls;
    (void)pthread_cond_broadcast(&g_hook_condition);
    (void)pthread_mutex_unlock(&g_hook_lock);
    return 0;
}

static void delay_ms(unsigned int milliseconds)
{
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000u);
    delay.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    (void)nanosleep(&delay, NULL);
}

static void reset_hook(void)
{
    (void)pthread_mutex_lock(&g_hook_lock);
    g_action_entered = 0;
    g_action_returned = 0;
    g_block_release = 0;
    g_reentrant_initialize_result = UINT32_MAX;
    g_hmi_ready_calls = 0u;
    g_transition_calls = 0u;
    g_deinitialize_thread_returned = 0;
    (void)pthread_mutex_unlock(&g_hook_lock);
}

static uint32_t run_action(TestProxy *proxy)
{
    int action = proxy->action;

    if (action == ACTION_NONE) {
        return proxy->callback_status;
    }
    (void)pthread_mutex_lock(&g_hook_lock);
    g_action_entered = 1;
    (void)pthread_cond_broadcast(&g_hook_condition);
    if (action == ACTION_BLOCK) {
        while (g_block_release == 0) {
            (void)pthread_cond_wait(&g_hook_condition, &g_hook_lock);
        }
    }
    (void)pthread_mutex_unlock(&g_hook_lock);

    if (action == ACTION_DEINITIALIZE) {
        panaDeinitialize();
    } else if (action == ACTION_INITIALIZE) {
        g_reentrant_initialize_result = panaInitialize(&g_initialization);
    }

    (void)pthread_mutex_lock(&g_hook_lock);
    g_action_returned = 1;
    (void)pthread_cond_broadcast(&g_hook_condition);
    (void)pthread_mutex_unlock(&g_hook_lock);
    return proxy->callback_status;
}

static uint32_t action_callback(void *self, ...)
{
    return run_action((TestProxy *)self);
}

static uint32_t hmi_ready_callback(void *self, ...)
{
    (void)pthread_mutex_lock(&g_hook_lock);
    ++g_hmi_ready_calls;
    (void)pthread_cond_broadcast(&g_hook_condition);
    (void)pthread_mutex_unlock(&g_hook_lock);
    return run_action((TestProxy *)self);
}

static uint32_t transition_callback(void *self, ...)
{
    (void)pthread_mutex_lock(&g_hook_lock);
    ++g_transition_calls;
    (void)pthread_cond_broadcast(&g_hook_condition);
    (void)pthread_mutex_unlock(&g_hook_lock);
    return ((TestProxy *)self)->callback_status;
}

static int v2_vehicle_callback(void *context,
                               const MockNavBackendVehicleV2 *vehicle)
{
    (void)context;
    (void)pthread_mutex_lock(&g_hook_lock);
    g_v2_vehicle = *vehicle;
    ++g_v2_vehicle_calls;
    (void)pthread_cond_broadcast(&g_hook_condition);
    (void)pthread_mutex_unlock(&g_hook_lock);
    return 0;
}

static int v2_gps_interval_callback(
    void *context, const MockNavBackendGpsIntervalV2 *gps_interval)
{
    (void)context;
    (void)pthread_mutex_lock(&g_hook_lock);
    g_v2_gps_interval = *gps_interval;
    ++g_v2_gps_interval_calls;
    (void)pthread_cond_broadcast(&g_hook_condition);
    (void)pthread_mutex_unlock(&g_hook_lock);
    return 0;
}

static int v2_audio_callback(void *context,
                             const MockNavBackendAudioV2 *audio)
{
    int deinitialize;
    (void)context;
    (void)pthread_mutex_lock(&g_hook_lock);
    g_v2_audio = *audio;
    ++g_v2_audio_calls;
    deinitialize = g_v2_deinitialize_on_audio;
    (void)pthread_cond_broadcast(&g_hook_condition);
    (void)pthread_mutex_unlock(&g_hook_lock);
    if (deinitialize != 0) {
        panaDeinitialize();
        (void)pthread_mutex_lock(&g_hook_lock);
        g_v2_deinitialize_returned = 1;
        (void)pthread_cond_broadcast(&g_hook_condition);
        (void)pthread_mutex_unlock(&g_hook_lock);
    }
    return 0;
}

static int v4_internationalization_callback(
    void *context, const MockNavBackendInternationalizationV1 *event)
{
    (void)context;
    (void)pthread_mutex_lock(&g_hook_lock);
    g_v4_internationalization = *event;
    ++g_v4_internationalization_calls;
    (void)pthread_cond_broadcast(&g_hook_condition);
    (void)pthread_mutex_unlock(&g_hook_lock);
    return 0;
}

static int v4_cluster_display_callback(
    void *context, const MockNavBackendClusterDisplayV1 *event)
{
    (void)context;
    (void)pthread_mutex_lock(&g_hook_lock);
    g_v4_cluster_display = *event;
    ++g_v4_cluster_display_calls;
    (void)pthread_cond_broadcast(&g_hook_condition);
    (void)pthread_mutex_unlock(&g_hook_lock);
    return 0;
}

static int v5_driver_distraction_callback(
    void *context, const MockNavBackendDriverDistractionV1 *event)
{
    (void)context;
    (void)pthread_mutex_lock(&g_hook_lock);
    g_v5_driver_distraction = *event;
    ++g_v5_driver_distraction_calls;
    (void)pthread_cond_broadcast(&g_hook_condition);
    (void)pthread_mutex_unlock(&g_hook_lock);
    return 0;
}

static void wait_for_int(const int *value)
{
    unsigned int attempt;
    for (attempt = 0u; attempt < 3000u; ++attempt) {
        int ready;
        (void)pthread_mutex_lock(&g_hook_lock);
        ready = *value != 0;
        (void)pthread_mutex_unlock(&g_hook_lock);
        if (ready) return;
        delay_ms(1u);
    }
    assert(!"timed out waiting for lifecycle event");
}

static void wait_for_count(const uint32_t *value, uint32_t expected)
{
    unsigned int attempt;
    for (attempt = 0u; attempt < 3000u; ++attempt) {
        uint32_t observed;
        (void)pthread_mutex_lock(&g_hook_lock);
        observed = *value;
        (void)pthread_mutex_unlock(&g_hook_lock);
        if (observed >= expected) return;
        delay_ms(1u);
    }
    (void)fprintf(stderr,
                  "callback timeout target=%p expected=%u observed=%u "
                  "v2=%u/%u/%u v3=%u v4=%u/%u v5=%u hmi=%u transition=%u\n",
                  (const void *)value, expected, *value,
                  g_v2_vehicle_calls, g_v2_gps_interval_calls,
                  g_v2_audio_calls, g_v3_start_calls,
                  g_v4_internationalization_calls,
                  g_v4_cluster_display_calls,
                  g_v5_driver_distraction_calls, g_hmi_ready_calls,
                  g_transition_calls);
    assert(!"timed out waiting for callback count");
}

static void begin_lifecycle(void)
{
    TestGetAdapter get_adapter;
    uint32_t adapter;

    assert(panaInitialize(&g_initialization) == 0u);
    g_root = (PanaRootObject *)panaGetSoftwareInfrastructure();
    assert(g_root != NULL);
    get_adapter = (TestGetAdapter)g_root->vptr->slot[3];
    for (adapter = 0u; adapter < TEST_ADAPTER_COUNT; ++adapter) {
        void *value = NULL;
        uint32_t status = get_adapter(g_root, adapter, &value);
        if (adapter == 21u || adapter == 27u || adapter == 28u) {
            assert(status == 1u && value == NULL);
        } else {
            assert(status == 0u && value != NULL);
            g_interfaces[adapter] = value;
        }
    }
}

static void install_worker_proxies(int deinitialize_adapter,
                                   int deinitialize_hmi)
{
    uint32_t index;

    for (index = 0u; index < TEST_VTABLE_SLOTS; ++index) {
        g_root_vtable[index] = action_callback;
        g_hmi_vtable[index] = action_callback;
        g_adapter_vtable[index] = action_callback;
    }
    g_hmi_vtable[2] = hmi_ready_callback;
    g_hmi_vtable[9] = transition_callback;
    g_root_proxy.vptr = g_root_vtable;
    g_root_proxy.action = ACTION_NONE;
    g_root_proxy.callback_status = 0u;
    g_hmi_proxy.vptr = g_hmi_vtable;
    g_hmi_proxy.action = deinitialize_hmi ? ACTION_DEINITIALIZE : ACTION_NONE;
    g_hmi_proxy.callback_status = 0u;
    g_root->host_proxy = &g_root_proxy;
    for (index = 0u; index < TEST_ADAPTER_COUNT; ++index) {
        if (g_interfaces[index] != NULL) {
            g_adapter_proxies[index].vptr = g_adapter_vtable;
            g_adapter_proxies[index].action =
                (int)index == deinitialize_adapter
                    ? ACTION_DEINITIALIZE : ACTION_NONE;
            g_adapter_proxies[index].callback_status = 0u;
            ((TestAdapterObject *)g_interfaces[index])->host_proxy =
                index == 1u ? (void *)&g_hmi_proxy
                            : (void *)&g_adapter_proxies[index];
        }
    }
}

static void assert_backend_state_zero(void)
{
    MockScalarBackendTestState scalar;
    MockPointerBackendTestState pointer;
    MockCallbackTestState callback;

    mock_scalar_backend_test_state(&scalar);
    assert(scalar.vehicle_generation == 0u);
    assert(scalar.gps_interval_generation == 0u);
    assert(scalar.audio_generation == 0u);
    mock_pointer_backend_test_state(&pointer);
    assert(pointer.generation == 0u);
    mock_callback_test_state(&callback);
    assert(callback.allocated_count == 0u);
    assert(callback.pending_count == 0u);
    assert(callback.callback_count == 0u);
    assert(callback.correlation_successes == 0u);
    assert(callback.correlation_failures == 0u);
    assert(callback.audio_transitions == 0u);
    assert(callback.cluster_generation == 0u);
    assert(callback.thread_generation == 0u);
    assert(mock_gps_test_call_count() == 0u);
    assert(mock_gps_backend_test_generation() == 0u);
}

static void *deinitialize_thread(void *unused)
{
    (void)unused;
    panaDeinitialize();
    (void)pthread_mutex_lock(&g_hook_lock);
    g_deinitialize_thread_returned = 1;
    (void)pthread_cond_broadcast(&g_hook_condition);
    (void)pthread_mutex_unlock(&g_hook_lock);
    return NULL;
}

static void *producer_thread(void *opaque)
{
    enum ProducerKind kind = *(const enum ProducerKind *)opaque;
    uint8_t record[MOCK_CLUSTER_RECORD_SIZE];
    MockClusterNavigationState state;

    if (kind == PRODUCER_AUDIO) {
        (void)mock_callback_signal_audio_availability(7u);
    } else if (kind == PRODUCER_THREAD) {
        (void)mock_callback_signal_thread_status(77u, "worker", 3u);
    } else {
        memset(&state, 0, sizeof(state));
        state.valid_attributes = MOCK_CLUSTER_NAVIGATION_STATUS;
        state.navigation_status = MOCK_CLUSTER_NAV_NO_ROUTE;
        memset(record, 0, sizeof(record));
        (void)record;
        (void)mock_cluster_backend_publish(&state);
    }
    return NULL;
}

static void exercise_producer_race(enum ProducerKind kind, uint32_t adapter,
                                   uint32_t slot)
{
    pthread_t producer;
    pthread_t deinitializer;
    TestProxy proxy;
    PanaGenericVirtual vtable[TEST_VTABLE_SLOTS];
    unsigned int index;
    unsigned int attempt;
    uint32_t stopping_status = 0u;

    begin_lifecycle();
    reset_hook();
    for (index = 0u; index < TEST_VTABLE_SLOTS; ++index) {
        vtable[index] = action_callback;
    }
    proxy.vptr = vtable;
    proxy.action = ACTION_BLOCK;
    proxy.callback_status = 0u;
    if (adapter == UINT32_MAX) g_root->host_proxy = &proxy;
    else ((TestAdapterObject *)g_interfaces[adapter])->host_proxy = &proxy;
    (void)slot;
    assert(pthread_create(&producer, NULL, producer_thread, &kind) == 0);
    wait_for_int(&g_action_entered);
    assert(pthread_create(&deinitializer, NULL, deinitialize_thread, NULL) == 0);
    for (attempt = 0u; attempt < 1000u; ++attempt) {
        stopping_status = ((TestByteCall)
            (*(const PanaGenericVirtual **)g_interfaces[8])[4])(
                g_interfaces[8], 1u);
        if (stopping_status == 1u) break;
        delay_ms(1u);
    }
    assert(stopping_status == 1u);
    assert(((TestPointerCall)
               (*(const PanaGenericVirtual **)g_interfaces[7])[3])(
               g_interfaces[7], 1u, NULL) == 1u);
    assert(((TestHmiTransitionCall)
               (*(const PanaGenericVirtual **)g_interfaces[1])[5])(
               g_interfaces[1], 1u, 0u, 1u, 1u, NULL) == 1u);
    (void)pthread_mutex_lock(&g_hook_lock);
    assert(g_deinitialize_thread_returned == 0);
    g_block_release = 1;
    (void)pthread_cond_broadcast(&g_hook_condition);
    (void)pthread_mutex_unlock(&g_hook_lock);
    assert(pthread_join(producer, NULL) == 0);
    assert(pthread_join(deinitializer, NULL) == 0);
    assert_backend_state_zero();
}

static void test_direct_callback_deinitialize(uint32_t adapter,
                                              uint32_t slot,
                                              enum ProducerKind kind)
{
    TestProxy proxy;
    PanaGenericVirtual vtable[TEST_VTABLE_SLOTS];
    unsigned int index;
    uint8_t record[MOCK_CLUSTER_RECORD_SIZE];

    begin_lifecycle();
    reset_hook();
    for (index = 0u; index < TEST_VTABLE_SLOTS; ++index) {
        vtable[index] = action_callback;
    }
    proxy.vptr = vtable;
    proxy.action = ACTION_DEINITIALIZE;
    proxy.callback_status = 0u;
    if (adapter == UINT32_MAX) g_root->host_proxy = &proxy;
    else ((TestAdapterObject *)g_interfaces[adapter])->host_proxy = &proxy;
    (void)slot;
    if (kind == PRODUCER_AUDIO) {
        (void)mock_callback_signal_audio_availability(2u);
    } else if (kind == PRODUCER_THREAD) {
        (void)mock_callback_signal_thread_status(77u, "thread", 3u);
    } else {
        memset(record, 0, sizeof(record));
        (void)mock_callback_send_cluster(record);
    }
    wait_for_int(&g_action_returned);
    panaDeinitialize();
    assert(panaGetSoftwareInfrastructure() == NULL);
    assert_backend_state_zero();
}

static void test_provider_v2_bridge(void)
{
    MockNavBackendV2 backend;
    MockNavBackendVehicleV2 vehicle;
    MockNavBackendGpsIntervalV2 gps_interval;
    MockNavBackendAudioV2 audio;
    char diagnostic[257];
    uint32_t speed_bits;

    memset(&backend, 0, sizeof(backend));
    backend.v1.abi_version = MOCK_NAV_BACKEND_ABI_V2;
    backend.v1.struct_size = sizeof(backend);
    backend.vehicle_changed = v2_vehicle_callback;
    backend.gps_interval_changed = v2_gps_interval_callback;
    backend.audio_changed = v2_audio_callback;
    (void)pthread_mutex_lock(&g_hook_lock);
    g_v2_vehicle_calls = 0u;
    g_v2_gps_interval_calls = 0u;
    g_v2_audio_calls = 0u;
    g_v2_deinitialize_on_audio = 0;
    g_v2_deinitialize_returned = 0;
    memset(&g_v2_vehicle, 0, sizeof(g_v2_vehicle));
    memset(&g_v2_gps_interval, 0, sizeof(g_v2_gps_interval));
    memset(&g_v2_audio, 0, sizeof(g_v2_audio));
    (void)pthread_mutex_unlock(&g_hook_lock);

    assert(mock_navicore_backend_install_v2(&backend) == 0);
    begin_lifecycle();
    assert(mock_renderer_create(diagnostic, sizeof(diagnostic)) == 0);

    assert(((TestU32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[3])(
               g_interfaces[8], 0x8300u, 1u) == 0u);
    wait_for_count(&g_v2_vehicle_calls, 1u);
    (void)pthread_mutex_lock(&g_hook_lock);
    vehicle = g_v2_vehicle;
    (void)pthread_mutex_unlock(&g_hook_lock);
    assert(vehicle.source_slot == 3u && vehicle.request_handle == 0x8300u);
    assert(vehicle.day_night_status == 1u);

    assert(((TestByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[4])(
               g_interfaces[8], 2u) == 0u);
    wait_for_count(&g_v2_vehicle_calls, 2u);
    assert(((TestU32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[5])(
               g_interfaces[8], 0x8500u, 3u) == 0u);
    wait_for_count(&g_v2_vehicle_calls, 3u);
    assert(((TestByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[6])(
               g_interfaces[8], 4u) == 0u);
    wait_for_count(&g_v2_vehicle_calls, 4u);
    assert(((TestFloatCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[8])(
               g_interfaces[8], -13.25f) == 0u);
    wait_for_count(&g_v2_vehicle_calls, 5u);
    memcpy(&speed_bits, &(float){ -13.25f }, sizeof(speed_bits));
    (void)pthread_mutex_lock(&g_hook_lock);
    vehicle = g_v2_vehicle;
    (void)pthread_mutex_unlock(&g_hook_lock);
    assert(vehicle.source_slot == 8u && vehicle.request_handle == 0u);
    assert(vehicle.valid_fields ==
           (MOCK_NAV_BACKEND_VEHICLE_DAY_NIGHT_STATUS |
            MOCK_NAV_BACKEND_VEHICLE_DAY_NIGHT_SETTING |
            MOCK_NAV_BACKEND_VEHICLE_SPEED));
    assert(vehicle.displayed_vehicle_speed_bits == speed_bits);

    assert(((TestU32Call)
               (*(const PanaGenericVirtual **)g_interfaces[9])[6])(
               g_interfaces[9], 1000u) == 0u);
    wait_for_count(&g_v2_gps_interval_calls, 1u);
    (void)pthread_mutex_lock(&g_hook_lock);
    gps_interval = g_v2_gps_interval;
    (void)pthread_mutex_unlock(&g_hook_lock);
    assert(gps_interval.source_slot == 6u && gps_interval.interval_ms == 1000u);

    assert(((TestU32U32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[10])[17])(
               g_interfaces[10], 0xa017u, 1u, 15u) == 0u);
    wait_for_count(&g_v2_audio_calls, 1u);
    (void)pthread_mutex_lock(&g_hook_lock);
    audio = g_v2_audio;
    (void)pthread_mutex_unlock(&g_hook_lock);
    assert(audio.source_slot == 17u && audio.request_handle == 0xa017u);
    assert(audio.audio_source == 1u && audio.volume == 15u);

    (void)pthread_mutex_lock(&g_hook_lock);
    g_v2_deinitialize_on_audio = 1;
    (void)pthread_mutex_unlock(&g_hook_lock);
    assert(((TestU32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[10])[19])(
               g_interfaces[10], 2u, 16u) == 0u);
    wait_for_count(&g_v2_audio_calls, 2u);
    wait_for_int(&g_v2_deinitialize_returned);
    (void)pthread_mutex_lock(&g_hook_lock);
    audio = g_v2_audio;
    (void)pthread_mutex_unlock(&g_hook_lock);
    assert(audio.source_slot == 19u && audio.request_handle == 0u);
    assert(audio.audio_source == 2u && audio.volume == 16u);
    mock_renderer_destroy();
    assert(panaGetSoftwareInfrastructure() == NULL);
    assert(mock_navicore_backend_install_v2(NULL) == 0);
    assert_backend_state_zero();
}

static void test_provider_v3_outbound_bridge(void)
{
    MockNavBackendV3 backend;
    MockNavBackendServicesV2 services;
    MockNavBackendClusterV1 cluster;
    MockNavBackendAudioAvailabilityV1 audio;
    MockNavBackendThreadStatusV1 thread_status;
    MockCallbackTestState callback;
    char diagnostic[257];
    char current_road[] = "Road";
    char long_name[64];

    memset(&backend, 0, sizeof(backend));
    backend.v2.v1.abi_version = MOCK_NAV_BACKEND_ABI_V3;
    backend.v2.v1.struct_size = sizeof(backend);
    backend.start_v2 = v3_start_callback;
    g_v3_start_calls = 0u;
    memset(&g_v3_services, 0, sizeof(g_v3_services));
    assert(mock_navicore_backend_install_v3(&backend) == 0);
    begin_lifecycle();
    install_worker_proxies(-1, 0);
    assert(mock_renderer_create(diagnostic, sizeof(diagnostic)) == 0);
    wait_for_count(&g_v3_start_calls, 1u);
    (void)pthread_mutex_lock(&g_hook_lock);
    services = g_v3_services;
    (void)pthread_mutex_unlock(&g_hook_lock);

    memset(&cluster, 0, sizeof(cluster));
    assert(services.publish_cluster(services.v1.runtime_context, &cluster) ==
           MOCK_NAV_BACKEND_SERVICE_BAD_ABI);
    cluster.abi_version = MOCK_NAV_BACKEND_PAYLOAD_ABI_V1;
    cluster.struct_size = sizeof(cluster);
    cluster.valid_attributes = MOCK_CLUSTER_ROUTE_SYMBOL;
    assert(services.publish_cluster(services.v1.runtime_context, &cluster) ==
           MOCK_CLUSTER_UNSUPPORTED_FIELD);
    cluster.valid_attributes = MOCK_CLUSTER_LANE;
    assert(services.publish_cluster(services.v1.runtime_context, &cluster) ==
           MOCK_CLUSTER_UNSUPPORTED_FIELD);
    cluster.valid_attributes = MOCK_CLUSTER_UNKNOWN_12;
    assert(services.publish_cluster(services.v1.runtime_context, &cluster) ==
           MOCK_CLUSTER_UNSUPPORTED_FIELD);
    memset(&cluster, 0, sizeof(cluster));
    cluster.abi_version = MOCK_NAV_BACKEND_PAYLOAD_ABI_V1;
    cluster.struct_size = sizeof(cluster);
    cluster.valid_attributes =
        MOCK_CLUSTER_ROUTE_GUIDANCE_STATE |
        MOCK_CLUSTER_NAVIGATION_STATUS |
        MOCK_CLUSTER_CURRENT_ROAD;
    cluster.route_guidance_state = MOCK_CLUSTER_ROUTE_FOLLOW_STREET;
    cluster.navigation_status = MOCK_CLUSTER_NAV_NO_ROUTE;
    cluster.current_road.bytes = current_road;
    cluster.current_road.byte_count = 4u;
    assert(services.publish_cluster(services.v1.runtime_context, &cluster) ==
           0u);
    current_road[0] = 'X';
    mock_callback_test_state(&callback);
    assert(callback.cluster_generation == 1u);
    assert(callback.cluster_record[0x150] == 'R');
    g_adapter_proxies[17].callback_status = 9u;
    cluster.navigation_status = MOCK_CLUSTER_NAV_ROUTE_CALCULATION;
    assert(services.publish_cluster(services.v1.runtime_context, &cluster) ==
           9u);
    mock_callback_test_state(&callback);
    assert(callback.cluster_generation == 1u);
    g_adapter_proxies[17].callback_status = 0u;
    assert(services.publish_cluster(services.v1.runtime_context, &cluster) ==
           0u);
    mock_callback_test_state(&callback);
    assert(callback.cluster_generation == 2u);
    assert((uint16_t)(callback.cluster_record[0xa56] |
                      ((uint16_t)callback.cluster_record[0xa57] << 8)) ==
           (MOCK_CLUSTER_NAVIGATION_STATUS |
            MOCK_CLUSTER_CURRENT_ROAD));

    memset(&audio, 0, sizeof(audio));
    audio.abi_version = MOCK_NAV_BACKEND_PAYLOAD_ABI_V1;
    audio.struct_size = sizeof(audio);
    audio.availability_state = 0u;
    assert(services.publish_audio_availability(
               services.v1.runtime_context, &audio) ==
           MOCK_NAV_BACKEND_SERVICE_BAD_VALUE);
    audio.availability_state = 1u;
    assert(services.publish_audio_availability(
               services.v1.runtime_context, &audio) == 0u);
    assert(services.publish_audio_availability(
               services.v1.runtime_context, &audio) == 0u);
    mock_callback_test_state(&callback);
    assert(callback.audio_transitions == 1u);
    g_adapter_proxies[10].callback_status = 5u;
    audio.availability_state = 2u;
    assert(services.publish_audio_availability(
               services.v1.runtime_context, &audio) == 5u);
    g_adapter_proxies[10].callback_status = 0u;
    assert(services.publish_audio_availability(
               services.v1.runtime_context, &audio) == 0u);
    mock_callback_test_state(&callback);
    assert(callback.audio_transitions == 2u);

    memset(&thread_status, 0, sizeof(thread_status));
    thread_status.abi_version = MOCK_NAV_BACKEND_PAYLOAD_ABI_V1;
    thread_status.struct_size = sizeof(thread_status);
    thread_status.thread_id = 77u;
    thread_status.thread_status = 3u;
    thread_status.name_ascii = "bad\200";
    thread_status.name_byte_count = 4u;
    assert(services.publish_thread_status(
               services.v1.runtime_context, &thread_status) ==
           MOCK_NAV_BACKEND_SERVICE_BAD_VALUE);
    memset(long_name, 'a', sizeof(long_name));
    thread_status.name_ascii = long_name;
    thread_status.name_byte_count = 64u;
    assert(services.publish_thread_status(
               services.v1.runtime_context, &thread_status) ==
           MOCK_NAV_BACKEND_SERVICE_BAD_VALUE);
    thread_status.name_ascii = "worker";
    thread_status.name_byte_count = 6u;
    assert(services.publish_thread_status(
               services.v1.runtime_context, &thread_status) == 0u);
    mock_callback_test_state(&callback);
    assert(callback.thread_generation == 1u);
    assert(strcmp(callback.thread_name, "worker") == 0);

    panaDeinitialize();
    assert(services.publish_cluster(services.v1.runtime_context, &cluster) ==
           MOCK_NAV_BACKEND_SERVICE_STALE);
    assert(mock_navicore_backend_install_v3(NULL) == 0);
    assert_backend_state_zero();
}

static void test_provider_v4_recovered_inputs(void)
{
    MockNavBackendV4 backend;
    MockNavBackendInternationalizationV1 internationalization;
    MockNavBackendClusterDisplayV1 cluster_display;
    uint8_t settings[4] = {0x34u, 0x12u, 0xa5u, 0x5au};
    char diagnostic[257];

    memset(&backend, 0, sizeof(backend));
    backend.v3.v2.v1.abi_version = MOCK_NAV_BACKEND_ABI_V4;
    backend.v3.v2.v1.struct_size = sizeof(backend);
    backend.v3.start_v2 = v3_start_callback;
    backend.internationalization_changed =
        v4_internationalization_callback;
    backend.cluster_display_changed = v4_cluster_display_callback;
    (void)pthread_mutex_lock(&g_hook_lock);
    g_v3_start_calls = 0u;
    g_v4_internationalization_calls = 0u;
    g_v4_cluster_display_calls = 0u;
    memset(&g_v4_internationalization, 0,
           sizeof(g_v4_internationalization));
    memset(&g_v4_cluster_display, 0, sizeof(g_v4_cluster_display));
    (void)pthread_mutex_unlock(&g_hook_lock);

    assert(mock_navicore_backend_install_v4(&backend) == 0);
    begin_lifecycle();
    assert(mock_renderer_create(diagnostic, sizeof(diagnostic)) == 0);
    wait_for_count(&g_v3_start_calls, 1u);

    assert(((TestPointerCall)
               (*(const PanaGenericVirtual **)g_interfaces[7])[3])(
               g_interfaces[7], 0x703u, settings) == 0u);
    memset(settings, 0, sizeof(settings));
    wait_for_count(&g_v4_internationalization_calls, 1u);
    (void)pthread_mutex_lock(&g_hook_lock);
    internationalization = g_v4_internationalization;
    (void)pthread_mutex_unlock(&g_hook_lock);
    assert(internationalization.abi_version ==
           MOCK_NAV_BACKEND_PAYLOAD_ABI_V1);
    assert(internationalization.struct_size == sizeof(internationalization));
    assert(internationalization.source_slot == 3u);
    assert(internationalization.request_handle == 0x703u);
    assert(internationalization.settings_present == 1u);
    assert(internationalization.language_raw == 0x1234u);
    assert(internationalization.date_format_raw == 0xa5u);
    assert(internationalization.time_format_raw == 0x5au);

    assert(((TestPointerCall)
               (*(const PanaGenericVirtual **)g_interfaces[7])[3])(
               g_interfaces[7], 0x704u, NULL) == 0u);
    wait_for_count(&g_v4_internationalization_calls, 2u);
    (void)pthread_mutex_lock(&g_hook_lock);
    internationalization = g_v4_internationalization;
    (void)pthread_mutex_unlock(&g_hook_lock);
    assert(internationalization.request_handle == 0x704u);
    assert(internationalization.settings_present == 0u);
    assert(internationalization.language_raw == 0u);
    assert(internationalization.date_format_raw == 0u);
    assert(internationalization.time_format_raw == 0u);

    assert(((TestU32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[17])[6])(
               g_interfaces[17], 0x1106u, 0xfeu) == 0u);
    wait_for_count(&g_v4_cluster_display_calls, 1u);
    (void)pthread_mutex_lock(&g_hook_lock);
    cluster_display = g_v4_cluster_display;
    (void)pthread_mutex_unlock(&g_hook_lock);
    assert(cluster_display.abi_version == MOCK_NAV_BACKEND_PAYLOAD_ABI_V1);
    assert(cluster_display.struct_size == sizeof(cluster_display));
    assert(cluster_display.source_slot == 6u);
    assert(cluster_display.request_handle == 0x1106u);
    assert(cluster_display.enabled_raw == 0xfeu);

    panaDeinitialize();

    /* Inputs accepted into a lifecycle that never starts a renderer must not
     * leak into the next renderer generation. */
    begin_lifecycle();
    assert(((TestPointerCall)
               (*(const PanaGenericVirtual **)g_interfaces[7])[3])(
               g_interfaces[7], 0x704u, settings) == 0u);
    assert(((TestU32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[17])[6])(
               g_interfaces[17], 0x1107u, 1u) == 0u);
    panaDeinitialize();
    begin_lifecycle();
    assert(mock_renderer_create(diagnostic, sizeof(diagnostic)) == 0);
    delay_ms(20u);
    (void)pthread_mutex_lock(&g_hook_lock);
    assert(g_v4_internationalization_calls == 2u);
    assert(g_v4_cluster_display_calls == 1u);
    (void)pthread_mutex_unlock(&g_hook_lock);
    panaDeinitialize();
    assert(mock_navicore_backend_install_v4(NULL) == 0);
    assert_backend_state_zero();
}

static void test_provider_v5_driver_distraction(void)
{
    MockNavBackendV5 backend;
    MockNavBackendDriverDistractionV1 distraction;
    char diagnostic[257];

    memset(&backend, 0, sizeof(backend));
    backend.v4.v3.v2.v1.abi_version = MOCK_NAV_BACKEND_ABI_V5;
    backend.v4.v3.v2.v1.struct_size = sizeof(backend);
    backend.v4.v3.start_v2 = v3_start_callback;
    backend.driver_distraction_changed =
        v5_driver_distraction_callback;
    (void)pthread_mutex_lock(&g_hook_lock);
    g_v3_start_calls = 0u;
    g_v5_driver_distraction_calls = 0u;
    memset(&g_v5_driver_distraction, 0,
           sizeof(g_v5_driver_distraction));
    (void)pthread_mutex_unlock(&g_hook_lock);

    assert(mock_navicore_backend_install_v5(&backend) == 0);
    begin_lifecycle();
    assert(mock_renderer_create(diagnostic, sizeof(diagnostic)) == 0);
    wait_for_count(&g_v3_start_calls, 1u);

    assert(((TestU32Call)
               (*(const PanaGenericVirtual **)g_interfaces[13])[4])(
               g_interfaces[13], UINT32_C(0xfedcba98)) == 0u);
    wait_for_count(&g_v5_driver_distraction_calls, 1u);
    (void)pthread_mutex_lock(&g_hook_lock);
    distraction = g_v5_driver_distraction;
    (void)pthread_mutex_unlock(&g_hook_lock);
    assert(distraction.abi_version == MOCK_NAV_BACKEND_PAYLOAD_ABI_V1);
    assert(distraction.struct_size == sizeof(distraction));
    assert(distraction.source_slot == 4u);
    assert(distraction.request_handle == 0u);
    assert(distraction.distraction_status_raw == UINT32_C(0xfedcba98));

    assert(((uint32_t (*)(void *, uint32_t, uint32_t))
               (*(const PanaGenericVirtual **)g_interfaces[13])[3])(
               g_interfaces[13], 0x1303u, UINT32_C(0x87654321)) == 0u);
    wait_for_count(&g_v5_driver_distraction_calls, 2u);
    (void)pthread_mutex_lock(&g_hook_lock);
    distraction = g_v5_driver_distraction;
    (void)pthread_mutex_unlock(&g_hook_lock);
    assert(distraction.source_slot == 3u);
    assert(distraction.request_handle == 0x1303u);
    assert(distraction.distraction_status_raw == UINT32_C(0x87654321));

    panaDeinitialize();
    begin_lifecycle();
    assert(((uint32_t (*)(void *, uint32_t, uint32_t))
               (*(const PanaGenericVirtual **)g_interfaces[13])[3])(
               g_interfaces[13], 0x1304u, 7u) == 0u);
    panaDeinitialize();
    begin_lifecycle();
    assert(mock_renderer_create(diagnostic, sizeof(diagnostic)) == 0);
    delay_ms(20u);
    (void)pthread_mutex_lock(&g_hook_lock);
    assert(g_v5_driver_distraction_calls == 2u);
    (void)pthread_mutex_unlock(&g_hook_lock);
    panaDeinitialize();
    assert(mock_navicore_backend_install_v5(NULL) == 0);
    assert_backend_state_zero();
}

int main(void)
{
    pthread_t first;
    pthread_t second;
    PanaGenericVirtual voice_vtable[TEST_VTABLE_SLOTS];
    TestProxy voice_proxy;
    MockCallbackTestState callback;
    MockClusterNavigationState cluster_state;
    MockNavBackendVisibilityV1 pending_visibility;
    MockNavBackendScreenV1 pending_screen;
    uint8_t gps_record[MOCK_GPS_RECORD_SIZE];
    void *unexpected = (void *)(uintptr_t)1u;
    uint32_t index;
    unsigned int create_before;
    unsigned int destroy_before;

    memset(&g_initialization, 0, sizeof(g_initialization));
    memset(g_interfaces, 0, sizeof(g_interfaces));
    mock_renderer_stub_reset();

    test_provider_v2_bridge();
    mock_renderer_stub_reset();
    test_provider_v3_outbound_bridge();
    test_provider_v4_recovered_inputs();
    test_provider_v5_driver_distraction();
    mock_renderer_stub_reset();

    /* Reentrant initialization is rejected without disturbing the active
     * lifecycle, and successful voice correlation requires PAS success. */
    begin_lifecycle();
    reset_hook();
    for (index = 0u; index < TEST_VTABLE_SLOTS; ++index) {
        voice_vtable[index] = action_callback;
    }
    voice_proxy.vptr = voice_vtable;
    voice_proxy.action = ACTION_INITIALIZE;
    voice_proxy.callback_status = 0u;
    ((TestAdapterObject *)g_interfaces[10])->host_proxy = &voice_proxy;
    assert(mock_callback_signal_audio_availability(2u) == 0u);
    assert(g_reentrant_initialize_result == 1u);
    assert(panaGetSoftwareInfrastructure() == g_root);

    voice_proxy.action = ACTION_NONE;
    voice_proxy.callback_status = 7u;
    ((TestAdapterObject *)g_interfaces[25])->host_proxy = &voice_proxy;
    assert(((TestU32Call)(*(const PanaGenericVirtual **)g_interfaces[25])[19])(
               g_interfaces[25], 0x2519u) == 0u);
    mock_callback_test_state(&callback);
    assert(callback.pending_count == 0u);
    assert(callback.correlation_successes == 0u);
    voice_proxy.callback_status = 0u;
    assert(((TestU32Call)(*(const PanaGenericVirtual **)g_interfaces[25])[19])(
               g_interfaces[25], 0x2520u) == 0u);
    mock_callback_test_state(&callback);
    assert(callback.pending_count == 0u);
    assert(callback.correlation_successes == 1u);
    memset(gps_record, 0, sizeof(gps_record));
    assert(((TestGpsCall)(*(const PanaGenericVirtual **)g_interfaces[9])[4])(
               g_interfaces[9], gps_record) == 0u);
    assert(mock_gps_test_call_count() == 1u);
    assert(((TestU32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[3])(
               g_interfaces[8], 0x8300u, 1u) == 0u);
    assert(((TestByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[4])(
               g_interfaces[8], 2u) == 0u);
    assert(((TestU32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[5])(
               g_interfaces[8], 0x8500u, 3u) == 0u);
    assert(((TestByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[6])(
               g_interfaces[8], 4u) == 0u);
    assert(((TestFloatCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[8])(
               g_interfaces[8], -13.25f) == 0u);
    assert(((TestU32Call)
               (*(const PanaGenericVirtual **)g_interfaces[9])[6])(
               g_interfaces[9], 1000u) == 0u);
    assert(((TestU32U32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[10])[17])(
               g_interfaces[10], 0xa017u, 1u, 15u) == 0u);
    assert(((TestU32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[10])[19])(
               g_interfaces[10], 1u, 15u) == 0u);
    /* Active provider calls must report failure when the renderer lifecycle
     * cannot retain their required latest state. */
    mock_renderer_end_lifecycle();
    assert(((TestGpsCall)(*(const PanaGenericVirtual **)g_interfaces[9])[4])(
               g_interfaces[9], gps_record) == 1u);
    assert(((TestU32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[3])(
               g_interfaces[8], 0x8301u, 1u) == 1u);
    assert(((TestByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[4])(
               g_interfaces[8], 2u) == 1u);
    assert(((TestU32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[5])(
               g_interfaces[8], 0x8501u, 3u) == 1u);
    assert(((TestByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[6])(
               g_interfaces[8], 4u) == 1u);
    assert(((TestFloatCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[8])(
               g_interfaces[8], -13.25f) == 1u);
    assert(((TestU32Call)
               (*(const PanaGenericVirtual **)g_interfaces[9])[6])(
               g_interfaces[9], 1000u) == 1u);
    assert(((TestU32U32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[10])[17])(
               g_interfaces[10], 0xa018u, 1u, 15u) == 1u);
    assert(((TestU32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[10])[19])(
               g_interfaces[10], 1u, 15u) == 1u);
    assert(((TestHmiTransitionCall)
               (*(const PanaGenericVirtual **)g_interfaces[1])[5])(
               g_interfaces[1], 0x9000u, 0u, 1u, 1u, NULL) == 1u);
    mock_renderer_prepare_lifecycle();

    /* HMI capacity rejection is atomic with renderer-state retention. Each
     * display mailbox reflects only an accepted provider request. */
    assert(((TestHmiTransitionCall)
               (*(const PanaGenericVirtual **)g_interfaces[1])[5])(
               g_interfaces[1], 0x8fffu, 3u, 99u, 99u, NULL) == 1u);
    assert(mock_renderer_test_visibility_unsupported() == 1u);
    for (index = 0u; index < 8u; ++index) {
        assert(((TestHmiTransitionCall)
                   (*(const PanaGenericVirtual **)g_interfaces[1])[5])(
                   g_interfaces[1], 0x9100u + index, 0u,
                   index + 1u, index + 101u, NULL) == 0u);
    }
    assert(((TestHmiTransitionCall)
               (*(const PanaGenericVirtual **)g_interfaces[1])[5])(
               g_interfaces[1], 0x91ffu, 0u, 999u, 999u, NULL) == 1u);
    assert(mock_renderer_test_pending_visibility(
               0u, &pending_visibility) != 0);
    assert(pending_visibility.visibility == 8u);
    assert(pending_visibility.interactivity == 108u);
    for (index = 0u; index < 8u; ++index) {
        assert(((TestHmiScreenCall)
                   (*(const PanaGenericVirtual **)g_interfaces[1])[9])(
                   g_interfaces[1], 0x9200u + index, 1u,
                   index + 20u, index + 30u, 0u) == 0u);
    }
    assert(((TestHmiScreenCall)
               (*(const PanaGenericVirtual **)g_interfaces[1])[9])(
               g_interfaces[1], 0x92ffu, 1u, 777u, 777u, 0u) == 1u);
    assert(mock_renderer_test_pending_screen(1u, &pending_screen) != 0);
    assert(pending_screen.screen_type == 27u);
    assert(pending_screen.animation == 37u);
    assert(mock_hmi_test_transition_reply_count() == 0u);
    assert(mock_renderer_stub_create_count() == 0u);
    panaDeinitialize();
    assert_backend_state_zero();

    test_direct_callback_deinitialize(10u, 10u, PRODUCER_AUDIO);
    test_direct_callback_deinitialize(UINT32_MAX, 10u, PRODUCER_THREAD);
    test_direct_callback_deinitialize(17u, 3u, PRODUCER_CLUSTER);

    /* Voice replies are also safe when their callback tears the provider down. */
    begin_lifecycle();
    reset_hook();
    voice_proxy.vptr = voice_vtable;
    voice_proxy.action = ACTION_DEINITIALIZE;
    voice_proxy.callback_status = 0u;
    ((TestAdapterObject *)g_interfaces[25])->host_proxy = &voice_proxy;
    assert(((TestU32Call)(*(const PanaGenericVirtual **)g_interfaces[25])[19])(
               g_interfaces[25], 0x2521u) == 0u);
    panaDeinitialize();
    assert_backend_state_zero();

    /* Concurrent teardown has one finisher and both callers return only after
     * the lifecycle is completely cold. */
    begin_lifecycle();
    reset_hook();
    assert(pthread_create(&first, NULL, deinitialize_thread, NULL) == 0);
    assert(pthread_create(&second, NULL, deinitialize_thread, NULL) == 0);
    assert(pthread_join(first, NULL) == 0);
    assert(pthread_join(second, NULL) == 0);
    assert(panaGetSoftwareInfrastructure() == NULL);
    assert_backend_state_zero();

    exercise_producer_race(PRODUCER_AUDIO, 10u, 10u);
    exercise_producer_race(PRODUCER_THREAD, UINT32_MAX, 10u);
    exercise_producer_race(PRODUCER_CLUSTER, 17u, 3u);

    /* Teardown from both worker callback classes exits the old worker before
     * COLD.  The following lifecycle therefore owns exactly one fresh worker
     * and one fresh renderer. */
    mock_renderer_stub_reset();
    begin_lifecycle();
    reset_hook();
    install_worker_proxies(8, 0);
    (void)g_root->vptr->slot[4](g_root);
    wait_for_int(&g_action_returned);
    panaDeinitialize();
    assert(mock_renderer_stub_create_count() == 0u);
    assert(mock_renderer_stub_destroy_count() == 0u);

    begin_lifecycle();
    reset_hook();
    install_worker_proxies(-1, 1);
    (void)g_root->vptr->slot[4](g_root);
    wait_for_int(&g_action_returned);
    panaDeinitialize();
    assert(mock_renderer_stub_create_count() == 1u);
    assert(mock_renderer_stub_destroy_count() == 1u);

    begin_lifecycle();
    reset_hook();
    install_worker_proxies(-1, 0);
    assert(g_root->vptr->slot[4](g_root) == 0u);
    wait_for_count(&g_hmi_ready_calls, 1u);
    assert(mock_renderer_stub_create_count() == 2u);
    panaDeinitialize();
    assert(mock_renderer_stub_destroy_count() == 2u);

    /* Transition telemetry commits only a PAS-accepted reply. */
    begin_lifecycle();
    reset_hook();
    install_worker_proxies(-1, 0);
    assert(g_root->vptr->slot[4](g_root) == 0u);
    wait_for_count(&g_hmi_ready_calls, 1u);
    g_hmi_proxy.callback_status = 9u;
    assert(((TestHmiTransitionCall)
               (*(const PanaGenericVirtual **)g_interfaces[1])[5])(
               g_interfaces[1], 0x9001u, 0u, 1u, 1u, NULL) == 0u);
    wait_for_count(&g_transition_calls, 1u);
    assert(mock_hmi_test_transition_reply_count() == 0u);
    panaDeinitialize();

    begin_lifecycle();
    reset_hook();
    install_worker_proxies(-1, 0);
    assert(g_root->vptr->slot[4](g_root) == 0u);
    wait_for_count(&g_hmi_ready_calls, 1u);
    g_hmi_proxy.callback_status = 0u;
    assert(((TestHmiTransitionCall)
               (*(const PanaGenericVirtual **)g_interfaces[1])[5])(
               g_interfaces[1], 0x9002u, 0u, 1u, 1u, NULL) == 0u);
    wait_for_count(&g_transition_calls, 1u);
    assert(mock_hmi_test_transition_reply_count() == 1u);
    panaDeinitialize();

    /* Every provider class and every public producer rejects COLD without
     * rebuilding telemetry or backend state. */
    create_before = mock_renderer_stub_create_count();
    destroy_before = mock_renderer_stub_destroy_count();
    memset(gps_record, 0, sizeof(gps_record));
    assert(g_root->vptr->slot[0](g_root) == 1u);
    assert(((TestGetAdapter)g_root->vptr->slot[3])(
               g_root, 8u, &unexpected) == 1u);
    assert(unexpected == NULL);
    assert(((TestGpsCall)(*(const PanaGenericVirtual **)g_interfaces[9])[4])(
               g_interfaces[9], gps_record) == 1u);
    assert(((TestByteCall)(*(const PanaGenericVirtual **)g_interfaces[8])[4])(
               g_interfaces[8], 1u) == 1u);
    assert(((TestU32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[5])(
               g_interfaces[8], 1u, 1u) == 1u);
    assert(((TestByteCall)(*(const PanaGenericVirtual **)g_interfaces[8])[6])(
               g_interfaces[8], 1u) == 1u);
    assert(((TestFloatCall)(*(const PanaGenericVirtual **)g_interfaces[8])[8])(
               g_interfaces[8], 1.0f) == 1u);
    assert(((TestU32Call)(*(const PanaGenericVirtual **)g_interfaces[9])[6])(
               g_interfaces[9], 1000u) == 1u);
    assert(((TestU32U32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[10])[17])(
               g_interfaces[10], 1u, 1u, 15u) == 1u);
    assert(((TestU32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[10])[19])(
               g_interfaces[10], 1u, 15u) == 1u);
    assert(((TestU32Call)(*(const PanaGenericVirtual **)g_interfaces[12])[5])(
               g_interfaces[12], 1u) == 1u);
    assert(((TestPointerCall)(*(const PanaGenericVirtual **)g_interfaces[7])[3])(
               g_interfaces[7], 1u, NULL) == 1u);
    assert(((TestU32ByteCall)
               (*(const PanaGenericVirtual **)g_interfaces[8])[3])(
               g_interfaces[8], 1u, 1u) == 1u);
    assert(((TestU32Call)(*(const PanaGenericVirtual **)g_interfaces[25])[19])(
               g_interfaces[25], 1u) == 1u);
    assert(((TestHmiTransitionCall)
               (*(const PanaGenericVirtual **)g_interfaces[1])[5])(
               g_interfaces[1], 1u, 0u, 1u, 1u, NULL) == 1u);
    assert(mock_callback_signal_audio_availability(1u) == 1u);
    assert(mock_callback_signal_thread_status(1u, "cold", 1u) == 1u);
    memset(&cluster_state, 0, sizeof(cluster_state));
    cluster_state.valid_attributes = MOCK_CLUSTER_NAVIGATION_STATUS;
    cluster_state.navigation_status = MOCK_CLUSTER_NAV_NO_ROUTE;
    assert(mock_cluster_backend_publish(&cluster_state) == 1u);
    assert_backend_state_zero();
    assert(mock_renderer_stub_create_count() == create_before);
    assert(mock_renderer_stub_destroy_count() == destroy_before);
    return 0;
}
