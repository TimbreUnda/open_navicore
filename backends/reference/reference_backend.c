#define _POSIX_C_SOURCE 200112L

#include <open_navicore/backend_plugin.h>

#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if !defined(OPEN_NAVI_REF_PLUGIN_HEADLESS)
#include <GLES2/gl2.h>
#include <limits.h>
#endif

#ifndef OPEN_NAVI_REF_PLUGIN_FAILURE
#define OPEN_NAVI_REF_PLUGIN_FAILURE 0
#endif

#ifndef OPEN_NAVI_REF_PLUGIN_INVALID
#define OPEN_NAVI_REF_PLUGIN_INVALID 0
#endif

#ifndef OPEN_NAVI_REF_PLUGIN_TRACE_PATH
#define OPEN_NAVI_REF_PLUGIN_TRACE_PATH "/tmp/open-navi-reference-plugin.log"
#endif

#ifndef OPEN_NAVI_REF_PLUGIN_OUTBOUND_TEST
#define OPEN_NAVI_REF_PLUGIN_OUTBOUND_TEST 0
#endif

#ifndef OPEN_NAVI_REF_PLUGIN_QUERY_V1_ONLY
#define OPEN_NAVI_REF_PLUGIN_QUERY_V1_ONLY 0
#endif

#ifndef OPEN_NAVI_REF_PLUGIN_MAX_QUERY_ABI
#define OPEN_NAVI_REF_PLUGIN_MAX_QUERY_ABI OPEN_NAVI_PLUGIN_QUERY_ABI_V4
#endif

enum {
    REF_FAIL_NONE = 0,
    REF_FAIL_START = 1,
    REF_FAIL_SURFACE = 2,
    REF_FAIL_RENDER = 3,
    REF_INVALID_NONE = 0,
    REF_INVALID_CAPABILITIES = 1,
    REF_INVALID_CALLBACK = 2,
    REF_INVALID_POINTER_SIZE = 3,
    REF_INVALID_BUILD_ID = 4,
    REF_INVALID_QUERY_STATUS = 5
};

typedef struct {
    pthread_mutex_t lock;
    pthread_t worker;
    int worker_created;
    int worker_stop;
    uint32_t render_count;
    OpenNaviBackendServicesV1 services;
    OpenNaviBackendServicesV2 services_v2;
    int services_v2_valid;
} ReferenceContext;

static ReferenceContext g_reference = {
    .lock = PTHREAD_MUTEX_INITIALIZER
};

static void trace_line(const char *line)
{
    char record[128];
    int descriptor;
    ssize_t ignored;
    size_t length = strlen(line);
    if (length + 1u >= sizeof(record)) return;
    memcpy(record, line, length);
    record[length++] = '\n';
    descriptor = open(OPEN_NAVI_REF_PLUGIN_TRACE_PATH,
                      O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (descriptor < 0) return;
    /* One O_APPEND write keeps worker/render trace records from interleaving. */
    ignored = write(descriptor, record, length);
    (void)ignored;
    (void)close(descriptor);
}

static void trace_status(const char *name, int status)
{
    char line[96];
    (void)snprintf(line, sizeof(line), "%s=%d", name, status);
    trace_line(line);
}

static void trace_stale_outbound(const OpenNaviBackendServicesV2 *services,
                                 const char *cluster_line,
                                 const char *audio_line,
                                 const char *thread_line)
{
    OpenNaviBackendClusterV1 cluster;
    OpenNaviBackendAudioAvailabilityV1 audio;
    OpenNaviBackendThreadStatusV1 thread_status;
    static const char name[] = "ref";

    memset(&cluster, 0, sizeof(cluster));
    cluster.abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
    cluster.struct_size = sizeof(cluster);
    memset(&audio, 0, sizeof(audio));
    audio.abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
    audio.struct_size = sizeof(audio);
    audio.availability_state = 1u;
    memset(&thread_status, 0, sizeof(thread_status));
    thread_status.abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
    thread_status.struct_size = sizeof(thread_status);
    thread_status.name_ascii = name;
    thread_status.name_byte_count = sizeof(name) - 1u;
    if (services->publish_cluster(services->v1.runtime_context, &cluster) ==
        OPEN_NAVI_BACKEND_SERVICE_STALE)
        trace_line(cluster_line);
    if (services->publish_audio_availability(
            services->v1.runtime_context, &audio) ==
        OPEN_NAVI_BACKEND_SERVICE_STALE)
        trace_line(audio_line);
    if (services->publish_thread_status(
            services->v1.runtime_context, &thread_status) ==
        OPEN_NAVI_BACKEND_SERVICE_STALE)
        trace_line(thread_line);
}

static void *reference_worker(void *unused)
{
    OpenNaviBackendServicesV1 services;
    OpenNaviBackendServicesV2 services_v2;
    int services_v2_valid;
    int stop;
    (void)unused;
    (void)pthread_mutex_lock(&g_reference.lock);
    services = g_reference.services;
    services_v2 = g_reference.services_v2;
    services_v2_valid = g_reference.services_v2_valid;
    stop = g_reference.worker_stop;
    (void)pthread_mutex_unlock(&g_reference.lock);
    if (stop == 0 && services.request_frame != NULL)
        trace_status("worker_request", services.request_frame(
            services.runtime_context));
    (void)services_v2;
    (void)services_v2_valid;
    trace_line("worker_exit");
    return NULL;
}

static int reference_start(void *context,
                           const OpenNaviBackendServicesV1 *services)
{
    ReferenceContext *reference = (ReferenceContext *)context;
    int start_request = -1;
    trace_line("start");
    if (OPEN_NAVI_REF_PLUGIN_FAILURE == REF_FAIL_START) {
        trace_line("start_fail");
        return -101;
    }
    (void)pthread_mutex_lock(&reference->lock);
    reference->services = *services;
    reference->worker_stop = 0;
    reference->worker_created = 0;
    reference->render_count = 0u;
    if (services->request_frame != NULL)
        start_request = services->request_frame(services->runtime_context);
    if (pthread_create(&reference->worker, NULL,
                       reference_worker, NULL) == 0)
        reference->worker_created = 1;
    (void)pthread_mutex_unlock(&reference->lock);
    trace_status("start_request", start_request);
    return 0;
}

static int reference_start_v2(
    void *context, const OpenNaviBackendServicesV2 *services)
{
    ReferenceContext *reference = (ReferenceContext *)context;
    int result;
    trace_line("start_v2");
    if (services == NULL ||
        services->v1.abi_version != OPEN_NAVI_BACKEND_ABI_V2 ||
        services->v1.struct_size < sizeof(*services) ||
        services->publish_cluster == NULL ||
        services->publish_audio_availability == NULL ||
        services->publish_thread_status == NULL) {
        return -107;
    }
    (void)pthread_mutex_lock(&reference->lock);
    reference->services_v2 = *services;
    reference->services_v2_valid = 1;
    (void)pthread_mutex_unlock(&reference->lock);
    result = reference_start(context, &services->v1);
    if (result != 0) {
        trace_stale_outbound(services,
                             "failed_start_cluster_stale",
                             "failed_start_audio_stale",
                             "failed_start_thread_stale");
        (void)pthread_mutex_lock(&reference->lock);
        reference->services_v2_valid = 0;
        (void)pthread_mutex_unlock(&reference->lock);
    }
    return result;
}

static void reference_stop(void *context)
{
    ReferenceContext *reference = (ReferenceContext *)context;
    pthread_t worker;
    OpenNaviBackendServicesV2 services_v2;
    int services_v2_valid;
    int join;
    int stale = -999;
    (void)pthread_mutex_lock(&reference->lock);
    reference->worker_stop = 1;
    worker = reference->worker;
    join = reference->worker_created;
    services_v2 = reference->services_v2;
    services_v2_valid = reference->services_v2_valid;
    reference->worker_created = 0;
    reference->services_v2_valid = 0;
    (void)pthread_mutex_unlock(&reference->lock);
    if (join != 0) {
        (void)pthread_join(worker, NULL);
        trace_line("worker_joined");
    }
    if (reference->services.request_frame != NULL)
        stale = reference->services.request_frame(
            reference->services.runtime_context);
    trace_status("stale_request", stale);
    if (services_v2_valid != 0)
        trace_stale_outbound(&services_v2,
                             "stop_cluster_stale",
                             "stop_audio_stale",
                             "stop_thread_stale");
    trace_line("stop");
}

static int reference_surface_created(
    void *context, const OpenNaviBackendRenderTargetV1 *target)
{
    (void)context;
    trace_line("surface_created");
    if (target == NULL || target->width == 0u || target->height == 0u)
        return -102;
    if (OPEN_NAVI_REF_PLUGIN_FAILURE == REF_FAIL_SURFACE) {
        trace_line("surface_fail");
        return -103;
    }
    return 0;
}

static void reference_surface_destroyed(void *context)
{
    (void)context;
    trace_line("surface_destroyed");
}

#define REFERENCE_EVENT(name, type)                                           \
    static int name(void *context, const type *event)                         \
    {                                                                         \
        (void)context;                                                        \
        (void)event;                                                          \
        trace_line(#name);                                                    \
        return 0;                                                             \
    }

REFERENCE_EVENT(reference_visibility, OpenNaviBackendVisibilityV1)
REFERENCE_EVENT(reference_screen, OpenNaviBackendScreenV1)
REFERENCE_EVENT(reference_gps, OpenNaviBackendGpsV1)
REFERENCE_EVENT(reference_pointer, OpenNaviBackendPointerV1)
REFERENCE_EVENT(reference_vehicle, OpenNaviBackendVehicleV2)
REFERENCE_EVENT(reference_gps_interval, OpenNaviBackendGpsIntervalV2)
REFERENCE_EVENT(reference_audio, OpenNaviBackendAudioV2)

static int reference_internationalization(
    void *context, const OpenNaviBackendInternationalizationV1 *event)
{
    char line[128];
    (void)context;
    if (event == NULL) return -1;
    (void)snprintf(line, sizeof(line),
                   "reference_internationalization slot=%u handle=%u "
                   "present=%u language_raw=%u date_raw=%u time_raw=%u",
                   (unsigned int)event->source_slot,
                   (unsigned int)event->request_handle,
                   (unsigned int)event->settings_present,
                   (unsigned int)event->language_raw,
                   (unsigned int)event->date_format_raw,
                   (unsigned int)event->time_format_raw);
    trace_line(line);
    return 0;
}

static int reference_cluster_display(
    void *context, const OpenNaviBackendClusterDisplayV1 *event)
{
    char line[96];
    (void)context;
    if (event == NULL) return -1;
    (void)snprintf(line, sizeof(line),
                   "reference_cluster_display slot=%u handle=%u enabled_raw=%u",
                   (unsigned int)event->source_slot,
                   (unsigned int)event->request_handle,
                   (unsigned int)event->enabled_raw);
    trace_line(line);
    return 0;
}

static int reference_driver_distraction(
    void *context, const OpenNaviBackendDriverDistractionV1 *event)
{
    char line[112];
    (void)context;
    if (event == NULL) return -1;
    (void)snprintf(line, sizeof(line),
                   "reference_driver_distraction slot=%u handle=%u status_raw=%u",
                   (unsigned int)event->source_slot,
                   (unsigned int)event->request_handle,
                   (unsigned int)event->distraction_status_raw);
    trace_line(line);
    return 0;
}

static int reference_render(void *context,
                            const OpenNaviBackendRenderTargetV1 *target,
                            const OpenNaviBackendFrameV1 *frame)
{
    ReferenceContext *reference = (ReferenceContext *)context;
    uint32_t count;
    (void)frame;
    (void)pthread_mutex_lock(&reference->lock);
    count = ++reference->render_count;
    (void)pthread_mutex_unlock(&reference->lock);
    trace_line("render");
    if (OPEN_NAVI_REF_PLUGIN_OUTBOUND_TEST != 0 && count == 1u) {
        OpenNaviBackendServicesV2 services;
        OpenNaviBackendClusterV1 cluster;
        OpenNaviBackendAudioAvailabilityV1 audio;
        OpenNaviBackendThreadStatusV1 thread_status;
        static const char thread_name[] = "ref-worker";
        (void)pthread_mutex_lock(&reference->lock);
        services = reference->services_v2;
        (void)pthread_mutex_unlock(&reference->lock);
        memset(&cluster, 0, sizeof(cluster));
        cluster.abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
        cluster.struct_size = sizeof(cluster);
        cluster.valid_attributes = (uint16_t)((1u << 0) | (1u << 1));
        cluster.route_guidance_state = 1u;
        cluster.navigation_status = 3u;
        memset(&audio, 0, sizeof(audio));
        audio.abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
        audio.struct_size = sizeof(audio);
        audio.availability_state = 1u;
        memset(&thread_status, 0, sizeof(thread_status));
        thread_status.abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
        thread_status.struct_size = sizeof(thread_status);
        thread_status.thread_id = 1u;
        thread_status.thread_status = 1u;
        thread_status.name_ascii = thread_name;
        thread_status.name_byte_count = sizeof(thread_name) - 1u;
        trace_line("outbound_test_trigger render=1");
        trace_line("outbound_cluster_values valid=3 route=1 navigation=3");
        trace_status("outbound_cluster", services.publish_cluster(
            services.v1.runtime_context, &cluster));
        trace_line("outbound_audio_value availability=1");
        trace_status("outbound_audio", services.publish_audio_availability(
            services.v1.runtime_context, &audio));
        trace_line("outbound_thread_values id=1 status=1 name=ref-worker");
        trace_status("outbound_thread", services.publish_thread_status(
            services.v1.runtime_context, &thread_status));
    }
    if (OPEN_NAVI_REF_PLUGIN_FAILURE == REF_FAIL_RENDER && count == 1u) {
        trace_line("render_fail");
        return -104;
    }
#if !defined(OPEN_NAVI_REF_PLUGIN_HEADLESS)
    {
        unsigned int drain;
        if (target == NULL || target->width == 0u || target->height == 0u ||
            target->width > (uint32_t)INT_MAX ||
            target->height > (uint32_t)INT_MAX) {
            trace_line("render_target_invalid");
            return -105;
        }

        /* The provider owns EGL and has already made its context current.
         * Reset every piece of GLES2 state needed by this full-surface draw;
         * a previous backend/frame must not be able to clip or mask it. */
        for (drain = 0u; drain < 16u && glGetError() != GL_NO_ERROR; ++drain) {}
        glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glViewport(0, 0, (GLsizei)target->width, (GLsizei)target->height);
        /* Cyan is intentionally distinct from the built-in magenta fallback. */
        glClearColor(0.0f, 0.85f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        if (glGetError() != GL_NO_ERROR) {
            trace_line("render_gl_error");
            return -106;
        }
    }
#else
    (void)target;
#endif
    return 0;
}

static void fill_query_v2(OpenNaviBackendPluginV2 *output_v2,
                          const uint8_t build_id_v2[32])
{
    memset(output_v2, 0, sizeof(*output_v2));
    output_v2->v1.magic0 = OPEN_NAVI_PLUGIN_MAGIC0;
    output_v2->v1.magic1 = OPEN_NAVI_PLUGIN_MAGIC1;
    output_v2->v1.query_abi_version = OPEN_NAVI_PLUGIN_QUERY_ABI_V2;
    output_v2->v1.struct_size = sizeof(*output_v2);
    output_v2->v1.endian_tag = OPEN_NAVI_PLUGIN_ENDIAN_TAG;
    output_v2->v1.pointer_bytes = sizeof(void *);
    output_v2->v1.backend_abi_version = OPEN_NAVI_BACKEND_ABI_V3;
    output_v2->v1.backend_v1_size = sizeof(OpenNaviBackendV1);
    output_v2->v1.backend_v2_size = sizeof(OpenNaviBackendV2);
    output_v2->v1.render_target_v1_size =
        sizeof(OpenNaviBackendRenderTargetV1);
    output_v2->v1.frame_v1_size = sizeof(OpenNaviBackendFrameV1);
    output_v2->v1.gps_v1_size = sizeof(OpenNaviBackendGpsV1);
    output_v2->v1.visibility_v1_size =
        sizeof(OpenNaviBackendVisibilityV1);
    output_v2->v1.screen_v1_size = sizeof(OpenNaviBackendScreenV1);
    output_v2->v1.pointer_v1_size = sizeof(OpenNaviBackendPointerV1);
    output_v2->v1.services_v1_size = sizeof(OpenNaviBackendServicesV1);
    output_v2->v1.vehicle_v2_size = sizeof(OpenNaviBackendVehicleV2);
    output_v2->v1.gps_interval_v2_size =
        sizeof(OpenNaviBackendGpsIntervalV2);
    output_v2->v1.audio_v2_size = sizeof(OpenNaviBackendAudioV2);
    output_v2->v1.capabilities = OPEN_NAVI_PLUGIN_REQUIRED_CAPABILITIES;
    memcpy(output_v2->v1.build_id, build_id_v2, 32u);
    output_v2->services_v2_size = sizeof(OpenNaviBackendServicesV2);
    output_v2->cluster_v1_size = sizeof(OpenNaviBackendClusterV1);
    output_v2->utf8_span_v1_size = sizeof(OpenNaviBackendUtf8SpanV1);
    output_v2->audio_availability_v1_size =
        sizeof(OpenNaviBackendAudioAvailabilityV1);
    output_v2->thread_status_v1_size = sizeof(OpenNaviBackendThreadStatusV1);
    output_v2->backend_v3_size = sizeof(OpenNaviBackendV3);
    output_v2->capabilities_v2 = OPEN_NAVI_PLUGIN_V2_REQUIRED_CAPABILITIES;
    output_v2->backend_v3.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V3;
    output_v2->backend_v3.v2.v1.struct_size = sizeof(OpenNaviBackendV3);
    output_v2->backend_v3.v2.v1.backend_context = &g_reference;
    output_v2->backend_v3.v2.v1.stop = reference_stop;
    output_v2->backend_v3.v2.v1.surface_created = reference_surface_created;
    output_v2->backend_v3.v2.v1.surface_destroyed =
        reference_surface_destroyed;
    output_v2->backend_v3.v2.v1.visibility_changed = reference_visibility;
    output_v2->backend_v3.v2.v1.screen_changed = reference_screen;
    output_v2->backend_v3.v2.v1.gps_changed = reference_gps;
    output_v2->backend_v3.v2.v1.pointer_changed = reference_pointer;
    output_v2->backend_v3.v2.v1.render_frame = reference_render;
    output_v2->backend_v3.v2.vehicle_changed = reference_vehicle;
    output_v2->backend_v3.v2.gps_interval_changed = reference_gps_interval;
    output_v2->backend_v3.v2.audio_changed = reference_audio;
    output_v2->backend_v3.start_v2 = reference_start_v2;
    if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_CAPABILITIES)
        output_v2->capabilities_v2 &= ~OPEN_NAVI_PLUGIN_CAP_V3_SERVICES;
    else if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_CALLBACK)
        output_v2->backend_v3.v2.v1.render_frame = NULL;
    else if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_POINTER_SIZE)
        ++output_v2->v1.pointer_bytes;
    else if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_BUILD_ID)
        output_v2->v1.build_id[0] ^= 0xffu;
}

static void fill_query_v3(OpenNaviBackendPluginV3 *output_v3,
                          const uint8_t build_id_v2[32],
                          const uint8_t build_id_v3[32])
{
    OpenNaviBackendPluginV2 prefix;
    fill_query_v2(&prefix, build_id_v2);
    memset(output_v3, 0, sizeof(*output_v3));
    output_v3->v2 = prefix;
    output_v3->v2.v1.query_abi_version = OPEN_NAVI_PLUGIN_QUERY_ABI_V3;
    output_v3->v2.v1.struct_size = sizeof(*output_v3);
    output_v3->v2.v1.backend_abi_version = OPEN_NAVI_BACKEND_ABI_V4;
    memcpy(output_v3->v2.v1.build_id, build_id_v3, 32u);
    output_v3->internationalization_v1_size =
        sizeof(OpenNaviBackendInternationalizationV1);
    output_v3->cluster_display_v1_size =
        sizeof(OpenNaviBackendClusterDisplayV1);
    output_v3->backend_v4_size = sizeof(OpenNaviBackendV4);
    output_v3->capabilities_v3 = OPEN_NAVI_PLUGIN_V3_REQUIRED_CAPABILITIES;
    output_v3->backend_v4.v3 = prefix.backend_v3;
    output_v3->backend_v4.v3.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V4;
    output_v3->backend_v4.v3.v2.v1.struct_size = sizeof(OpenNaviBackendV4);
    output_v3->backend_v4.internationalization_changed =
        reference_internationalization;
    output_v3->backend_v4.cluster_display_changed = reference_cluster_display;
    if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_CAPABILITIES)
        output_v3->capabilities_v3 &=
            ~OPEN_NAVI_PLUGIN_CAP_INPUT_INTERNATIONALIZATION;
    else if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_CALLBACK)
        output_v3->backend_v4.cluster_display_changed = NULL;
    else if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_POINTER_SIZE)
        ++output_v3->v2.v1.pointer_bytes;
    else if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_BUILD_ID)
        output_v3->v2.v1.build_id[0] ^= 0xffu;
}

int open_navi_backend_query_v1(uint32_t requested_query_abi,
                              uint32_t output_size,
                              OpenNaviBackendPluginV1 *output)
{
    static const uint8_t build_id[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
        OPEN_NAVI_PLUGIN_EXPECTED_BUILD_ID_INITIALIZER;
    static const uint8_t build_id_v2[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
        OPEN_NAVI_PLUGIN_V2_EXPECTED_BUILD_ID_INITIALIZER;
    static const uint8_t build_id_v3[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
        OPEN_NAVI_PLUGIN_V3_EXPECTED_BUILD_ID_INITIALIZER;
    static const uint8_t build_id_v4[OPEN_NAVI_PLUGIN_BUILD_ID_BYTES] =
        OPEN_NAVI_PLUGIN_V4_EXPECTED_BUILD_ID_INITIALIZER;
    trace_line("query");
    if (requested_query_abi == OPEN_NAVI_PLUGIN_QUERY_ABI_V4)
        trace_line("query_v4");
    else if (requested_query_abi == OPEN_NAVI_PLUGIN_QUERY_ABI_V3)
        trace_line("query_v3");
    else if (requested_query_abi == OPEN_NAVI_PLUGIN_QUERY_ABI_V2)
        trace_line("query_v2");
    else if (requested_query_abi == OPEN_NAVI_PLUGIN_QUERY_ABI_V1)
        trace_line("query_v1");
    else
        trace_line("query_unsupported");
    if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_QUERY_STATUS)
        return -201;
    if (output == NULL)
        return -202;
    if (requested_query_abi == OPEN_NAVI_PLUGIN_QUERY_ABI_V4) {
        OpenNaviBackendPluginV4 *output_v4 =
            (OpenNaviBackendPluginV4 *)output;
        OpenNaviBackendPluginV3 prefix;
        if (OPEN_NAVI_REF_PLUGIN_QUERY_V1_ONLY != 0 ||
            OPEN_NAVI_REF_PLUGIN_MAX_QUERY_ABI < OPEN_NAVI_PLUGIN_QUERY_ABI_V4 ||
            output_size != sizeof(*output_v4)) return -202;
        fill_query_v3(&prefix, build_id_v2, build_id_v3);
        memset(output_v4, 0, sizeof(*output_v4));
        output_v4->v3 = prefix;
        output_v4->v3.v2.v1.query_abi_version =
            OPEN_NAVI_PLUGIN_QUERY_ABI_V4;
        output_v4->v3.v2.v1.struct_size = sizeof(*output_v4);
        output_v4->v3.v2.v1.backend_abi_version = OPEN_NAVI_BACKEND_ABI_V5;
        memcpy(output_v4->v3.v2.v1.build_id, build_id_v4,
               sizeof(build_id_v4));
        output_v4->driver_distraction_v1_size =
            sizeof(OpenNaviBackendDriverDistractionV1);
        output_v4->backend_v5_size = sizeof(OpenNaviBackendV5);
        output_v4->capabilities_v4 =
            OPEN_NAVI_PLUGIN_V4_REQUIRED_CAPABILITIES;
        output_v4->backend_v5.v4 = prefix.backend_v4;
        output_v4->backend_v5.v4.v3.v2.v1.abi_version =
            OPEN_NAVI_BACKEND_ABI_V5;
        output_v4->backend_v5.v4.v3.v2.v1.struct_size =
            sizeof(OpenNaviBackendV5);
        output_v4->backend_v5.driver_distraction_changed =
            reference_driver_distraction;
        if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_CAPABILITIES)
            output_v4->capabilities_v4 &=
                ~OPEN_NAVI_PLUGIN_CAP_INPUT_DRIVER_DISTRACTION;
        else if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_CALLBACK)
            output_v4->backend_v5.driver_distraction_changed = NULL;
        else if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_POINTER_SIZE)
            ++output_v4->v3.v2.v1.pointer_bytes;
        else if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_BUILD_ID)
            output_v4->v3.v2.v1.build_id[0] ^= 0xffu;
        return 0;
    }
    if (requested_query_abi == OPEN_NAVI_PLUGIN_QUERY_ABI_V3) {
        OpenNaviBackendPluginV3 *output_v3 =
            (OpenNaviBackendPluginV3 *)output;
        if (OPEN_NAVI_REF_PLUGIN_QUERY_V1_ONLY != 0 ||
            OPEN_NAVI_REF_PLUGIN_MAX_QUERY_ABI < OPEN_NAVI_PLUGIN_QUERY_ABI_V3 ||
            output_size != sizeof(*output_v3)) return -202;
        fill_query_v3(output_v3, build_id_v2, build_id_v3);
        return 0;
    }
    if (requested_query_abi == OPEN_NAVI_PLUGIN_QUERY_ABI_V2) {
        OpenNaviBackendPluginV2 *output_v2 =
            (OpenNaviBackendPluginV2 *)output;
        if (OPEN_NAVI_REF_PLUGIN_QUERY_V1_ONLY != 0 ||
            OPEN_NAVI_REF_PLUGIN_MAX_QUERY_ABI < OPEN_NAVI_PLUGIN_QUERY_ABI_V2 ||
            output_size != sizeof(*output_v2)) return -202;
        fill_query_v2(output_v2, build_id_v2);
        return 0;
    }
    if (requested_query_abi != OPEN_NAVI_PLUGIN_QUERY_ABI_V1 ||
        output_size != sizeof(*output))
        return -202;
    memset(output, 0, sizeof(*output));
    output->magic0 = OPEN_NAVI_PLUGIN_MAGIC0;
    output->magic1 = OPEN_NAVI_PLUGIN_MAGIC1;
    output->query_abi_version = OPEN_NAVI_PLUGIN_QUERY_ABI_V1;
    output->struct_size = sizeof(*output);
    output->endian_tag = OPEN_NAVI_PLUGIN_ENDIAN_TAG;
    output->pointer_bytes = sizeof(void *);
    output->backend_abi_version = OPEN_NAVI_BACKEND_ABI_V2;
    output->backend_v1_size = sizeof(OpenNaviBackendV1);
    output->backend_v2_size = sizeof(OpenNaviBackendV2);
    output->render_target_v1_size = sizeof(OpenNaviBackendRenderTargetV1);
    output->frame_v1_size = sizeof(OpenNaviBackendFrameV1);
    output->gps_v1_size = sizeof(OpenNaviBackendGpsV1);
    output->visibility_v1_size = sizeof(OpenNaviBackendVisibilityV1);
    output->screen_v1_size = sizeof(OpenNaviBackendScreenV1);
    output->pointer_v1_size = sizeof(OpenNaviBackendPointerV1);
    output->services_v1_size = sizeof(OpenNaviBackendServicesV1);
    output->vehicle_v2_size = sizeof(OpenNaviBackendVehicleV2);
    output->gps_interval_v2_size = sizeof(OpenNaviBackendGpsIntervalV2);
    output->audio_v2_size = sizeof(OpenNaviBackendAudioV2);
    output->capabilities = OPEN_NAVI_PLUGIN_REQUIRED_CAPABILITIES;
    memcpy(output->build_id, build_id, sizeof(build_id));
    output->backend.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V2;
    output->backend.v1.struct_size = sizeof(OpenNaviBackendV2);
    output->backend.v1.backend_context = &g_reference;
    output->backend.v1.start = reference_start;
    output->backend.v1.stop = reference_stop;
    output->backend.v1.surface_created = reference_surface_created;
    output->backend.v1.surface_destroyed = reference_surface_destroyed;
    output->backend.v1.visibility_changed = reference_visibility;
    output->backend.v1.screen_changed = reference_screen;
    output->backend.v1.gps_changed = reference_gps;
    output->backend.v1.pointer_changed = reference_pointer;
    output->backend.v1.render_frame = reference_render;
    output->backend.vehicle_changed = reference_vehicle;
    output->backend.gps_interval_changed = reference_gps_interval;
    output->backend.audio_changed = reference_audio;
    if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_CAPABILITIES)
        output->capabilities &= ~OPEN_NAVI_PLUGIN_CAP_RENDER;
    else if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_CALLBACK)
        output->backend.v1.render_frame = NULL;
    else if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_POINTER_SIZE)
        ++output->pointer_bytes;
    else if (OPEN_NAVI_REF_PLUGIN_INVALID == REF_INVALID_BUILD_ID)
        output->build_id[0] ^= 0xffu;
    return 0;
}
