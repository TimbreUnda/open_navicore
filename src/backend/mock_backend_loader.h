#ifndef MOCK_BACKEND_LOADER_H
#define MOCK_BACKEND_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include <open_navicore/backend_plugin.h>

enum MockBackendFallbackReason {
    MOCK_BACKEND_FALLBACK_NONE = 0,
    MOCK_BACKEND_FALLBACK_LOAD = 1,
    MOCK_BACKEND_FALLBACK_SYMBOL = 2,
    MOCK_BACKEND_FALLBACK_QUERY = 3,
    MOCK_BACKEND_FALLBACK_HANDSHAKE = 4,
    MOCK_BACKEND_FALLBACK_INSTALL = 5,
    MOCK_BACKEND_FALLBACK_START = 6,
    MOCK_BACKEND_FALLBACK_SURFACE = 7,
    MOCK_BACKEND_FALLBACK_RENDER = 8
};

/* Idempotent process-lifetime bootstrap. It never unloads a DSO. */
void mock_backend_loader_bootstrap(void);

int mock_backend_plugin_validate(const MockNavBackendPluginV1 *plugin,
                                 char *diagnostic,
                                 size_t diagnostic_capacity);
int mock_backend_plugin_validate_v2(const MockNavBackendPluginV2 *plugin,
                                    char *diagnostic,
                                    size_t diagnostic_capacity);
int mock_backend_plugin_validate_v3(const MockNavBackendPluginV3 *plugin,
                                    char *diagnostic,
                                    size_t diagnostic_capacity);
int mock_backend_plugin_validate_v4(const MockNavBackendPluginV4 *plugin,
                                    char *diagnostic,
                                    size_t diagnostic_capacity);

typedef struct {
    uint32_t bootstrap_calls;
    uint32_t load_attempts;
    uint32_t query_calls;
    uint32_t wrapper_starts;
    uint32_t wrapper_stops;
    uint32_t plugin_starts;
    uint32_t plugin_stops;
    uint32_t default_starts;
    uint32_t default_stops;
    uint32_t plugin_surface_destroys;
    uint32_t default_surface_destroys;
    uint32_t fallback_reason;
    uint32_t handle_pinned;
    uint32_t plugin_valid;
    uint32_t query_abi;
    uint32_t outbound_accepted;
    uint32_t outbound_rejected;
} MockBackendLoaderTestState;

#if defined(MOCK_NAVICORE_TESTING)
void mock_backend_loader_test_state(MockBackendLoaderTestState *output);
#endif

#endif
