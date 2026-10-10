#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

/* The recovered SDK's trimmed unistd.h omits these QNX libc declarations. */
extern "C" int usleep(unsigned);

/*
 * This is deliberately a narrow ABI shim.  The vendor headers are not in the
 * image, so storage sizes and callback slots below are recovered from QR39.6:
 *   CService base size                         0x94 bytes
 *   CNavigationNaviMapUpdateSessionIf size    0x60 bytes
 *   StopResponse virtual                      vptr + 0x20
 *   StartResponse virtual                     vptr + 0x24
 *
 * We still call the genuine constructors, registration methods and request
 * methods from the two vendor DSOs.  Copying the genuine vtable and replacing
 * two response slots avoids inventing the rest of CSessionIf's C++ layout.
 */

typedef int32_t PasStatus;

/*
 * libns_framework_unified.so imports these from each PAS service executable.
 * They are process-global logging/version records; zero initialization selects
 * the framework defaults.  Keep deliberately generous aligned storage because
 * the vendor declarations are unavailable and framework builds have grown the
 * logging record over time.
 */
extern "C" {
uint32_t g_PasLogParams[256] = { 0 };
uint32_t g_PasVersion[64] = { 0 };
}

typedef void (*ServiceCtor)(void *, std::string &);
typedef void (*ObjectDtor)(void *);
typedef int (*ServiceRun)(void *, int, char **, char *);
typedef void (*FrameworkDestroy)(void *);
typedef void (*SessionCtor)(void *, void *, unsigned);
typedef void (*VoidMethod)(void *);
typedef PasStatus (*SessionMethod)(void *);
typedef PasStatus (*SendRequest)(void *, unsigned);

static ServiceCtor service_ctor;
static ObjectDtor service_dtor;
static ServiceRun service_run;
static FrameworkDestroy framework_destroy;
static SessionCtor session_ctor;
static ObjectDtor session_dtor;
static VoidMethod session_register_callbacks;
static VoidMethod session_register_pairs;
static SessionMethod session_is_active;
static SendRequest send_stop;
static SendRequest send_start;

enum Action { ACTION_STOP, ACTION_START, ACTION_PROBE };

static uint64_t g_service_storage[0x94 / sizeof(uint64_t) + 1];
static uint64_t g_session_storage[0x60 / sizeof(uint64_t)];
static void *g_service_vtable[16];
static void *g_session_vtable[16];
static void **g_vendor_service_vtable;
static void **g_vendor_session_vtable;
static volatile sig_atomic_t g_response_seen;
static volatile sig_atomic_t g_result = 125;
static volatile sig_atomic_t g_shutdown_started;
static volatile sig_atomic_t g_session_constructed;
static volatile sig_atomic_t g_thread_started;
static Action g_action;
static unsigned g_txid = 1;
static pthread_t g_thread;

static void **get_vtable(void *object)
{
    void **value;
    memcpy(&value, object, sizeof(value));
    return value;
}

static void set_vtable(void *object, void **value)
{
    memcpy(object, &value, sizeof(value));
}

static void *required_symbol(void *handle, const char *name)
{
    void *value = dlsym(handle, name);
    if (!value) {
        fprintf(stderr, "navicore_reload: missing vendor symbol %s: %s\n",
                name, dlerror());
        exit(69);
    }
    return value;
}

template <typename T> static void load_symbol(T &target, void *handle,
                                               const char *name)
{
    void *value = required_symbol(handle, name);
    memcpy(&target, &value, sizeof(target));
}

static void load_vendor_abi()
{
    void *framework = dlopen("libns_framework_unified.so", RTLD_NOW | RTLD_GLOBAL);
    if (!framework) {
        fprintf(stderr, "navicore_reload: cannot load libns_framework_unified.so: %s\n",
                dlerror());
        exit(69);
    }
    void *navigation = dlopen("libnv_navigation_if.so", RTLD_NOW | RTLD_GLOBAL);
    if (!navigation) {
        fprintf(stderr, "navicore_reload: cannot load libnv_navigation_if.so: %s\n",
                dlerror());
        exit(69);
    }

    load_symbol(service_ctor, framework, "_ZN2ns9framework8CServiceC2ERSs");
    load_symbol(service_dtor, framework, "_ZN2ns9framework8CServiceD2Ev");
    load_symbol(service_run, framework, "_ZN2ns9framework8CService3RunEiPPcS2_");
    load_symbol(framework_destroy, framework, "_ZN2ns9framework10CFramework7DestroyEv");
    load_symbol(session_is_active, framework, "_ZN2ns9framework10CSessionIf8IsActiveEv");
    load_symbol(session_ctor, navigation,
        "_ZN2nv10navigation33CNavigationNaviMapUpdateSessionIfC1EPN2ns9framework10CFrameworkEj");
    load_symbol(session_dtor, navigation,
        "_ZN2nv10navigation33CNavigationNaviMapUpdateSessionIfD1Ev");
    load_symbol(session_register_callbacks, navigation,
        "_ZN2nv10navigation33CNavigationNaviMapUpdateSessionIf17RegisterCallbacksEv");
    load_symbol(session_register_pairs, navigation,
        "_ZN2nv10navigation33CNavigationNaviMapUpdateSessionIf21RegisterReqsWithRespsEv");
    load_symbol(send_stop, navigation,
        "_ZN2nv10navigation33CNavigationNaviMapUpdateSessionIf33SendMapUpdate_NaviCoreStopRequestEj");
    load_symbol(send_start, navigation,
        "_ZN2nv10navigation33CNavigationNaviMapUpdateSessionIf34SendMapUpdate_NaviCoreStartRequestEj");
}

static void shutdown_framework(int result)
{
    if (!g_shutdown_started) {
        g_shutdown_started = 1;
        g_result = result;
        framework_destroy(g_service_storage);
    }
}

static void finish_response(PasStatus status)
{
    g_response_seen = 1;
    fprintf(stderr, "navicore_reload: %s response status=%ld\n",
            g_action == ACTION_STOP ? "stop" : "start", (long)status);
    shutdown_framework(status == 0 ? 0 : 3);
}

extern "C" PasStatus on_stop_response(void *, PasStatus status)
{
    if (g_action == ACTION_STOP)
        finish_response(status);
    return 0;
}

extern "C" PasStatus on_start_response(void *, PasStatus status)
{
    if (g_action == ACTION_START)
        finish_response(status);
    return 0;
}

extern "C" PasStatus on_service_started(void *)
{
    return 0;
}

static void *request_thread(void *)
{
    unsigned waited_ms = 0;
    while (!session_is_active(g_session_storage)) {
        if (waited_ms >= 15000) {
            fprintf(stderr, "navicore_reload: nv_navigation session did not become active\n");
            shutdown_framework(4);
            return 0;
        }
        usleep(100000);
        waited_ms += 100;
    }

    if (g_action == ACTION_PROBE) {
        fprintf(stderr, "navicore_reload: nv_navigation session is active\n");
        g_response_seen = 1;
        shutdown_framework(0);
        return 0;
    }

    PasStatus status = g_action == ACTION_STOP
        ? send_stop(g_session_storage, g_txid)
        : send_start(g_session_storage, g_txid);
    fprintf(stderr, "navicore_reload: queued %s txid=%u status=%ld\n",
            g_action == ACTION_STOP ? "stop" : "start", g_txid, (long)status);
    if (status != 0) {
        shutdown_framework(2);
        return 0;
    }

    waited_ms = 0;
    while (!g_response_seen && waited_ms < 30000) {
        usleep(100000);
        waited_ms += 100;
    }
    if (!g_response_seen) {
        fprintf(stderr, "navicore_reload: timed out waiting for PAS response\n");
        shutdown_framework(124);
    }
    return 0;
}

/* CService slot 2 runs after its CFramework dispatcher is initialized. */
extern "C" PasStatus on_service_initialize(void *, void *)
{
    session_ctor(g_session_storage, g_service_storage, 0);
    g_session_constructed = 1;
    session_register_pairs(g_session_storage);
    session_register_callbacks(g_session_storage);

    g_vendor_session_vtable = get_vtable(g_session_storage);
    memcpy(g_session_vtable, g_vendor_session_vtable, sizeof(g_session_vtable));
    g_session_vtable[8] = reinterpret_cast<void *>(&on_stop_response);
    g_session_vtable[9] = reinterpret_cast<void *>(&on_start_response);
    set_vtable(g_session_storage, g_session_vtable);

    int status = pthread_create(&g_thread, 0, request_thread, 0);
    if (status != 0) {
        fprintf(stderr, "navicore_reload: pthread_create failed: %s\n",
                strerror(status));
        shutdown_framework(6);
    } else {
        g_thread_started = 1;
    }
    return 0;
}

static int usage(const char *name)
{
    fprintf(stderr, "usage: %s {stop|start|probe} [transaction-id]\n", name);
    return 64;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
        return usage(argv[0]);
    if (strcmp(argv[1], "stop") == 0)
        g_action = ACTION_STOP;
    else if (strcmp(argv[1], "start") == 0)
        g_action = ACTION_START;
    else if (strcmp(argv[1], "probe") == 0)
        g_action = ACTION_PROBE;
    else
        return usage(argv[0]);

    if (argc == 3) {
        char *end = 0;
        unsigned long value = strtoul(argv[2], &end, 0);
        if (!end || *end || value > 0xffffffffUL)
            return usage(argv[0]);
        g_txid = (unsigned)value;
    }

    load_vendor_abi();

    /* A unique client identity avoids colliding with the running stock tool. */
    std::string service_name("navicore_reload");
    memset(g_service_storage, 0, sizeof(g_service_storage));
    memset(g_session_storage, 0, sizeof(g_session_storage));
    service_ctor(g_service_storage, service_name);

    g_vendor_service_vtable = get_vtable(g_service_storage);
    memcpy(g_service_vtable, g_vendor_service_vtable, sizeof(g_service_vtable));
    g_service_vtable[2] = reinterpret_cast<void *>(&on_service_initialize);
    g_service_vtable[7] = reinterpret_cast<void *>(&on_service_started);
    set_vtable(g_service_storage, g_service_vtable);

    /* Match the stock main: Run(argc, argv, NULL), excluding our own verb. */
    char *service_argv[] = { argv[0], 0 };
    int run_status = service_run(g_service_storage, 1, service_argv, 0);
    if (g_thread_started)
        (void)pthread_join(g_thread, 0);

    /* Destroy using the genuine vtable/destructors after the dispatcher stops. */
    if (g_session_constructed) {
        set_vtable(g_session_storage, g_vendor_session_vtable);
        session_dtor(g_session_storage);
    }
    set_vtable(g_service_storage, g_vendor_service_vtable);
    service_dtor(g_service_storage);

    if (!g_response_seen) {
        fprintf(stderr, "navicore_reload: framework exited before a response (Run=%d)\n",
                run_status);
        if (g_shutdown_started)
            return g_result;
        return 7;
    }
    return g_result;
}
