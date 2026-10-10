#define _POSIX_C_SOURCE 200112L

#include "mock_gps_backend.h"
#include "mock_callback_backend.h"
#include <open_navicore/cluster.h>
#include "mock_navicore.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TRACE_TEST_PATH "/tmp/mock-navicore-host-test.log"

typedef uint32_t (*TestGPSDataVirtual)(void *self, const void *record);

typedef struct {
    const PanaGenericVirtual *vptr;
} TestProxy;

typedef struct {
    const PanaGenericVirtual *vptr;
    void *host_proxy;
} TestAdapter;

static uint32_t accept_callback(void *self, ...)
{
    (void)self;
    return 0u;
}

static unsigned int count_text(const char *text, const char *needle)
{
    unsigned int count = 0u;
    size_t needle_length = strlen(needle);

    while ((text = strstr(text, needle)) != NULL) {
        ++count;
        text += needle_length;
    }
    return count;
}

int main(void)
{
    PanaServerVersionHeadUnit server = {{ 24u, 7u, 0u, 0u, 0u, 0u }};
    PanaClientVersionNavigationSoftwareModule client;
    PanaInitializationData initialization;
    PanaRootObject *root;
    PanaGetAdapterVirtual get_adapter;
    void *gps_interface = NULL;
    void *audio_interface = NULL;
    void *cluster_interface = NULL;
    PanaGenericVirtual callback_vtable[32];
    TestProxy callback_proxy;
    uint8_t gps_record[MOCK_GPS_RECORD_SIZE];
    uint8_t cluster_record[MOCK_CLUSTER_RECORD_SIZE];
    uint32_t compatibility = 0u;
    char contents[65536];
    size_t bytes_read;
    FILE *trace;
    unsigned int slot;

    (void)unlink(TRACE_TEST_PATH);
    errno = EDOM;
    assert(panaVersionCheck(server, &client, &compatibility) == 0u);
    assert(errno == EDOM);

    trace = fopen(TRACE_TEST_PATH, "r");
    assert(trace != NULL);
    bytes_read = fread(contents, 1u, sizeof(contents) - 1u, trace);
    contents[bytes_read] = '\0';
    assert(fclose(trace) == 0);
    assert(strstr(contents, "panaVersionCheck server=") != NULL);
    assert(unlink(TRACE_TEST_PATH) == 0);

    memset(&initialization, 0, sizeof(initialization));
    memset(gps_record, 0, sizeof(gps_record));
    assert(panaInitialize(&initialization) == 0u);
    root = (PanaRootObject *)panaGetSoftwareInfrastructure();
    get_adapter = (PanaGetAdapterVirtual)root->vptr->slot[3];
    for (slot = 0u; slot < 32u; ++slot)
        callback_vtable[slot] = accept_callback;
    callback_proxy.vptr = callback_vtable;
    root->host_proxy = &callback_proxy;
    assert(get_adapter(root, 9u, &gps_interface) == 0u);
    assert(get_adapter(root, 10u, &audio_interface) == 0u);
    assert(get_adapter(root, 17u, &cluster_interface) == 0u);
    assert(gps_interface != NULL);
    assert(audio_interface != NULL && cluster_interface != NULL);
    ((TestAdapter *)audio_interface)->host_proxy = &callback_proxy;
    ((TestAdapter *)cluster_interface)->host_proxy = &callback_proxy;
    for (compatibility = 0u; compatibility < 5u; ++compatibility) {
        assert(((TestGPSDataVirtual)
                   (*(const PanaGenericVirtual **)gps_interface)[4])(
                   gps_interface, gps_record) == 0u);
    }
    assert(((TestGPSDataVirtual)
               (*(const PanaGenericVirtual **)gps_interface)[4])(
               gps_interface, NULL) == 0u);
    memset(cluster_record, 0, sizeof(cluster_record));
    cluster_record[0] = 1u;
    cluster_record[1] = 3u;
    cluster_record[0xa56] = 3u;
    cluster_record[0xa58] = 3u;
    assert(mock_callback_send_cluster(cluster_record) == 0u);
    assert(mock_callback_signal_audio_availability(1u) == 0u);
    assert(mock_callback_signal_thread_status(
               1u, "ref-worker", 1u) == 0u);
    panaDeinitialize();

    trace = fopen(TRACE_TEST_PATH, "r");
    assert(trace != NULL);
    bytes_read = fread(contents, 1u, sizeof(contents) - 1u, trace);
    contents[bytes_read] = '\0';
    assert(fclose(trace) == 0);
    /* Successful calls 1, 2 and 4 are checkpoints; NULL call 6 is always
     * retained. Calls 3 and 5 still parse and update but do not grow logs. */
    assert(count_text(contents, "GPS data slot=4") == 4u);
    assert(strstr(contents, "call_count=1 checkpoint=1") != NULL);
    assert(strstr(contents, "call_count=2 checkpoint=1") != NULL);
    assert(strstr(contents, "call_count=4 checkpoint=1") != NULL);
    assert(strstr(contents, "call_count=6 checkpoint=1") != NULL);
    assert(strstr(contents,
                  "outbound PAS callback adapter=17 slot=3 payload_size=0xa5c route=1 navigation=3 changed=0x0003 valid=0x0003 status=0") != NULL);
    assert(strstr(contents,
                  "outbound PAS callback adapter=10 slot=10 state=1 status=0") != NULL);
    assert(strstr(contents,
                  "outbound PAS callback root slot=10 thread_id=1 name=ref-worker thread_status=1 status=0") != NULL);
    assert(unlink(TRACE_TEST_PATH) == 0);
    return 0;
}
