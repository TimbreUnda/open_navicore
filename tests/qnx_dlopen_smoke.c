#include "open_navi.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t (*VersionCheckFunction)(
    PanaServerVersionHeadUnit,
    PanaClientVersionNavigationSoftwareModule *,
    uint32_t *);
typedef uint32_t (*InitializeFunction)(const void *);
typedef void *(*GetInfrastructureFunction)(void);
typedef void (*DeinitializeFunction)(void);

static void *required_symbol(void *handle, const char *name)
{
    void *symbol;
    const char *error;

    (void)dlerror();
    symbol = dlsym(handle, name);
    error = dlerror();
    if (error != NULL || symbol == NULL) {
        (void)fprintf(stderr, "dlsym(%s) failed: %s\n",
                      name, error != NULL ? error : "null symbol");
        return NULL;
    }
    return symbol;
}

int main(int argc, char **argv)
{
    const char *provider_path = argc > 1 ? argv[1] : "./libNaviCore.so";
    PanaServerVersionHeadUnit server = {{ 24u, 7u, 0u, 0u, 0u, 0u }};
    PanaClientVersionNavigationSoftwareModule client;
    PanaInitializationData initialization;
    VersionCheckFunction version_check;
    InitializeFunction initialize;
    GetInfrastructureFunction get_infrastructure;
    DeinitializeFunction deinitialize;
    PanaGetAdapterVirtual get_adapter;
    PanaRootObject *root;
    void *interface_object = NULL;
    uint32_t compatibility = UINT32_MAX;
    uint32_t status;
    void *handle;
    int initialized = 0;
    int result = 1;

    union { void *object; VersionCheckFunction function; } version_cast;
    union { void *object; InitializeFunction function; } initialize_cast;
    union { void *object; GetInfrastructureFunction function; } getter_cast;
    union { void *object; DeinitializeFunction function; } deinit_cast;

    handle = dlopen(provider_path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        (void)fprintf(stderr, "dlopen(%s) failed: %s\n",
                      provider_path, dlerror());
        return 1;
    }

    version_cast.object = required_symbol(handle, "panaVersionCheck");
    initialize_cast.object = required_symbol(handle, "panaInitialize");
    getter_cast.object = required_symbol(handle,
                                         "panaGetSoftwareInfrastructure");
    deinit_cast.object = required_symbol(handle, "panaDeinitialize");
    version_check = version_cast.function;
    initialize = initialize_cast.function;
    get_infrastructure = getter_cast.function;
    deinitialize = deinit_cast.function;
    if (version_check == NULL || initialize == NULL ||
        get_infrastructure == NULL || deinitialize == NULL) {
        goto done;
    }

    memset(&client, 0xa5, sizeof(client));
    status = version_check(server, &client, &compatibility);
    (void)printf("version: status=%u client=%u.%u compatibility=%u text=%s\n",
                 status, (unsigned int)client.api_major,
                 (unsigned int)client.api_minor, compatibility,
                 client.version_text != NULL ? client.version_text : "(null)");
    if (status != 0u || client.api_major != 24u || client.api_minor != 7u ||
        client.version_text == NULL || compatibility != 1u) {
        goto done;
    }

    memset(&initialization, 0, sizeof(initialization));
    status = initialize(&initialization);
    initialized = 1;
    (void)printf("initialize: status=%u\n", status);
    if (status != 0u) {
        goto done;
    }

    root = (PanaRootObject *)get_infrastructure();
    if (root == NULL || root->vptr == NULL || root->vptr->slot[3] == NULL) {
        (void)fprintf(stderr, "invalid software-infrastructure root\n");
        goto done;
    }

    get_adapter = (PanaGetAdapterVirtual)root->vptr->slot[3];
    status = get_adapter(root, 0u, &interface_object);
    (void)printf("getAdapter(0): status=%u interface=%p\n",
                 status, interface_object);
    if (status != 0u || interface_object == NULL ||
        *(const void **)interface_object == NULL) {
        goto done;
    }

    (void)printf("PASS: provider loader/bootstrap smoke completed\n");
    result = 0;

done:
    if (initialized != 0 && deinitialize != NULL) {
        deinitialize();
    }
    (void)dlclose(handle);
    return result;
}

