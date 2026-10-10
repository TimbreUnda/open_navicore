#include <open_navicore/backend_plugin.h>

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    static const uint8_t expected_id[MOCK_NAV_PLUGIN_BUILD_ID_BYTES] =
        MOCK_NAV_PLUGIN_EXPECTED_BUILD_ID_INITIALIZER;
    MockNavBackendPluginV1 plugin;
    MockNavBackendPluginQueryV1 query;
    union {
        void *object;
        MockNavBackendPluginQueryV1 function;
    } conversion;
    void *handle;
    const char *error;

    if (argc != 2) {
        fprintf(stderr, "usage: qnx-osmand-plugin-smoke BACKEND.so\n");
        return 2;
    }
    handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 3;
    }
    (void)dlerror();
    conversion.object = dlsym(handle, "mock_nav_backend_query_v1");
    error = dlerror();
    if (error != NULL || conversion.object == NULL) {
        fprintf(stderr, "query symbol failed: %s\n",
                error != NULL ? error : "null");
        return 4;
    }
    query = conversion.function;
    memset(&plugin, 0, sizeof(plugin));
    if (query(MOCK_NAV_PLUGIN_QUERY_ABI_V1, sizeof(plugin), &plugin) != 0)
        return 5;
    if (plugin.magic0 != MOCK_NAV_PLUGIN_MAGIC0 ||
        plugin.magic1 != MOCK_NAV_PLUGIN_MAGIC1 ||
        plugin.query_abi_version != MOCK_NAV_PLUGIN_QUERY_ABI_V1 ||
        plugin.struct_size != sizeof(plugin) ||
        plugin.pointer_bytes != sizeof(void *) ||
        plugin.backend_abi_version != MOCK_NAV_BACKEND_ABI_V2 ||
        memcmp(plugin.build_id, expected_id, sizeof(expected_id)) != 0 ||
        plugin.backend.v1.start == NULL ||
        plugin.backend.v1.surface_created == NULL ||
        plugin.backend.v1.render_frame == NULL)
        return 6;
    printf("osmand-plugin:ok abi=%u backend=%u\n",
           plugin.query_abi_version, plugin.backend_abi_version);
    dlclose(handle);
    return 0;
}
