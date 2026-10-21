#define _POSIX_C_SOURCE 200112L

#include "open_navi_renderer_internal.h"

#include "open_navi_default_backend.h"

#include <string.h>

/* Forgets every installed backend level and the platform state cached for it,
 * so a replacement backend starts clean. */
static void reset_installed_backend_locked(void)
{
    memset(&g_runtime.backend.installed, 0, sizeof(g_runtime.backend.installed));
    memset(&g_runtime.backend.installed_v2, 0, sizeof(g_runtime.backend.installed_v2));
    memset(&g_runtime.backend.installed_v3, 0, sizeof(g_runtime.backend.installed_v3));
    memset(&g_runtime.backend.installed_v4, 0, sizeof(g_runtime.backend.installed_v4));
    memset(&g_runtime.backend.installed_v5, 0, sizeof(g_runtime.backend.installed_v5));
    memset(&g_runtime.backend.installed_v6, 0, sizeof(g_runtime.backend.installed_v6));
    g_runtime.backend.installed_version = 0u;
    memset(&g_runtime.vehicle, 0, sizeof(g_runtime.vehicle));
    memset(&g_runtime.gps_interval, 0, sizeof(g_runtime.gps_interval));
    memset(&g_runtime.audio, 0, sizeof(g_runtime.audio));
    memset(&g_runtime.internationalization, 0,
           sizeof(g_runtime.internationalization));
    memset(&g_runtime.cluster_display, 0, sizeof(g_runtime.cluster_display));
    memset(&g_runtime.driver_distraction, 0,
           sizeof(g_runtime.driver_distraction));
    memset(&g_runtime.guidance_stop, 0, sizeof(g_runtime.guidance_stop));
}

int open_navi_backend_install_v1(const OpenNaviBackendV1 *backend)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state != RENDERER_COLD) {
        result = -1;
    } else if (backend == NULL) {
        reset_installed_backend_locked();
    } else if (backend->abi_version != OPEN_NAVI_BACKEND_ABI_V1 ||
               backend->struct_size < sizeof(OpenNaviBackendV1)) {
        result = -2;
    } else {
        reset_installed_backend_locked();
        g_runtime.backend.installed = *backend;
        g_runtime.backend.installed_version = OPEN_NAVI_BACKEND_ABI_V1;
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_backend_install_v2(const OpenNaviBackendV2 *backend)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state != RENDERER_COLD) {
        result = -1;
    } else if (backend == NULL) {
        reset_installed_backend_locked();
    } else if (backend->v1.abi_version != OPEN_NAVI_BACKEND_ABI_V2 ||
               backend->v1.struct_size < sizeof(OpenNaviBackendV2)) {
        result = -2;
    } else {
        reset_installed_backend_locked();
        g_runtime.backend.installed = backend->v1;
        g_runtime.backend.installed_v2 = *backend;
        g_runtime.backend.installed_version = OPEN_NAVI_BACKEND_ABI_V2;
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_backend_install_v3(const OpenNaviBackendV3 *backend)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state != RENDERER_COLD) {
        result = -1;
    } else if (backend == NULL) {
        reset_installed_backend_locked();
    } else if (backend->v2.v1.abi_version != OPEN_NAVI_BACKEND_ABI_V3 ||
               backend->v2.v1.struct_size < sizeof(OpenNaviBackendV3) ||
               backend->start_v2 == NULL) {
        result = -2;
    } else {
        reset_installed_backend_locked();
        g_runtime.backend.installed = backend->v2.v1;
        g_runtime.backend.installed_v2 = backend->v2;
        g_runtime.backend.installed_v3 = *backend;
        g_runtime.backend.installed_version = OPEN_NAVI_BACKEND_ABI_V3;
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_backend_install_v4(const OpenNaviBackendV4 *backend)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state != RENDERER_COLD) {
        result = -1;
    } else if (backend == NULL) {
        reset_installed_backend_locked();
    } else if (backend->v3.v2.v1.abi_version != OPEN_NAVI_BACKEND_ABI_V4 ||
               backend->v3.v2.v1.struct_size < sizeof(OpenNaviBackendV4) ||
               backend->v3.start_v2 == NULL) {
        result = -2;
    } else {
        reset_installed_backend_locked();
        g_runtime.backend.installed = backend->v3.v2.v1;
        g_runtime.backend.installed_v2 = backend->v3.v2;
        g_runtime.backend.installed_v3 = backend->v3;
        g_runtime.backend.installed_v4 = *backend;
        g_runtime.backend.installed_version = OPEN_NAVI_BACKEND_ABI_V4;
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_backend_install_v5(const OpenNaviBackendV5 *backend)
{
    int result = 0;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state != RENDERER_COLD) {
        result = -1;
    } else if (backend == NULL) {
        reset_installed_backend_locked();
    } else if (backend->v4.v3.v2.v1.abi_version !=
                   OPEN_NAVI_BACKEND_ABI_V5 ||
               backend->v4.v3.v2.v1.struct_size < sizeof(OpenNaviBackendV5) ||
               backend->v4.v3.start_v2 == NULL) {
        result = -2;
    } else {
        reset_installed_backend_locked();
        g_runtime.backend.installed = backend->v4.v3.v2.v1;
        g_runtime.backend.installed_v2 = backend->v4.v3.v2;
        g_runtime.backend.installed_v3 = backend->v4.v3;
        g_runtime.backend.installed_v4 = backend->v4;
        g_runtime.backend.installed_v5 = *backend;
        g_runtime.backend.installed_version = OPEN_NAVI_BACKEND_ABI_V5;
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

int open_navi_backend_install_v6(const OpenNaviBackendV6 *backend)
{
    int result = 0;
    if (backend != NULL &&
        (backend->v5.v4.v3.v2.v1.abi_version != OPEN_NAVI_BACKEND_ABI_V6 ||
         backend->v5.v4.v3.v2.v1.struct_size < sizeof(OpenNaviBackendV6) ||
         backend->start_v3 == NULL))
        return -2;
    (void)pthread_mutex_lock(&g_runtime.lock);
    if (g_runtime.state != RENDERER_COLD) {
        result = -1;
    } else if (backend == NULL) {
        reset_installed_backend_locked();
    } else {
        reset_installed_backend_locked();
        g_runtime.backend.installed = backend->v5.v4.v3.v2.v1;
        g_runtime.backend.installed_v2 = backend->v5.v4.v3.v2;
        g_runtime.backend.installed_v3 = backend->v5.v4.v3;
        g_runtime.backend.installed_v4 = backend->v5.v4;
        g_runtime.backend.installed_v5 = backend->v5;
        g_runtime.backend.installed_v6 = *backend;
        g_runtime.backend.installed_version = OPEN_NAVI_BACKEND_ABI_V6;
    }
    (void)pthread_mutex_unlock(&g_runtime.lock);
    return result;
}

/* Copies the installed backend, at its ABI level, into the active slots. */
void open_navi_renderer_activate_installed_locked(void)
{
    if (g_runtime.backend.installed_version == OPEN_NAVI_BACKEND_ABI_V6) {
        g_runtime.backend.active = g_runtime.backend.installed_v6.v5.v4.v3.v2.v1;
        g_runtime.backend.active_v2 = g_runtime.backend.installed_v6.v5.v4.v3.v2;
        g_runtime.backend.active_v3 = g_runtime.backend.installed_v6.v5.v4.v3;
        g_runtime.backend.active_v4 = g_runtime.backend.installed_v6.v5.v4;
        g_runtime.backend.active_v5 = g_runtime.backend.installed_v6.v5;
        g_runtime.backend.active_v6 = g_runtime.backend.installed_v6;
        g_runtime.backend.active_version = OPEN_NAVI_BACKEND_ABI_V6;
    } else if (g_runtime.backend.installed_version == OPEN_NAVI_BACKEND_ABI_V5) {
        g_runtime.backend.active = g_runtime.backend.installed_v5.v4.v3.v2.v1;
        g_runtime.backend.active_v2 = g_runtime.backend.installed_v5.v4.v3.v2;
        g_runtime.backend.active_v3 = g_runtime.backend.installed_v5.v4.v3;
        g_runtime.backend.active_v4 = g_runtime.backend.installed_v5.v4;
        g_runtime.backend.active_v5 = g_runtime.backend.installed_v5;
        g_runtime.backend.active_version = OPEN_NAVI_BACKEND_ABI_V5;
    } else if (g_runtime.backend.installed_version == OPEN_NAVI_BACKEND_ABI_V4) {
        g_runtime.backend.active = g_runtime.backend.installed_v4.v3.v2.v1;
        g_runtime.backend.active_v2 = g_runtime.backend.installed_v4.v3.v2;
        g_runtime.backend.active_v3 = g_runtime.backend.installed_v4.v3;
        g_runtime.backend.active_v4 = g_runtime.backend.installed_v4;
        g_runtime.backend.active_version = OPEN_NAVI_BACKEND_ABI_V4;
    } else if (g_runtime.backend.installed_version == OPEN_NAVI_BACKEND_ABI_V3) {
        g_runtime.backend.active = g_runtime.backend.installed_v3.v2.v1;
        g_runtime.backend.active_v2 = g_runtime.backend.installed_v3.v2;
        g_runtime.backend.active_v3 = g_runtime.backend.installed_v3;
        g_runtime.backend.active_version = OPEN_NAVI_BACKEND_ABI_V3;
    } else if (g_runtime.backend.installed_version == OPEN_NAVI_BACKEND_ABI_V2) {
        g_runtime.backend.active = g_runtime.backend.installed_v2.v1;
        g_runtime.backend.active_v2 = g_runtime.backend.installed_v2;
        g_runtime.backend.active_version = OPEN_NAVI_BACKEND_ABI_V2;
    } else if (g_runtime.backend.installed_version == OPEN_NAVI_BACKEND_ABI_V1) {
        g_runtime.backend.active = g_runtime.backend.installed;
        g_runtime.backend.active_version = OPEN_NAVI_BACKEND_ABI_V1;
    } else {
        g_runtime.backend.active = *open_navi_default_backend_v1();
        g_runtime.backend.active_version = OPEN_NAVI_BACKEND_ABI_V1;
    }
}
