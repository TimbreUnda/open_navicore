#define _POSIX_C_SOURCE 200112L

#include "mock_gps_backend.h"
#include <open_navicore/backend.h>
#include "mock_renderer.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

enum {
    EVENT_START = 1,
    EVENT_SURFACE = 2,
    EVENT_RENDER = 3,
    EVENT_VISIBILITY = 4,
    EVENT_SCREEN = 5,
    EVENT_GPS = 6,
    EVENT_POINTER = 7,
    EVENT_SURFACE_DESTROYED = 8,
    EVENT_STOP = 9,
    EVENT_VEHICLE = 10,
    EVENT_GPS_INTERVAL = 11,
    EVENT_AUDIO = 12,
    EVENT_CAPACITY = 128
};

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    pthread_t owner;
    int owner_set;
    unsigned int events[EVENT_CAPACITY];
    unsigned int count;
    unsigned int render_count;
    unsigned int visibility_count;
    unsigned int screen_count;
    unsigned int gps_count;
    unsigned int pointer_count;
    unsigned int vehicle_count;
    unsigned int gps_interval_count;
    unsigned int audio_count;
    int reenter_on_visibility;
    int start_status;
    int render_status;
    int event_status;
    MockNavBackendServicesV1 services;
    MockNavBackendServicesV2 services_v2;
    MockNavBackendRenderTargetV1 target;
    MockNavBackendVisibilityV1 visibility;
    MockNavBackendScreenV1 screen;
    MockNavBackendGpsV1 gps;
    MockNavBackendPointerV1 pointer;
    MockNavBackendPointerV2 contact[2];  /* last V2 payload per contact */
    unsigned int v2_pointer_count;
    MockNavBackendVehicleV2 vehicle;
    MockNavBackendGpsIntervalV2 gps_interval;
    MockNavBackendAudioV2 audio;
} FakeBackend;

static void delay_ms(unsigned int milliseconds)
{
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000u);
    delay.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    (void)nanosleep(&delay, NULL);
}

static void record(FakeBackend *fake, unsigned int event)
{
    (void)pthread_mutex_lock(&fake->lock);
    if (fake->owner_set == 0) {
        fake->owner = pthread_self();
        fake->owner_set = 1;
    } else {
        assert(pthread_equal(fake->owner, pthread_self()));
    }
    assert(fake->count < EVENT_CAPACITY);
    fake->events[fake->count++] = event;
    if (event == EVENT_RENDER) ++fake->render_count;
    if (event == EVENT_VISIBILITY) ++fake->visibility_count;
    if (event == EVENT_SCREEN) ++fake->screen_count;
    if (event == EVENT_GPS) ++fake->gps_count;
    if (event == EVENT_POINTER) ++fake->pointer_count;
    if (event == EVENT_VEHICLE) ++fake->vehicle_count;
    if (event == EVENT_GPS_INTERVAL) ++fake->gps_interval_count;
    if (event == EVENT_AUDIO) ++fake->audio_count;
    (void)pthread_cond_broadcast(&fake->condition);
    (void)pthread_mutex_unlock(&fake->lock);
}

static void wait_count(FakeBackend *fake, unsigned int event,
                       unsigned int expected)
{
    unsigned int attempt;
    for (attempt = 0u; attempt < 3000u; ++attempt) {
        unsigned int value;
        (void)pthread_mutex_lock(&fake->lock);
        value = event == EVENT_RENDER ? fake->render_count :
                event == EVENT_VISIBILITY ? fake->visibility_count :
                event == EVENT_SCREEN ? fake->screen_count :
                event == EVENT_GPS ? fake->gps_count :
                event == EVENT_POINTER ? fake->pointer_count :
                event == EVENT_VEHICLE ? fake->vehicle_count :
                event == EVENT_GPS_INTERVAL ? fake->gps_interval_count :
                event == EVENT_AUDIO ? fake->audio_count :
                fake->count;
        (void)pthread_mutex_unlock(&fake->lock);
        if (value >= expected) return;
        delay_ms(1u);
    }
    assert(!"timed out waiting for backend event");
}

static int fake_start(void *context, const MockNavBackendServicesV1 *services)
{
    FakeBackend *fake = (FakeBackend *)context;
    fake->services = *services;
    (void)pthread_mutex_lock(&fake->lock);
    fake->owner = pthread_self();
    fake->owner_set = 1;
    (void)pthread_mutex_unlock(&fake->lock);
    record(fake, EVENT_START);
    /* Must be safe from inside start, before renderer creation returns. */
    assert(services->request_frame(services->runtime_context) == 0);
    return fake->start_status;
}

static int fake_start_v2(void *context,
                         const MockNavBackendServicesV2 *services)
{
    FakeBackend *fake = (FakeBackend *)context;
    fake->services_v2 = *services;
    fake->services = services->v1;
    (void)pthread_mutex_lock(&fake->lock);
    fake->owner = pthread_self();
    fake->owner_set = 1;
    (void)pthread_mutex_unlock(&fake->lock);
    record(fake, EVENT_START);
    assert(services->v1.abi_version == MOCK_NAV_BACKEND_ABI_V2);
    assert(services->v1.struct_size == sizeof(*services));
    /* Outbound services become valid only after start_v2 succeeds. */
    assert(services->publish_cluster(services->v1.runtime_context, NULL) ==
           MOCK_NAV_BACKEND_SERVICE_STALE);
    return fake->start_status;
}

static void fake_stop(void *context)
{
    record((FakeBackend *)context, EVENT_STOP);
}

static int fake_surface(void *context,
                        const MockNavBackendRenderTargetV1 *target)
{
    FakeBackend *fake = (FakeBackend *)context;
    fake->target = *target;
    record(fake, EVENT_SURFACE);
    return fake->event_status;
}

static void fake_surface_destroyed(void *context)
{
    record((FakeBackend *)context, EVENT_SURFACE_DESTROYED);
}

static int fake_visibility(void *context,
                           const MockNavBackendVisibilityV1 *visibility)
{
    FakeBackend *fake = (FakeBackend *)context;
    int reenter;
    fake->visibility = *visibility;
    record(fake, EVENT_VISIBILITY);
    (void)pthread_mutex_lock(&fake->lock);
    reenter = fake->reenter_on_visibility;
    (void)pthread_mutex_unlock(&fake->lock);
    if (reenter != 0) mock_renderer_destroy();
    return fake->event_status;
}

static int fake_screen(void *context, const MockNavBackendScreenV1 *screen)
{
    FakeBackend *fake = (FakeBackend *)context;
    fake->screen = *screen;
    record(fake, EVENT_SCREEN);
    return fake->event_status;
}

static int fake_gps(void *context, const MockNavBackendGpsV1 *gps)
{
    FakeBackend *fake = (FakeBackend *)context;
    fake->gps = *gps;
    record(fake, EVENT_GPS);
    return fake->event_status;
}

static int fake_pointer(void *context,
                        const MockNavBackendPointerV1 *pointer)
{
    FakeBackend *fake = (FakeBackend *)context;
    fake->pointer = *pointer;
    if (pointer->abi_version == MOCK_NAV_BACKEND_POINTER_ABI_V2 &&
        pointer->struct_size >= sizeof(MockNavBackendPointerV2)) {
        const MockNavBackendPointerV2 *v2 =
            (const MockNavBackendPointerV2 *)(const void *)pointer;
        assert(v2->contact_id < 2u);
        fake->contact[v2->contact_id] = *v2;
        ++fake->v2_pointer_count;
    }
    record(fake, EVENT_POINTER);
    return fake->event_status;
}

static int fake_vehicle(void *context,
                        const MockNavBackendVehicleV2 *vehicle)
{
    FakeBackend *fake = (FakeBackend *)context;
    fake->vehicle = *vehicle;
    record(fake, EVENT_VEHICLE);
    return fake->event_status;
}

static int fake_gps_interval(
    void *context, const MockNavBackendGpsIntervalV2 *gps_interval)
{
    FakeBackend *fake = (FakeBackend *)context;
    fake->gps_interval = *gps_interval;
    record(fake, EVENT_GPS_INTERVAL);
    return fake->event_status;
}

static int fake_audio(void *context, const MockNavBackendAudioV2 *audio)
{
    FakeBackend *fake = (FakeBackend *)context;
    fake->audio = *audio;
    record(fake, EVENT_AUDIO);
    return fake->event_status;
}

static int fake_render(void *context,
                       const MockNavBackendRenderTargetV1 *target,
                       const MockNavBackendFrameV1 *frame)
{
    FakeBackend *fake = (FakeBackend *)context;
    assert(target->width == 800u && target->height == 480u);
    assert(frame->sequence != 0u);
    record(fake, EVENT_RENDER);
    return fake->render_status;
}

static MockNavBackendV1 backend_for(FakeBackend *fake)
{
    MockNavBackendV1 backend;
    memset(&backend, 0, sizeof(backend));
    backend.abi_version = MOCK_NAV_BACKEND_ABI_V1;
    backend.struct_size = sizeof(backend);
    backend.backend_context = fake;
    backend.start = fake_start;
    backend.stop = fake_stop;
    backend.surface_created = fake_surface;
    backend.surface_destroyed = fake_surface_destroyed;
    backend.visibility_changed = fake_visibility;
    backend.screen_changed = fake_screen;
    backend.gps_changed = fake_gps;
    backend.pointer_changed = fake_pointer;
    backend.render_frame = fake_render;
    return backend;
}

static MockNavBackendV2 backend_v2_for(FakeBackend *fake)
{
    MockNavBackendV2 backend;
    memset(&backend, 0, sizeof(backend));
    backend.v1 = backend_for(fake);
    backend.v1.abi_version = MOCK_NAV_BACKEND_ABI_V2;
    backend.v1.struct_size = sizeof(backend);
    backend.vehicle_changed = fake_vehicle;
    backend.gps_interval_changed = fake_gps_interval;
    backend.audio_changed = fake_audio;
    return backend;
}

static MockNavBackendV3 backend_v3_for(FakeBackend *fake)
{
    MockNavBackendV3 backend;
    memset(&backend, 0, sizeof(backend));
    backend.v2 = backend_v2_for(fake);
    backend.v2.v1.abi_version = MOCK_NAV_BACKEND_ABI_V3;
    backend.v2.v1.struct_size = sizeof(backend);
    backend.start_v2 = fake_start_v2;
    return backend;
}

static void initialize_fake(FakeBackend *fake)
{
    memset(fake, 0, sizeof(*fake));
    assert(pthread_mutex_init(&fake->lock, NULL) == 0);
    assert(pthread_cond_init(&fake->condition, NULL) == 0);
}

static void *destroy_thread(void *unused)
{
    (void)unused;
    mock_renderer_destroy();
    return NULL;
}

int main(void)
{
    FakeBackend fake;
    FakeBackend failed;
    FakeBackend semantic;
    FakeBackend outbound;
    MockNavBackendV1 backend;
    MockNavBackendV2 backend_v2;
    MockNavBackendV3 backend_v3;
    MockNavBackendServicesV2 stale_services;
    MockGpsSnapshot gps;
    pthread_t first;
    pthread_t second;
    char diagnostic[257];
    unsigned int before;
    unsigned int index;
    unsigned int ordered[6];
    unsigned int ordered_count = 0u;
    unsigned int render_before;
    uint32_t speed_bits;

    initialize_fake(&fake);
    backend = backend_for(&fake);
    assert(mock_navicore_backend_install_v1(&backend) == 0);
    assert(mock_renderer_create(diagnostic, sizeof(diagnostic)) == 0);
    wait_count(&fake, EVENT_RENDER, 1u);
    assert(fake.events[0] == EVENT_START);
    assert(fake.events[1] == EVENT_SURFACE);
    assert(fake.events[2] == EVENT_RENDER);
    assert(fake.target.abi_version == MOCK_NAV_BACKEND_ABI_V1);

    /* V1 accepts recovered semantic inputs as a no-op and receives no V2
     * callback, preserving its event and binary contract. */
    before = fake.count;
    assert(mock_renderer_notify_vehicle_status(4u, 0u, 1u) == 0);
    assert(mock_renderer_notify_gps_interval(6u, 1000u) == 0);
    assert(mock_renderer_notify_audio(19u, 0u, 1u, 15u) == 0);
    delay_ms(10u);
    assert(fake.count == before);

    mock_renderer_notify_visibility(0u, 0u, 0u); /* hidden */
    wait_count(&fake, EVENT_VISIBILITY, 1u);
    assert(fake.visibility.visibility == 0u);
    mock_renderer_notify_visibility(0u, 1u, 1u); /* shown */
    wait_count(&fake, EVENT_VISIBILITY, 2u);
    assert(fake.visibility.visibility == 1u);
    assert(fake.visibility.interactivity == 1u);

    mock_renderer_notify_screen(0u, 7u, 3u);
    memset(&gps, 0, sizeof(gps));
    gps.latitude_e7 = 504501234;
    gps.longitude_e7 = 305201234;
    gps.fix_type = 3u;
    gps.sats_used = 9u;
    gps.hdop_raw = 14u;
    gps.pdop_raw = 15u;
    gps.vdop_raw = 16u;
    gps.sync_counter = 17u;
    gps.satellites[15][11] = 0xa5u;
    gps.travel_distance_raw = 123.5f;
    gps.altitude_change_raw = -4.25;
    gps.noise_raw = 0.125;
    mock_renderer_notify_gps(&gps);
    assert(mock_renderer_test_submit_pointer(MOCK_NAV_BACKEND_POINTER_PRESS,
                                             111, 222, 1u) == 0);
    assert(mock_renderer_test_submit_pointer(MOCK_NAV_BACKEND_POINTER_RELEASE,
                                             111, 222, 0u) == 0);
    wait_count(&fake, EVENT_SCREEN, 1u);
    wait_count(&fake, EVENT_GPS, 1u);
    wait_count(&fake, EVENT_POINTER, 2u);
    assert(fake.screen.screen_type == 7u);
    assert(fake.gps.latitude_e7 == 504501234);
    assert(fake.gps.longitude_e7 == 305201234);
    assert(fake.gps.hdop_raw == 14u && fake.gps.pdop_raw == 15u);
    assert(fake.gps.vdop_raw == 16u && fake.gps.sync_counter == 17u);
    assert(fake.gps.satellites[15][11] == 0xa5u);
    assert(fake.gps.travel_distance_raw == 123.5f);
    assert(fake.gps.altitude_change_raw == -4.25);
    assert(fake.gps.noise_raw == 0.125);
    assert(fake.pointer.action == MOCK_NAV_BACKEND_POINTER_RELEASE);
    assert(fake.pointer.x == 111 && fake.pointer.y == 222);
    /* Without the contacts capability a backend gets V1 payloads for the
     * first contact only. */
    assert(fake.pointer.abi_version == MOCK_NAV_BACKEND_ABI_V1);
    assert(fake.pointer.struct_size == sizeof(MockNavBackendPointerV1));
    assert(mock_renderer_test_submit_contact(MOCK_NAV_BACKEND_POINTER_PRESS,
                                             500, 50, 0u, 1u, 2u) == 0);
    assert(mock_renderer_test_submit_pointer(MOCK_NAV_BACKEND_POINTER_PRESS,
                                             7, 8, 1u) == 0);
    wait_count(&fake, EVENT_POINTER, 3u);
    delay_ms(10u);
    assert(fake.pointer_count == 3u && fake.v2_pointer_count == 0u);
    assert(fake.pointer.x == 7 && fake.pointer.y == 8);
    assert(mock_renderer_set_pointer_contacts(1) == -1);  /* not while live */
    for (index = 3u; index < fake.count && ordered_count < 6u; ++index) {
        if (fake.events[index] != EVENT_RENDER)
            ordered[ordered_count++] = fake.events[index];
    }
    assert(ordered_count == 6u);
    assert(ordered[0] == EVENT_VISIBILITY);
    assert(ordered[1] == EVENT_VISIBILITY);
    assert(ordered[2] == EVENT_SCREEN);
    assert(ordered[3] == EVENT_GPS);
    assert(ordered[4] == EVENT_POINTER);
    assert(ordered[5] == EVENT_POINTER);

    /* A rejected frame or event does not kill the renderer. */
    (void)pthread_mutex_lock(&fake.lock);
    fake.render_status = 7;
    fake.event_status = 9;
    render_before = fake.render_count;
    (void)pthread_mutex_unlock(&fake.lock);
    assert(fake.services.request_frame(fake.services.runtime_context) == 0);
    wait_count(&fake, EVENT_RENDER, render_before + 1u);
    mock_renderer_notify_gps(&gps);
    wait_count(&fake, EVENT_GPS, 2u);
    mock_renderer_notify_screen(0u, 8u, 0u);
    wait_count(&fake, EVENT_SCREEN, 2u);

    assert(pthread_create(&first, NULL, destroy_thread, NULL) == 0);
    assert(pthread_create(&second, NULL, destroy_thread, NULL) == 0);
    assert(pthread_join(first, NULL) == 0);
    assert(pthread_join(second, NULL) == 0);
    assert(fake.events[fake.count - 2u] == EVENT_SURFACE_DESTROYED);
    assert(fake.events[fake.count - 1u] == EVENT_STOP);

    /* Teardown requested from a backend callback cannot self-join or deadlock.
     * A later create waits for that detached renderer to become fully cold. */
    (void)pthread_mutex_lock(&fake.lock);
    fake.reenter_on_visibility = 1;
    fake.render_status = 0;
    fake.event_status = 0;
    before = fake.visibility_count;
    (void)pthread_mutex_unlock(&fake.lock);
    assert(mock_renderer_create(diagnostic, sizeof(diagnostic)) == 0);
    mock_renderer_notify_visibility(0u, 0u, 0u);
    wait_count(&fake, EVENT_VISIBILITY, before + 1u);
    while (mock_navicore_backend_install_v1(&backend) != 0) delay_ms(1u);
    assert(mock_renderer_create(diagnostic, sizeof(diagnostic)) == 0);
    mock_renderer_destroy();
    assert(mock_navicore_backend_install_v1(NULL) == 0);

    /* V2 retains the complete V1 behavior and adds owned semantic payloads.
     * Exercise both request/reply and notification source slots. */
    initialize_fake(&semantic);
    backend_v2 = backend_v2_for(&semantic);
    mock_renderer_prepare_lifecycle();
    assert(mock_navicore_backend_install_v2(&backend_v2) == 0);
    assert(mock_renderer_create(diagnostic, sizeof(diagnostic)) == 0);
    wait_count(&semantic, EVENT_RENDER, 1u);
    assert(mock_renderer_notify_vehicle_status(3u, 0x83u, 2u) == 0);
    wait_count(&semantic, EVENT_VEHICLE, 1u);
    assert(semantic.vehicle.source_slot == 3u);
    assert(semantic.vehicle.request_handle == 0x83u);
    assert(semantic.vehicle.day_night_status == 2u);
    assert(semantic.vehicle.valid_fields ==
           MOCK_NAV_BACKEND_VEHICLE_DAY_NIGHT_STATUS);
    assert(mock_renderer_notify_vehicle_status(4u, 0u, 1u) == 0);
    wait_count(&semantic, EVENT_VEHICLE, 2u);
    assert(semantic.vehicle.source_slot == 4u);
    assert(mock_renderer_notify_vehicle_setting(5u, 0x85u, 3u) == 0);
    wait_count(&semantic, EVENT_VEHICLE, 3u);
    assert(semantic.vehicle.source_slot == 5u);
    assert(semantic.vehicle.request_handle == 0x85u);
    assert(mock_renderer_notify_vehicle_setting(6u, 0u, 4u) == 0);
    wait_count(&semantic, EVENT_VEHICLE, 4u);
    assert(semantic.vehicle.source_slot == 6u);
    speed_bits = 0xc1540000u; /* -13.25f */
    assert(mock_renderer_notify_vehicle_speed(8u, speed_bits) == 0);
    wait_count(&semantic, EVENT_VEHICLE, 5u);
    assert(semantic.vehicle.abi_version == MOCK_NAV_BACKEND_ABI_V2);
    assert(semantic.vehicle.struct_size == sizeof(semantic.vehicle));
    assert(semantic.vehicle.valid_fields ==
           (MOCK_NAV_BACKEND_VEHICLE_DAY_NIGHT_STATUS |
            MOCK_NAV_BACKEND_VEHICLE_DAY_NIGHT_SETTING |
            MOCK_NAV_BACKEND_VEHICLE_SPEED));
    assert(semantic.vehicle.displayed_vehicle_speed_bits == speed_bits);
    assert(mock_renderer_notify_gps_interval(6u, 250u) == 0);
    assert(mock_renderer_notify_audio(17u, 0xa017u, 1u, 15u) == 0);
    wait_count(&semantic, EVENT_GPS_INTERVAL, 1u);
    wait_count(&semantic, EVENT_AUDIO, 1u);
    assert(semantic.gps_interval.source_slot == 6u);
    assert(semantic.gps_interval.interval_ms == 250u);
    assert(semantic.audio.source_slot == 17u);
    assert(semantic.audio.request_handle == 0xa017u);
    assert(semantic.audio.audio_source == 1u && semantic.audio.volume == 15u);
    assert(mock_renderer_notify_audio(19u, 0u, 7u, 0xfeu) == 0);
    wait_count(&semantic, EVENT_AUDIO, 2u);
    assert(semantic.audio.source_slot == 19u);
    assert(semantic.audio.request_handle == 0u);
    mock_renderer_destroy();
    mock_renderer_end_lifecycle();
    assert(mock_navicore_backend_install_v2(NULL) == 0);

    /* V3 alone receives the append-only outbound tail.  With no provider
     * bridge linked into this renderer-only test, a current call reports
     * unavailable; the same copied token is stale after stop and forever. */
    initialize_fake(&outbound);
    backend_v3 = backend_v3_for(&outbound);
    assert(mock_navicore_backend_install_v3(&backend_v3) == 0);
    /* With the contacts capability (set by the plugin loader) every
     * contact arrives as a V2 payload, moves coalesced per contact. */
    assert(mock_renderer_set_pointer_contacts(1) == 0);
    assert(mock_renderer_create(diagnostic, sizeof(diagnostic)) == 0);
    wait_count(&outbound, EVENT_RENDER, 1u);
    assert(mock_renderer_test_submit_contact(MOCK_NAV_BACKEND_POINTER_PRESS,
                                             100, 100, 0u, 0u, 1u) == 0);
    assert(mock_renderer_test_submit_contact(MOCK_NAV_BACKEND_POINTER_PRESS,
                                             300, 100, 0u, 1u, 2u) == 0);
    assert(mock_renderer_test_submit_contact(MOCK_NAV_BACKEND_POINTER_MOVE,
                                             310, 100, 0u, 1u, 2u) == 0);
    assert(mock_renderer_test_submit_contact(MOCK_NAV_BACKEND_POINTER_MOVE,
                                             90, 100, 0u, 0u, 2u) == 0);
    assert(mock_renderer_test_submit_contact(MOCK_NAV_BACKEND_POINTER_MOVE,
                                             320, 100, 0u, 1u, 2u) == 0);
    for (index = 0u; index < 3000u; ++index) {
        int done;
        (void)pthread_mutex_lock(&outbound.lock);
        done = outbound.contact[1].x == 320 && outbound.contact[0].x == 90;
        (void)pthread_mutex_unlock(&outbound.lock);
        if (done) break;
        delay_ms(1u);
    }
    assert(outbound.contact[0].x == 90 && outbound.contact[1].x == 320);
    assert(outbound.contact[1].contact_count == 2u);
    assert(outbound.contact[1].struct_size == sizeof(MockNavBackendPointerV2));
    assert(outbound.v2_pointer_count >= 4u && outbound.v2_pointer_count <= 5u);
    assert(mock_renderer_notify_vehicle_status(4u, 0u, 2u) == 0);
    assert(mock_renderer_notify_gps_interval(6u, 750u) == 0);
    assert(mock_renderer_notify_audio(19u, 0u, 3u, 17u) == 0);
    wait_count(&outbound, EVENT_VEHICLE, 1u);
    wait_count(&outbound, EVENT_GPS_INTERVAL, 1u);
    wait_count(&outbound, EVENT_AUDIO, 1u);
    assert(outbound.vehicle.day_night_status == 2u);
    assert(outbound.gps_interval.interval_ms == 750u);
    assert(outbound.audio.audio_source == 3u &&
           outbound.audio.volume == 17u);
    stale_services = outbound.services_v2;
    assert(stale_services.publish_cluster(
               stale_services.v1.runtime_context, NULL) ==
           MOCK_NAV_BACKEND_SERVICE_UNAVAILABLE);
    mock_renderer_destroy();
    assert(stale_services.publish_cluster(
               stale_services.v1.runtime_context, NULL) ==
           MOCK_NAV_BACKEND_SERVICE_STALE);
    assert(mock_renderer_create(diagnostic, sizeof(diagnostic)) == 0);
    assert(stale_services.publish_audio_availability(
               stale_services.v1.runtime_context, NULL) ==
           MOCK_NAV_BACKEND_SERVICE_STALE);
    mock_renderer_destroy();
    assert(mock_renderer_set_pointer_contacts(0) == 0);
    assert(mock_navicore_backend_install_v3(NULL) == 0);

    /* Registration validates the version/size prefix, and failed start is
     * returned synchronously without calling surface_created or stop. */
    initialize_fake(&failed);
    backend = backend_for(&failed);
    backend.abi_version = 99u;
    assert(mock_navicore_backend_install_v1(&backend) == -2);
    backend.abi_version = MOCK_NAV_BACKEND_ABI_V1;
    backend.struct_size = sizeof(backend) - 1u;
    assert(mock_navicore_backend_install_v1(&backend) == -2);
    backend.struct_size = sizeof(backend);
    failed.start_status = -7;
    assert(mock_navicore_backend_install_v1(&backend) == 0);
    assert(mock_renderer_create(diagnostic, sizeof(diagnostic)) == -7);
    assert(failed.count == 1u && failed.events[0] == EVENT_START);
    assert(mock_navicore_backend_install_v1(NULL) == 0);
    backend_v2 = backend_v2_for(&failed);
    backend_v2.v1.abi_version = MOCK_NAV_BACKEND_ABI_V1;
    assert(mock_navicore_backend_install_v2(&backend_v2) == -2);
    backend_v2.v1.abi_version = MOCK_NAV_BACKEND_ABI_V2;
    backend_v2.v1.struct_size = sizeof(backend_v2) - 1u;
    assert(mock_navicore_backend_install_v2(&backend_v2) == -2);
    return 0;
}
