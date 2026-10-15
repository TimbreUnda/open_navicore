#define _POSIX_C_SOURCE 200112L

#include "open_navi.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define REFERENCE_TRACE_PATH "/tmp/open-navi-reference-plugin.log"

typedef uint32_t (*Initialize)(const void *record);
typedef void *(*GetInfrastructure)(void);
typedef void (*Deinitialize)(void);

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

static int wait_for_hmi(void)
{
    struct timespec delay = { 0, 1000000L };
    unsigned int attempt;
    for (attempt = 0u; attempt < 5000u; ++attempt) {
        uint32_t ready;
        (void)pthread_mutex_lock(&g_hmi_lock);
        ready = g_hmi_ready;
        (void)pthread_mutex_unlock(&g_hmi_lock);
        if (ready != 0u) return 1;
        (void)nanosleep(&delay, NULL);
    }
    return 0;
}

static void *required(void *handle, const char *name)
{
    void *symbol;
    const char *error;
    (void)dlerror();
    symbol = dlsym(handle, name);
    error = dlerror();
    if (error != NULL || symbol == NULL) {
        (void)fprintf(stderr, "missing %s: %s\n", name,
                      error != NULL ? error : "null");
        return NULL;
    }
    return symbol;
}

static int trace_contains(const char *wanted)
{
    char buffer[4096];
    ssize_t count;
    int descriptor = open(REFERENCE_TRACE_PATH, O_RDONLY);
    if (descriptor < 0) return 0;
    count = read(descriptor, buffer, sizeof(buffer) - 1u);
    (void)close(descriptor);
    if (count <= 0) return 0;
    buffer[count] = '\0';
    return strstr(buffer, wanted) != NULL;
}

static int wait_for_trace(const char *wanted)
{
    struct timespec delay = { 0, 1000000L };
    unsigned int attempt;
    for (attempt = 0u; attempt < 5000u; ++attempt) {
        if (trace_contains(wanted)) return 1;
        (void)nanosleep(&delay, NULL);
    }
    return 0;
}

int main(int argc, char **argv)
{
    PanaInitializationData initialization;
    void *provider_handle;
    union { void *object; Initialize function; } initialize;
    union { void *object; GetInfrastructure function; } get_infrastructure;
    union { void *object; Deinitialize function; } deinitialize;
    PanaRootObject *root;
    PanaGetAdapterVirtual get_adapter;
    uint32_t adapter;
    uint32_t slot;
    int initialized = 0;
    int result = 1;

    if (argc != 2) {
        (void)fprintf(stderr,
            "usage: %s provider.so\n"
            "install the reviewed reference plugin first at "
            "/datastore/navicore-backend/libOpenNaviBackendOsmAnd.so\n",
            argv[0]);
        return 2;
    }
    if (unlink(REFERENCE_TRACE_PATH) != 0 && errno != ENOENT) return 4;
    provider_handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (provider_handle == NULL) {
        (void)fprintf(stderr, "provider dlopen failed: %s\n", dlerror());
        return 3;
    }
    initialize.object = required(provider_handle, "panaInitialize");
    get_infrastructure.object = required(
        provider_handle, "panaGetSoftwareInfrastructure");
    deinitialize.object = required(provider_handle, "panaDeinitialize");
    if (initialize.function == NULL || get_infrastructure.function == NULL ||
        deinitialize.function == NULL)
        goto done;
    memset(&initialization, 0, sizeof(initialization));
    if (initialize.function(&initialization) != 0) goto done;
    initialized = 1;
    root = (PanaRootObject *)get_infrastructure.function();
    if (root == NULL) goto done;
    for (slot = 0u; slot < TEST_SLOTS; ++slot) g_vtable[slot] = noop;
    memcpy(g_hmi_vtable, g_vtable, sizeof(g_hmi_vtable));
    g_hmi_vtable[2] = hmi_ready;
    g_root_proxy.vptr = g_vtable;
    root->host_proxy = &g_root_proxy;
    get_adapter = (PanaGetAdapterVirtual)root->vptr->slot[3];
    for (adapter = 0u; adapter < TEST_ADAPTERS; ++adapter) {
        void *interface = NULL;
        uint32_t status = get_adapter(root, adapter, &interface);
        if (adapter == 21u || adapter == 27u || adapter == 28u) {
            if (status != 1u || interface != NULL) goto done;
        } else {
            if (status != 0u || interface == NULL) goto done;
            g_adapter_proxy[adapter].vptr =
                adapter == 1u ? g_hmi_vtable : g_vtable;
            ((TestAdapter *)interface)->host_proxy =
                &g_adapter_proxy[adapter];
        }
    }
    if (root->vptr->slot[4](root) != 0u || !wait_for_hmi() ||
        !wait_for_trace("render\n"))
        goto done;
    deinitialize.function();
    initialized = 0;
    if (!trace_contains("query\n") || !trace_contains("query_v4\n") ||
        !trace_contains("start_v2\n") || !trace_contains("start\n") ||
        !trace_contains("surface_created\n") ||
        !trace_contains("surface_destroyed\n") ||
        !trace_contains("stale_request=-1\n") ||
        !trace_contains("stop\n"))
        goto done;
    result = 0;
done:
    if (initialized != 0 && deinitialize.function != NULL)
        deinitialize.function();
    /* Match production: callback-bearing DSOs remain pinned until exit. */
    (void)provider_handle;
    return result;
}
