#define _POSIX_C_SOURCE 200112L

#include "open_navi.h"
#include "open_navi_renderer.h"
#include "open_navi_backend_loader.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef EXPECT_TRACE_PATH
#define EXPECT_TRACE_PATH "/tmp/open-navi-reference-plugin.log"
#endif

enum { TEST_ADAPTERS = 256, TEST_SLOTS = 32 };

typedef struct {
    const PanaGenericVirtual *vptr;
    void *host_proxy;
    uint32_t adapter_id;
} TestAdapter;

typedef struct {
    const PanaGenericVirtual *vptr;
} TestProxy;

static PanaGenericVirtual g_vtable[TEST_SLOTS];
static PanaGenericVirtual g_hmi_vtable[TEST_SLOTS];
static TestProxy g_root_proxy;
static TestProxy g_adapter_proxy[TEST_ADAPTERS];
static pthread_mutex_t g_hmi_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_hmi_ready;

static uint32_t noop(void *self, ...)
{
    (void)self;
    return 0u;
}

static uint32_t hmi_ready(void *self, ...)
{
    (void)self;
    (void)pthread_mutex_lock(&g_hmi_lock);
    if (g_hmi_ready != UINT32_MAX) ++g_hmi_ready;
    (void)pthread_mutex_unlock(&g_hmi_lock);
    return 0u;
}

static void delay_ms(unsigned int milliseconds)
{
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000u);
    delay.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    (void)nanosleep(&delay, NULL);
}

static void wait_hmi(uint32_t expected)
{
    unsigned int attempt;
    for (attempt = 0u; attempt < 5000u; ++attempt) {
        uint32_t ready;
        (void)pthread_mutex_lock(&g_hmi_lock);
        ready = g_hmi_ready;
        (void)pthread_mutex_unlock(&g_hmi_lock);
        if (ready >= expected) return;
        delay_ms(1u);
    }
    assert(!"timed out waiting for real readiness sequence");
}

static unsigned int trace_line_count(const char *wanted)
{
    FILE *file = fopen(EXPECT_TRACE_PATH, "rb");
    char line[128];
    unsigned int count = 0u;
    size_t wanted_length = strlen(wanted);
    if (file == NULL) return 0u;
    while (fgets(line, sizeof(line), file) != NULL) {
        size_t length = strlen(line);
        if (length != 0u && line[length - 1u] == '\n') --length;
        if (length == wanted_length &&
            memcmp(line, wanted, wanted_length) == 0)
            ++count;
    }
    (void)fclose(file);
    return count;
}

static void wait_trace_line(const char *wanted, uint32_t expected)
{
    unsigned int attempt;
    for (attempt = 0u; attempt < 5000u; ++attempt) {
        if (trace_line_count(wanted) >= expected) return;
        delay_ms(1u);
    }
    assert(!"timed out waiting for backend trace record");
}

static void run_lifecycle(uint32_t number, int expect_plugin_render,
                          uint32_t input_query_abi)
{
    PanaInitializationData initialization;
    PanaRootObject *root;
    PanaGetAdapterVirtual get_adapter;
    uint32_t adapter;
    void *internationalization = NULL;
    void *cluster = NULL;

    memset(&initialization, 0, sizeof(initialization));
    assert(panaInitialize(&initialization) == 0u);
    root = (PanaRootObject *)panaGetSoftwareInfrastructure();
    assert(root != NULL);
    root->host_proxy = &g_root_proxy;
    get_adapter = (PanaGetAdapterVirtual)root->vptr->slot[3];
    for (adapter = 0u; adapter < TEST_ADAPTERS; ++adapter) {
        void *interface = NULL;
        uint32_t status = get_adapter(root, adapter, &interface);
        if (adapter == 21u || adapter == 27u || adapter == 28u) {
            assert(status == 1u && interface == NULL);
        } else {
            assert(status == 0u && interface != NULL);
            ((TestAdapter *)interface)->host_proxy = &g_adapter_proxy[adapter];
            if (adapter == 7u) internationalization = interface;
            if (adapter == 17u) cluster = interface;
        }
    }
    assert(root->vptr->slot[4](root) == 0u);
    wait_hmi(number);
    if (expect_plugin_render != 0) wait_trace_line("render", number);
    if (input_query_abi >= OPEN_NAVI_PLUGIN_QUERY_ABI_V3) {
        static const uint8_t settings[4] = {0x34u, 0x12u, 0xa5u, 0x5au};
        typedef uint32_t (*IntlCall)(void *, uint32_t, const void *);
        typedef uint32_t (*ClusterCall)(void *, uint32_t, uint8_t);
        assert(internationalization != NULL && cluster != NULL);
        assert(((IntlCall)(*(const PanaGenericVirtual **)internationalization)[3])(
                   internationalization, 0x703u, settings) == 0u);
        assert(((ClusterCall)(*(const PanaGenericVirtual **)cluster)[6])(
                   cluster, 0x1106u, 0xfeu) == 0u);
        wait_trace_line(
            "reference_internationalization slot=3 handle=1795 present=1 "
            "language_raw=4660 date_raw=165 time_raw=90", number);
        wait_trace_line(
            "reference_cluster_display slot=6 handle=4358 enabled_raw=254",
            number);
    }
    if (input_query_abi >= OPEN_NAVI_PLUGIN_QUERY_ABI_V4) {
        typedef uint32_t (*DistractionReply)(void *, uint32_t, uint32_t);
        typedef uint32_t (*DistractionInfo)(void *, uint32_t);
        void *diagnosis_navigation = NULL;
        /* Adapter objects are process-lifetime; recover adapter 13 again to
         * keep this test independent of loop-local interface variables. */
        assert(get_adapter(root, 13u, &diagnosis_navigation) == 0u);
        assert(diagnosis_navigation != NULL);
        assert(((DistractionInfo)
                    (*(const PanaGenericVirtual **)diagnosis_navigation)[4])(
                   diagnosis_navigation, UINT32_C(0xfedcba98)) == 0u);
        assert(((DistractionReply)
                    (*(const PanaGenericVirtual **)diagnosis_navigation)[3])(
                   diagnosis_navigation, 0x1303u,
                   UINT32_C(0x87654321)) == 0u);
        wait_trace_line(
            "reference_driver_distraction slot=3 handle=4867 "
            "status_raw=2271560481", number);
    }
    if (open_navi_renderer_stub_create_count() != number)
        (void)fprintf(stderr, "create count got=%u expected=%u\n",
                      open_navi_renderer_stub_create_count(), number);
    assert(open_navi_renderer_stub_create_count() == number);
    panaDeinitialize();
    assert(open_navi_renderer_stub_destroy_count() == number);
    assert(panaGetSoftwareInfrastructure() == NULL);
}

static unsigned int count_line(const char *text, const char *wanted)
{
    unsigned int count = 0u;
    size_t wanted_length = strlen(wanted);
    const char *cursor = text;
    while (*cursor != '\0') {
        const char *end = strchr(cursor, '\n');
        size_t length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (length == wanted_length &&
            memcmp(cursor, wanted, wanted_length) == 0)
            ++count;
        if (end == NULL) break;
        cursor = end + 1;
    }
    return count;
}

static int line_index(const char *text, const char *wanted,
                      unsigned int occurrence)
{
    unsigned int seen = 0u;
    int index = 0;
    size_t wanted_length = strlen(wanted);
    const char *cursor = text;
    while (*cursor != '\0') {
        const char *end = strchr(cursor, '\n');
        size_t length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (length == wanted_length &&
            memcmp(cursor, wanted, wanted_length) == 0 &&
            ++seen == occurrence)
            return index;
        if (end == NULL) break;
        cursor = end + 1;
        ++index;
    }
    return -1;
}

static int line_index_after(const char *text, const char *wanted,
                            int minimum_index)
{
    int index = 0;
    size_t wanted_length = strlen(wanted);
    const char *cursor = text;
    while (*cursor != '\0') {
        const char *end = strchr(cursor, '\n');
        size_t length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (index > minimum_index && length == wanted_length &&
            memcmp(cursor, wanted, wanted_length) == 0)
            return index;
        if (end == NULL) break;
        cursor = end + 1;
        ++index;
    }
    return -1;
}

static char *read_trace(void)
{
    FILE *file = fopen(EXPECT_TRACE_PATH, "rb");
    long length;
    char *text;
    if (file == NULL) return NULL;
    assert(fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    assert(length >= 0 && length < 65536);
    assert(fseek(file, 0, SEEK_SET) == 0);
    text = (char *)calloc((size_t)length + 1u, 1u);
    assert(text != NULL);
    assert(fread(text, 1u, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);
    return text;
}

int main(int argc, char **argv)
{
    OpenNaviBackendLoaderTestState state;
    char *trace;
    uint32_t slot;
    uint32_t lifecycle;
    uint32_t expected_reason;
    uint32_t expected_valid;
    uint32_t expected_lifecycles;
    uint32_t expected_start_failure;
    uint32_t expected_surface_failure;
    uint32_t expected_render_failure;
    uint32_t expected_outbound;
    uint32_t expected_query_abi;

    assert(argc == 9);
    expected_reason = (uint32_t)strtoul(argv[1], NULL, 0);
    expected_valid = (uint32_t)strtoul(argv[2], NULL, 0);
    expected_lifecycles = (uint32_t)strtoul(argv[3], NULL, 0);
    expected_start_failure = (uint32_t)strtoul(argv[4], NULL, 0);
    expected_surface_failure = (uint32_t)strtoul(argv[5], NULL, 0);
    expected_render_failure = (uint32_t)strtoul(argv[6], NULL, 0);
    expected_outbound = (uint32_t)strtoul(argv[7], NULL, 0);
    expected_query_abi = (uint32_t)strtoul(argv[8], NULL, 0);

    (void)remove(EXPECT_TRACE_PATH);
    for (slot = 0u; slot < TEST_SLOTS; ++slot) g_vtable[slot] = noop;
    memcpy(g_hmi_vtable, g_vtable, sizeof(g_hmi_vtable));
    g_hmi_vtable[2] = hmi_ready;
    g_root_proxy.vptr = g_vtable;
    for (slot = 0u; slot < TEST_ADAPTERS; ++slot)
        g_adapter_proxy[slot].vptr = g_vtable;
    g_adapter_proxy[1].vptr = g_hmi_vtable;
    open_navi_renderer_stub_reset();

    for (lifecycle = 1u; lifecycle <= expected_lifecycles; ++lifecycle)
        run_lifecycle(lifecycle,
                      expected_valid != 0u && expected_start_failure == 0u &&
                      expected_surface_failure == 0u,
                      expected_valid != 0u &&
                              expected_start_failure == 0u &&
                              expected_surface_failure == 0u &&
                              expected_render_failure == 0u
                          ? expected_query_abi : 0u);

    memset(&state, 0, sizeof(state));
    open_navi_backend_loader_test_state(&state);
    assert(state.bootstrap_calls == expected_lifecycles);
    assert(state.load_attempts == 1u);
    assert(state.query_calls == (expected_valid ?
        (5u - expected_query_abi) :
        (expected_reason == OPEN_NAVI_BACKEND_FALLBACK_LOAD ||
         expected_reason == OPEN_NAVI_BACKEND_FALLBACK_SYMBOL ? 0u :
         expected_reason == OPEN_NAVI_BACKEND_FALLBACK_QUERY ? 4u : 1u)));
    assert(state.plugin_valid == expected_valid);
    if (expected_valid) assert(state.query_abi == expected_query_abi);
    assert(state.fallback_reason == expected_reason);
    if (expected_reason == OPEN_NAVI_BACKEND_FALLBACK_LOAD)
        assert(state.handle_pinned == 0u);
    else
        assert(state.handle_pinned == 1u);

    if (expected_valid) {
        assert(state.wrapper_starts == expected_lifecycles);
        assert(state.wrapper_stops == expected_lifecycles);
        assert(state.plugin_starts == expected_lifecycles);
        assert(state.plugin_stops ==
               (expected_start_failure ? 0u : expected_lifecycles));
        if (expected_reason == OPEN_NAVI_BACKEND_FALLBACK_NONE) {
            assert(state.default_starts == 0u);
            assert(state.plugin_surface_destroys == expected_lifecycles);
        } else {
            assert(state.default_starts == expected_lifecycles);
            assert(state.default_stops == expected_lifecycles);
            assert(state.default_surface_destroys == expected_lifecycles);
        }
        if (expected_surface_failure || expected_start_failure)
            assert(state.plugin_surface_destroys == 0u);
        if (expected_render_failure)
            assert(state.plugin_surface_destroys == expected_lifecycles);
    } else {
        assert(state.wrapper_starts == 0u && state.plugin_starts == 0u);
    }

    trace = read_trace();
    if (expected_reason == OPEN_NAVI_BACKEND_FALLBACK_LOAD ||
        expected_reason == OPEN_NAVI_BACKEND_FALLBACK_SYMBOL) {
        assert(trace == NULL);
    } else {
        assert(trace != NULL);
        assert(count_line(trace, "query") ==
               (expected_reason == OPEN_NAVI_BACKEND_FALLBACK_QUERY ||
                expected_valid ?
                    (expected_reason == OPEN_NAVI_BACKEND_FALLBACK_QUERY
                         ? 4u : 5u - expected_query_abi) : 1u));
        assert(count_line(trace, "query_v4") == 1u);
        assert(count_line(trace, "query_v3") ==
               (expected_reason == OPEN_NAVI_BACKEND_FALLBACK_QUERY ||
                (expected_valid && expected_query_abi <=
                 OPEN_NAVI_PLUGIN_QUERY_ABI_V3) ? 1u : 0u));
        assert(count_line(trace, "query_v2") ==
               (expected_reason == OPEN_NAVI_BACKEND_FALLBACK_QUERY ||
                (expected_valid && expected_query_abi <=
                 OPEN_NAVI_PLUGIN_QUERY_ABI_V2) ? 1u : 0u));
        assert(count_line(trace, "query_v1") ==
               (expected_reason == OPEN_NAVI_BACKEND_FALLBACK_QUERY ||
                (expected_valid && expected_query_abi ==
                 OPEN_NAVI_PLUGIN_QUERY_ABI_V1) ? 1u : 0u));
        assert(count_line(trace, "query_unsupported") == 0u);
        if (expected_valid) {
            uint32_t cycle;
            assert(count_line(trace, "start") == expected_lifecycles);
            assert(count_line(trace, "start_v2") ==
                   (expected_query_abi >= OPEN_NAVI_PLUGIN_QUERY_ABI_V2
                        ? expected_lifecycles : 0u));
            if (expected_query_abi >= OPEN_NAVI_PLUGIN_QUERY_ABI_V2)
                assert(line_index(trace, "query_v4", 1u) <
                       line_index(trace, "start_v2", 1u));
            else
                assert(line_index(trace, "query_v4", 1u) <
                       line_index(trace, "query_v1", 1u));
            if (!expected_start_failure) {
                assert(count_line(trace, "worker_joined") == expected_lifecycles);
                assert(count_line(trace, "start_request=0") ==
                       expected_lifecycles);
                assert(count_line(trace, "stale_request=-1") == expected_lifecycles);
                if (expected_query_abi >= OPEN_NAVI_PLUGIN_QUERY_ABI_V2) {
                    assert(count_line(trace, "stop_cluster_stale") ==
                           expected_lifecycles);
                    assert(count_line(trace, "stop_audio_stale") ==
                           expected_lifecycles);
                    assert(count_line(trace, "stop_thread_stale") ==
                           expected_lifecycles);
                }
                assert(count_line(trace, "stop") == expected_lifecycles);
            } else if (expected_query_abi >= OPEN_NAVI_PLUGIN_QUERY_ABI_V2) {
                assert(count_line(trace, "failed_start_cluster_stale") ==
                       expected_lifecycles);
                assert(count_line(trace, "failed_start_audio_stale") ==
                       expected_lifecycles);
                assert(count_line(trace, "failed_start_thread_stale") ==
                       expected_lifecycles);
            }
            if (expected_surface_failure)
                assert(count_line(trace, "surface_fail") == expected_lifecycles);
            if (expected_render_failure)
                assert(count_line(trace, "render_fail") == expected_lifecycles);
            if (expected_outbound) {
                assert(count_line(trace, "outbound_test_trigger render=1") ==
                       expected_lifecycles);
                assert(count_line(
                           trace,
                           "outbound_cluster_values valid=3 route=1 navigation=3") ==
                       expected_lifecycles);
                assert(count_line(trace, "outbound_cluster=0") ==
                       expected_lifecycles);
                assert(count_line(
                           trace,
                           "outbound_audio_value availability=1") ==
                       expected_lifecycles);
                assert(count_line(trace, "outbound_audio=0") ==
                       expected_lifecycles);
                assert(count_line(
                           trace,
                           "outbound_thread_values id=1 status=1 name=ref-worker") ==
                       expected_lifecycles);
                assert(count_line(trace, "outbound_thread=0") ==
                       expected_lifecycles);
                assert(state.outbound_accepted == expected_lifecycles * 3u);
            } else {
                assert(count_line(trace, "outbound_test_trigger render=1") ==
                       0u);
                assert(state.outbound_accepted == 0u);
            }
            for (cycle = 1u; cycle <= expected_lifecycles; ++cycle) {
                int start = line_index(trace, "start", cycle);
                assert(start >= 0);
                if (expected_start_failure) {
                    assert(start < line_index(trace, "start_fail", cycle));
                } else {
                    int surface = line_index(trace, "surface_created", cycle);
                    int joined = line_index(trace, "worker_joined", cycle);
                    int stale = line_index(trace, "stale_request=-1", cycle);
                    int stop = line_index(trace, "stop", cycle);
                    assert(start < surface);
                    assert(surface < joined && joined < stale && stale < stop);
                    if (expected_surface_failure) {
                        assert(surface < line_index(trace, "surface_fail", cycle));
                    } else {
                        int render = line_index_after(trace, "render", surface);
                        int destroyed = line_index(trace, "surface_destroyed", cycle);
                        assert(surface < render && render < destroyed);
                        if (expected_outbound) {
                            int trigger = line_index(
                                trace, "outbound_test_trigger render=1", cycle);
                            int cluster = line_index(
                                trace, "outbound_cluster=0", cycle);
                            int audio = line_index(
                                trace, "outbound_audio=0", cycle);
                            int thread = line_index(
                                trace, "outbound_thread=0", cycle);
                            assert(render < trigger && trigger < cluster);
                            assert(cluster < audio && audio < thread);
                        }
                        assert(destroyed < joined);
                        if (expected_render_failure)
                            assert(render < line_index(trace, "render_fail", cycle));
                    }
                }
            }
        }
    }
    free(trace);
    return 0;
}
