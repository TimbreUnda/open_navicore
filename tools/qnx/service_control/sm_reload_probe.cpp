#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <string>

#include "sm_reload_abi.h"

extern "C" int usleep(unsigned);
extern "C" int getpid(void);

extern "C" {
uint32_t g_PasLogParams[256] = { 0 };
uint32_t g_PasVersion[64] = { 0 };
}

typedef int32_t PasStatus;
typedef void (*ServiceCtor)(void *, std::string &);
typedef void (*ObjectDtor)(void *);
typedef int (*ServiceRun)(void *, int, char **, char *);
typedef void (*FrameworkDestroy)(void *);
typedef void (*SessionCtor)(void *, void *, unsigned);
typedef void (*VoidMethod)(void *);
typedef PasStatus (*SessionMethod)(void *);
typedef PasStatus (*SendStartRequest)(void *, SmServiceStartInfo &, unsigned);
typedef PasStatus (*SessionCreationResponse)(void *, PasStatus, std::string &,
                                             unsigned);

enum Action { ACTION_PROBE, ACTION_START };

static ServiceCtor service_ctor;
static ObjectDtor service_dtor;
static ServiceRun service_run;
static FrameworkDestroy framework_destroy;
static SessionCtor session_ctor;
static ObjectDtor session_dtor;
static VoidMethod session_register_callbacks;
static VoidMethod session_register_pairs;
static SessionMethod session_is_active;
static SendStartRequest send_start;

static uint64_t g_service_storage[0x98 / sizeof(uint64_t)];
static uint64_t g_session_storage[0x60 / sizeof(uint64_t)];
static void *g_service_vtable[16];
static void *g_session_vtable[16];
static void **g_vendor_service_vtable;
static void **g_vendor_session_vtable;
static volatile sig_atomic_t g_shutdown_started;
static volatile sig_atomic_t g_session_constructed;
static volatile sig_atomic_t g_thread_started;
static volatile sig_atomic_t g_operation_complete;
static volatile sig_atomic_t g_response_seen;
static volatile sig_atomic_t g_result = 125;
static volatile sig_atomic_t g_transport_status = -1;
static volatile sig_atomic_t g_operation_status = -1;
static volatile sig_atomic_t g_response_name_matches;
static volatile sig_atomic_t g_constructor_returned;
static volatile sig_atomic_t g_pairs_registered;
static volatile sig_atomic_t g_callbacks_registered;
static volatile sig_atomic_t g_creation_seen;
static volatile sig_atomic_t g_creation_status = -1;
static pthread_t g_thread;
static Action g_action = ACTION_PROBE;
static unsigned g_txid;
static SessionCreationResponse g_vendor_creation_response;

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
        fprintf(stderr, "sm_reload_probe: missing vendor symbol %s: %s\n",
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
        fprintf(stderr, "sm_reload_probe: cannot load framework: %s\n", dlerror());
        exit(69);
    }
    void *system_manager = dlopen("libss_service_if_unified.so",
                                  RTLD_NOW | RTLD_GLOBAL);
    if (!system_manager) {
        fprintf(stderr, "sm_reload_probe: cannot load system-manager IF: %s\n",
                dlerror());
        exit(69);
    }

    load_symbol(service_ctor, framework, "_ZN2ns9framework8CServiceC2ERSs");
    load_symbol(service_dtor, framework, "_ZN2ns9framework8CServiceD2Ev");
    load_symbol(service_run, framework, "_ZN2ns9framework8CService3RunEiPPcS2_");
    load_symbol(framework_destroy, framework,
                "_ZN2ns9framework10CFramework7DestroyEv");
    load_symbol(session_is_active, framework,
                "_ZN2ns9framework10CSessionIf8IsActiveEv");
    load_symbol(session_ctor, system_manager,
                "_ZN2ss13systemmanager34CSystemManagerTestControlSessionIfC1EPN2ns9framework10CFrameworkEj");
    load_symbol(session_dtor, system_manager,
                "_ZN2ss13systemmanager34CSystemManagerTestControlSessionIfD1Ev");
    load_symbol(session_register_callbacks, system_manager,
                "_ZN2ss13systemmanager34CSystemManagerTestControlSessionIf17RegisterCallbacksEv");
    load_symbol(session_register_pairs, system_manager,
                "_ZN2ss13systemmanager34CSystemManagerTestControlSessionIf21RegisterReqsWithRespsEv");
    load_symbol(send_start, system_manager,
                "_ZN2ss13systemmanager34CSystemManagerTestControlSessionIf19SendAppStartRequestER20_SS_ServiceStartInfoj");
}

extern "C" PasStatus on_session_creation_response(void *self,
                                                   PasStatus status,
                                                   std::string &server_name,
                                                   unsigned transaction)
{
    PasStatus result = g_vendor_creation_response(
        self, status, server_name, transaction);
    fprintf(stderr,
            "sm_reload_probe: SessionCreationResponse target=%s status=%ld txid=%u base=%ld\n",
            server_name.c_str(), (long)status, transaction, (long)result);
    g_creation_status = status;
    __sync_synchronize();
    g_creation_seen = 1;
    return result;
}

static void shutdown_framework(int result)
{
    if (!g_shutdown_started) {
        g_shutdown_started = 1;
        g_result = result;
        framework_destroy(g_service_storage);
    }
}

static int exact_service_name(const char *name)
{
    static const char expected[] = "nv_navigation";
    return name != 0 && memcmp(name, expected, sizeof(expected)) == 0;
}

/* Proven generated virtual: vptr slot 9. */
extern "C" PasStatus on_start_response(void *, PasStatus transport_status,
                                        SmStartResponseInfo &info)
{
    g_transport_status = transport_status;
    g_operation_status = (sig_atomic_t)info.operation_result;
    g_response_name_matches = exact_service_name(info.service_name);
    __sync_synchronize();
    g_response_seen = 1;
    return 0;
}

static unsigned make_transaction_id()
{
    unsigned pid_part = ((unsigned)getpid() & 0xffffu) << 16;
    unsigned time_part = (unsigned)time(0) & 0xffffu;
    return pid_part ^ time_part ^ 0x534du;
}

static void *operation_thread(void *)
{
    unsigned elapsed_ms = 0;
    while (!session_is_active(g_session_storage)) {
        if (elapsed_ms >= 30000) {
            fprintf(stderr,
                    "sm_reload_probe: ss_system_manager session activation timed out "
                    "ctor=%d pairs=%d callbacks=%d creation_seen=%d creation_status=%ld\n",
                    (int)g_constructor_returned, (int)g_pairs_registered,
                    (int)g_callbacks_registered, (int)g_creation_seen,
                    (long)g_creation_status);
            shutdown_framework(124);
            return 0;
        }
        usleep(100000);
        elapsed_ms += 100;
    }

    if (g_action == ACTION_PROBE) {
        fprintf(stderr,
                "sm_reload_probe: ss_system_manager test-control session is active\n");
        g_operation_complete = 1;
        shutdown_framework(0);
        return 0;
    }

    SmServiceStartInfo request;
    memset(&request, 0, sizeof(request));
    memcpy(request.service_name, "nv_navigation", sizeof("nv_navigation"));
    request.user_mode = 0;
    request.startup_reason = 0;
    g_txid = make_transaction_id();

    PasStatus immediate = send_start(g_session_storage, request, g_txid);
    fprintf(stderr,
            "sm_reload_probe: queued start service=nv_navigation txid=%u status=%ld\n",
            g_txid, (long)immediate);
    if (immediate != 0) {
        shutdown_framework(2);
        return 0;
    }

    while (!g_response_seen && elapsed_ms < 30000) {
        usleep(100000);
        elapsed_ms += 100;
    }
    if (!g_response_seen) {
        fprintf(stderr,
                "sm_reload_probe: AppStartResponse timed out (txid=%u)\n", g_txid);
        shutdown_framework(124);
        return 0;
    }

    __sync_synchronize();
    fprintf(stderr,
            "sm_reload_probe: AppStartResponse transport=%ld operation=%ld service_match=%d\n",
            (long)g_transport_status, (long)g_operation_status,
            (int)g_response_name_matches);
    if (g_transport_status != 0 || g_operation_status != 0 ||
        !g_response_name_matches) {
        shutdown_framework(3);
        return 0;
    }

    g_operation_complete = 1;
    shutdown_framework(0);
    return 0;
}

extern "C" PasStatus on_service_initialize(void *, void *)
{
    fprintf(stderr,
            "sm_reload_probe: constructing target=ss_system_manager type=5 instance=1\n");
    session_ctor(g_session_storage, g_service_storage, 1);
    g_session_constructed = 1;
    g_constructor_returned = 1;

    g_vendor_session_vtable = get_vtable(g_session_storage);
    memcpy(g_session_vtable, g_vendor_session_vtable, sizeof(g_session_vtable));
    g_vendor_creation_response =
        reinterpret_cast<SessionCreationResponse>(g_session_vtable[2]);
    g_session_vtable[2] =
        reinterpret_cast<void *>(&on_session_creation_response);
    g_session_vtable[9] = reinterpret_cast<void *>(&on_start_response);
    set_vtable(g_session_storage, g_session_vtable);
    fprintf(stderr,
            "sm_reload_probe: constructor returned vptr=%p; callback shim installed\n",
            (void *)g_vendor_session_vtable);

    session_register_pairs(g_session_storage);
    g_pairs_registered = 1;
    fprintf(stderr, "sm_reload_probe: RegisterReqsWithResps returned\n");
    session_register_callbacks(g_session_storage);
    g_callbacks_registered = 1;
    fprintf(stderr, "sm_reload_probe: RegisterCallbacks returned\n");

    int status = pthread_create(&g_thread, 0, operation_thread, 0);
    if (status != 0) {
        fprintf(stderr, "sm_reload_probe: pthread_create failed: %s\n",
                strerror(status));
        shutdown_framework(6);
    } else {
        g_thread_started = 1;
    }
    return 0;
}

extern "C" PasStatus on_service_started(void *)
{
    return 0;
}

static int usage(const char *name)
{
    fprintf(stderr, "usage: %s {probe|start nv_navigation}\n", name);
    return 64;
}

int main(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "probe") == 0)) {
        g_action = ACTION_PROBE;
    } else if (argc == 3 && strcmp(argv[1], "start") == 0 &&
               strcmp(argv[2], "nv_navigation") == 0) {
        g_action = ACTION_START;
    } else {
        return usage(argv[0]);
    }

    load_vendor_abi();
    /* Reserved diagnostic client queue in QR39.5/QR39.6 ns_mq.cfg. */
    std::string service_name("ss_service_lifecycle_test_client");
    memset(g_service_storage, 0, sizeof(g_service_storage));
    memset(g_session_storage, 0, sizeof(g_session_storage));
    service_ctor(g_service_storage, service_name);

    g_vendor_service_vtable = get_vtable(g_service_storage);
    memcpy(g_service_vtable, g_vendor_service_vtable, sizeof(g_service_vtable));
    g_service_vtable[2] = reinterpret_cast<void *>(&on_service_initialize);
    g_service_vtable[7] = reinterpret_cast<void *>(&on_service_started);
    set_vtable(g_service_storage, g_service_vtable);

    char *service_argv[] = { argv[0], 0 };
    int run_status = service_run(g_service_storage, 1, service_argv, 0);
    if (g_thread_started)
        (void)pthread_join(g_thread, 0);

    if (g_session_constructed) {
        set_vtable(g_session_storage, g_vendor_session_vtable);
        session_dtor(g_session_storage);
    }
    set_vtable(g_service_storage, g_vendor_service_vtable);
    service_dtor(g_service_storage);

    if (!g_operation_complete) {
        fprintf(stderr,
                "sm_reload_probe: framework exited before completion (Run=%d)\n",
                run_status);
        return g_shutdown_started ? g_result : 7;
    }
    return g_result;
}
