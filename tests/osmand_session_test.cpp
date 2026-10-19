#include "session.h"
#include <open_navicore/backend_plugin.h>
#include <EGL/egl.h>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <cstdio>
#include <fstream>
#include <unistd.h>

using namespace navicore_osmand;

namespace {
enum class Failure { None, Factory, NullFactory, Resources, CorePartial,
                     Pipeline, RenderingPartial, InitializeException,
                     FrameException, ReleaseFalse, ReleaseException };
struct Probe {
    Failure failure = Failure::None;
    unsigned created = 0, initialized = 0, released = 0, destroyed = 0, frames = 0;
    ReleaseMode mode = ReleaseMode::CurrentContext;
    FrameResult frame{FrameOutcome::Presented};
    std::shared_ptr<Wakeup> wakeup;
    Config config;
    OpenNaviBackendGpsV1 gps{};
    unsigned gps_events = 0;
    const OpenNaviBackendServicesV2* outbound = nullptr;
    unsigned outbound_calls = 0;
    const OpenNaviBackendServicesV3* guidance_outbound = nullptr;
    unsigned guidance_outbound_calls = 0, guidance_stops = 0;
    uint32_t guidance_stop_handle = 0;
    OpenNaviBackendVehicleV2 vehicle{};
    OpenNaviBackendVisibilityV2 visibility{};
    unsigned vehicle_events = 0, visibility_events = 0, distraction_events = 0,
             language_events = 0, cluster_display_events = 0;
};
std::shared_ptr<Probe> probe;
std::vector<std::string> diagnostics;
uintptr_t current = 7;

class FakeEngine final : public Engine {
    const std::shared_ptr<Probe> p_ = probe;
public:
    FakeEngine() { ++p_->created; }
    ~FakeEngine() override { ++p_->destroyed; }
    bool initialize(const Config& config, const OpenNaviBackendRenderTargetV1&,
                    std::shared_ptr<Wakeup> wakeup) override
    {
        ++p_->initialized;
        p_->config = config;
        p_->wakeup = wakeup;
        if (p_->failure == Failure::InitializeException)
            throw std::runtime_error("initialize");
        return p_->failure != Failure::Resources && p_->failure != Failure::CorePartial &&
               p_->failure != Failure::Pipeline && p_->failure != Failure::RenderingPartial;
    }
    FrameResult frame(const OpenNaviBackendRenderTargetV1&) override
    {
        ++p_->frames;
        if (p_->failure == Failure::FrameException)
            throw std::runtime_error("frame");
        return p_->frame;
    }
    int gps(const OpenNaviBackendGpsV1& event) override
    {
        p_->gps = event;
        ++p_->gps_events;
        return 0;
    }
    int pointer(const OpenNaviBackendPointerV2&) override { return 0; }
    void visibility(const OpenNaviBackendVisibilityV2& event) override
    {
        ++p_->visibility_events;
        p_->visibility = event;
    }
    void vehicle(const OpenNaviBackendVehicleV2& event) override
    {
        p_->vehicle = event;
        ++p_->vehicle_events;
    }
    void internationalization(const OpenNaviBackendInternationalizationV1&) override
    {
        ++p_->language_events;
    }
    void cluster_display(const OpenNaviBackendClusterDisplayV1&) override
    {
        ++p_->cluster_display_events;
    }
    void driver_distraction(const OpenNaviBackendDriverDistractionV1&) override
    {
        ++p_->distraction_events;
    }
    void set_outbound(const OpenNaviBackendServicesV2* services) override
    {
        p_->outbound = services;
        ++p_->outbound_calls;
    }
    void set_guidance_outbound(const OpenNaviBackendServicesV3* services) override
    {
        p_->guidance_outbound = services;
        ++p_->guidance_outbound_calls;
    }
    void guidance_stop(const OpenNaviBackendGuidanceStopV1& event) override
    {
        ++p_->guidance_stops;
        p_->guidance_stop_handle = event.request_handle;
    }
    ReleaseResult release(ReleaseMode mode) override
    {
        ++p_->released;
        p_->mode = mode;
        if (p_->failure == Failure::ReleaseException)
            throw std::runtime_error("release");
        return p_->failure == Failure::ReleaseFalse || p_->failure == Failure::CorePartial ||
               p_->failure == Failure::RenderingPartial
                   ? ReleaseResult::Quarantined : ReleaseResult::Released;
    }
};

int request(void* context)
{
    ++*static_cast<unsigned*>(context);
    return 0;
}
int throwing_request(void*) { throw std::runtime_error("service"); }

OpenNaviBackendServicesV1 services(unsigned& requests)
{
    return {OPEN_NAVI_BACKEND_ABI_V1, sizeof(OpenNaviBackendServicesV1), &requests, request};
}
OpenNaviBackendRenderTargetV1 target()
{
    OpenNaviBackendRenderTargetV1 value{};
    value.abi_version = OPEN_NAVI_BACKEND_ABI_V1;
    value.struct_size = sizeof(value);
    value.width = 800;
    value.height = 480;
    value.native_pointer_bytes = sizeof(void*);
    value.native_egl_context = 7;
    return value;
}

void lifecycle()
{
    assert(setenv("OPEN_NAVICORE_OSMAND_SIMULATE_ROUTE", "1", 1) == 0);
    assert(Config::from_environment().simulate_route);
    assert(unsetenv("OPEN_NAVICORE_OSMAND_SIMULATE_ROUTE") == 0);
    assert(setenv("OPEN_NAVICORE_OSMAND_PERFORMANCE_METRICS", "1", 1) == 0);
    assert(Config::from_environment().performance_metrics);
    assert(unsetenv("OPEN_NAVICORE_OSMAND_PERFORMANCE_METRICS") == 0);
    assert(setenv("OPEN_NAVICORE_OSMAND_BENCHMARK_AUTOSTART", "1", 1) == 0);
    assert(Config::from_environment().benchmark_autostart);
    assert(unsetenv("OPEN_NAVICORE_OSMAND_BENCHMARK_AUTOSTART") == 0);
    assert(setenv("OPEN_NAVICORE_OSMAND_SIMULATE_DETOUR", "1", 1) == 0);
    assert(Config::from_environment().simulate_detour);
    assert(unsetenv("OPEN_NAVICORE_OSMAND_SIMULATE_DETOUR") == 0);

    probe = std::make_shared<Probe>();
    unsigned requests = 0, new_requests = 0;
    Session session(make_engine);
    assert(session.attach(target()) != 0);
    assert(session.frame(target()).outcome == FrameOutcome::Fault);
    session.detach(7);
    session.stop();
    assert(session.start(services(requests), {"resources", "map", "fonts"}) == 0);
    assert(session.start(services(new_requests), {}) != 0);
    OpenNaviBackendGpsV1 gps{};
    gps.abi_version = OPEN_NAVI_BACKEND_ABI_V1;
    gps.struct_size = sizeof(gps);
    gps.latitude_e7 = 504501000;
    gps.longitude_e7 = 305234000;
    assert(session.gps(gps) == 0);
    assert(session.attach(target()) == 0);
    assert(probe->gps_events == 1);
    assert(probe->gps.latitude_e7 == gps.latitude_e7);
    assert(probe->config.map == "map");
    assert(session.attach(target()) != 0);
    assert(probe->created == 1);
    auto old_wakeup = probe->wakeup;
    assert(requests == 1);
    assert(session.frame(target()).outcome == FrameOutcome::Presented);
    session.detach(7);
    assert(probe->mode == ReleaseMode::CurrentContext);
    assert(probe->released == 1 && probe->destroyed == 1);
    session.detach(7);
    assert(probe->released == 1);
    assert(old_wakeup->request() != 0);
    assert(session.attach(target()) == 0);
    assert(probe->gps_events == 2);
    assert(probe->wakeup->generation() > old_wakeup->generation());
    assert(old_wakeup->request() != 0);
    session.stop(); // An omitted detach has no current-context guarantee.
    assert(probe->mode == ReleaseMode::ContextLost);
    assert(probe->released == 2);
    session.stop();
    assert(probe->released == 2);
    assert(session.start(services(new_requests), {}) == 0);
    assert(session.attach(target()) == 0);
    assert(old_wakeup->request() != 0);
    assert(new_requests == 1 && requests == 2);
    session.stop();
}

void failures()
{
    for (const auto failure : {Failure::Factory, Failure::NullFactory, Failure::Resources,
             Failure::CorePartial, Failure::Pipeline, Failure::RenderingPartial,
             Failure::InitializeException}) {
        probe = std::make_shared<Probe>();
        probe->failure = failure;
        unsigned requests = 0;
        Session session(make_engine);
        assert(session.start(services(requests), {}) == 0);
        assert(session.attach(target()) != 0);
        const bool poisoned = failure == Failure::CorePartial || failure == Failure::RenderingPartial;
        assert(session.state() == (poisoned ? State::Quarantined : State::Started));
        assert(probe->released == (probe->created != 0 ? 1u : 0u));
        if (probe->wakeup)
            assert(probe->wakeup->request() != 0);
        session.stop();
        session.detach(7);
        probe->failure = Failure::None;
        if (poisoned) {
            assert(probe->destroyed == 0);
            assert(session.start(services(requests), {}) != 0);
            assert(session.attach(target()) != 0);
        } else {
            assert(probe->destroyed == probe->created);
            assert(session.start(services(requests), {}) == 0);
            assert(session.attach(target()) == 0);
            session.stop();
        }
    }
    for (const auto failure : {Failure::ReleaseFalse, Failure::ReleaseException}) {
        probe = std::make_shared<Probe>();
        probe->failure = failure;
        unsigned requests = 0;
        Session session(make_engine);
        assert(session.start(services(requests), {}) == 0);
        assert(session.attach(target()) == 0);
        session.detach(7);
        assert(session.state() == State::Quarantined);
        session.detach(7);
        session.stop();
        assert(probe->released == 1 && probe->destroyed == 0);
        assert(probe->wakeup->request() != 0);
        assert(session.start(services(requests), {}) != 0);
    }
}

void frames_and_contexts()
{
    probe = std::make_shared<Probe>();
    unsigned requests = 0;
    Session session(make_engine);
    assert(session.start(services(requests), {}) == 0);
    assert(session.attach(target()) == 0);
    diagnostics.clear();
    for (unsigned i = 0; i < 2048; ++i) {
        probe->frame = {FrameOutcome::Pending,
                       i % 2 ? PendingStage::Prepare : PendingStage::Render, true};
        assert(session.frame(target()).outcome == FrameOutcome::Pending);
    }
    assert(diagnostics.size() == 4); // Once per stage + two persistent warnings.
    assert(session.state() == State::Attached && requests == 2049);
    probe->frame = {FrameOutcome::Pending, PendingStage::Prepare, false};
    assert(session.frame(target()).outcome == FrameOutcome::Pending);
    assert(requests == 2049); // Idle prepare waits on the engine's worker callback.
    probe->frame = {FrameOutcome::Presented};
    assert(session.frame(target()).outcome == FrameOutcome::Presented);
    probe->frame = {FrameOutcome::Fault}; // Includes an engine update failure.
    assert(session.frame(target()).outcome == FrameOutcome::Fault);
    assert(session.state() == State::Faulted && probe->released == 0);
    session.detach(7);
    assert(session.attach(target()) == 0);
    probe->failure = Failure::FrameException;
    assert(session.frame(target()).outcome == FrameOutcome::Fault);
    assert(session.state() == State::Faulted);
    assert(probe->wakeup->request() != 0);
    assert(probe->released == 1); // Never tear GL down inside render_frame.
    session.detach(0);
    assert(probe->mode == ReleaseMode::ContextLost);
    probe->failure = Failure::None;
    assert(session.attach(target()) == 0);
    session.detach(99); // A different current context is also context loss.
    assert(probe->mode == ReleaseMode::ContextLost);
    assert(session.attach(target()) == 0);
    auto other = target();
    other.native_egl_context = 8;
    const auto frames = probe->frames;
    assert(session.frame(other).outcome == FrameOutcome::Fault);
    assert(probe->frames == frames && session.state() == State::Faulted);
    session.stop();

    auto bad_services = services(requests);
    bad_services.request_frame = throwing_request;
    Wakeup wakeup(bad_services, 1);
    assert(wakeup.request() != 0);
}

void c_boundary()
{
    probe = std::make_shared<Probe>();
    OpenNaviBackendPluginV1 plugin{};
    assert(open_navi_backend_query_v1(999, sizeof(plugin), &plugin) != 0);
    assert(open_navi_backend_query_v1(1, sizeof(plugin) - 1, &plugin) != 0);
    assert(open_navi_backend_query_v1(1, sizeof(plugin), nullptr) != 0);
    assert(open_navi_backend_query_v1(1, sizeof(plugin), &plugin) == 0);
    auto& api = plugin.backend.v1;
    void* context = api.backend_context;
    unsigned requests = 0;
    auto svc = services(requests);
    assert(api.start(nullptr, &svc) != 0);
    svc.struct_size = 8;
    assert(api.start(context, &svc) != 0);
    svc = services(requests);
    svc.abi_version = 2;
    assert(api.start(context, &svc) != 0);
    svc = services(requests);
    assert(setenv("OPEN_NAVICORE_OSMAND_MAP", "owned-map", 1) == 0);
    assert(api.start(context, &svc) == 0);
    assert(setenv("OPEN_NAVICORE_OSMAND_MAP", "later-map", 1) == 0);
    auto surface = target();
    surface.struct_size = 8;
    assert(api.surface_created(context, &surface) != 0);
    surface = target();
    current = 0;
    assert(api.surface_created(context, &surface) != 0);
    current = 7;
    surface.width = UINT32_MAX;
    assert(api.surface_created(context, &surface) != 0);
    surface = target();
    surface.native_pointer_bytes = 0;
    assert(api.surface_created(context, &surface) != 0);
    surface = target();
    probe->failure = Failure::InitializeException;
    assert(api.surface_created(context, &surface) != 0);
    assert(probe->destroyed == 1);
    probe->failure = Failure::None;
    assert(api.surface_created(context, &surface) == 0);
    assert(probe->config.map == "owned-map");
    assert(api.surface_created(context, &surface) != 0);
    OpenNaviBackendFrameV1 frame{1, sizeof(frame), 1, 0};
    probe->frame = {FrameOutcome::Pending, PendingStage::Render, true};
    assert(api.render_frame(context, &surface, &frame) == 0);
    frame.struct_size = 8;
    assert(api.render_frame(context, &surface, &frame) != 0);
    frame.struct_size = sizeof(frame);
    assert(api.render_frame(context, &surface, nullptr) != 0);

#define CHECK_EVENT(member, Type, version) do { \
    Type event{}; \
    event.abi_version = version; event.struct_size = sizeof(event); \
    assert(member(context, &event) == 0); \
    event.struct_size = 8; assert(member(context, &event) != 0); \
    event.struct_size = sizeof(event); event.abi_version = 999; \
    assert(member(context, &event) != 0); \
    assert(member(context, nullptr) != 0); \
} while (0)
    CHECK_EVENT(api.visibility_changed, OpenNaviBackendVisibilityV1, 1);
    CHECK_EVENT(api.screen_changed, OpenNaviBackendScreenV1, 1);
    CHECK_EVENT(api.gps_changed, OpenNaviBackendGpsV1, 1);
    CHECK_EVENT(api.pointer_changed, OpenNaviBackendPointerV1, 1);
    CHECK_EVENT(plugin.backend.vehicle_changed, OpenNaviBackendVehicleV2, 2);
    CHECK_EVENT(plugin.backend.gps_interval_changed, OpenNaviBackendGpsIntervalV2, 2);
    CHECK_EVENT(plugin.backend.audio_changed, OpenNaviBackendAudioV2, 2);
#undef CHECK_EVENT
    assert(probe->gps_events == 1);
    assert(probe->gps.latitude_e7 == 0 && probe->gps.longitude_e7 == 0);
    probe->failure = Failure::FrameException;
    assert(api.render_frame(context, &surface, &frame) != 0);
    current = 0;
    api.surface_destroyed(context);
    assert(probe->mode == ReleaseMode::ContextLost);
    api.surface_destroyed(context);
    api.stop(context);
    api.stop(context);
    api.stop(nullptr);
    api.surface_destroyed(nullptr);
    assert(unsetenv("OPEN_NAVICORE_OSMAND_MAP") == 0);
}
}

namespace navicore_osmand {
std::unique_ptr<Engine> make_engine()
{
    if (probe->failure == Failure::Factory)
        throw std::runtime_error("factory");
    if (probe->failure == Failure::NullFactory)
        return nullptr;
    return std::unique_ptr<Engine>(new FakeEngine());
}
void trace(const char* message) noexcept { diagnostics.emplace_back(message); }
}
extern "C" EGLContext eglGetCurrentContext(void)
{
    return reinterpret_cast<EGLContext>(current);
}

uint32_t publish_cluster(void*, const OpenNaviBackendClusterV1*) { return 0; }

// Query V4 selects backend V5: started with the outbound services, and
// the platform inputs recorded before attach are replayed after it.
void backend_v5()
{
    probe = std::make_shared<Probe>();
    current = 7;  // c_boundary ends with no current context
    OpenNaviBackendPluginV4 plugin{};
    assert(open_navi_backend_query_v1(OPEN_NAVI_PLUGIN_QUERY_ABI_V4, sizeof(plugin) - 1,
                                     reinterpret_cast<OpenNaviBackendPluginV1*>(&plugin)) != 0);
    assert(open_navi_backend_query_v1(OPEN_NAVI_PLUGIN_QUERY_ABI_V4, sizeof(plugin),
                                     reinterpret_cast<OpenNaviBackendPluginV1*>(&plugin)) == 0);
    const auto& v1 = plugin.v3.v2.v1;
    static const uint8_t build_id[] = OPEN_NAVI_PLUGIN_V4_EXPECTED_BUILD_ID_INITIALIZER;
    assert(v1.query_abi_version == OPEN_NAVI_PLUGIN_QUERY_ABI_V4);
    assert(v1.struct_size == sizeof(plugin));
    assert(v1.backend_abi_version == OPEN_NAVI_BACKEND_ABI_V5);
    assert(std::equal(std::begin(build_id), std::end(build_id), v1.build_id));
    assert((v1.capabilities & ~uint32_t(OPEN_NAVI_PLUGIN_OPTIONAL_CAPABILITIES)) ==
           OPEN_NAVI_PLUGIN_REQUIRED_CAPABILITIES);
    assert(v1.capabilities & OPEN_NAVI_PLUGIN_CAP_POINTER_CONTACTS);
    assert(v1.capabilities & OPEN_NAVI_PLUGIN_CAP_WINDOW_AREA);
    assert(plugin.v3.v2.capabilities_v2 == OPEN_NAVI_PLUGIN_V2_REQUIRED_CAPABILITIES);
    assert(plugin.v3.capabilities_v3 == OPEN_NAVI_PLUGIN_V3_REQUIRED_CAPABILITIES);
    assert(plugin.capabilities_v4 == OPEN_NAVI_PLUGIN_V4_REQUIRED_CAPABILITIES);
    assert(plugin.backend_v5_size == sizeof(OpenNaviBackendV5));
    assert(plugin.driver_distraction_v1_size == sizeof(OpenNaviBackendDriverDistractionV1));
    const auto& backend = plugin.backend_v5;
    const auto& api = backend.v4.v3.v2.v1;
    assert(api.abi_version == OPEN_NAVI_BACKEND_ABI_V5 && api.struct_size == sizeof(backend));
    assert(backend.v4.v3.start_v2 && backend.v4.internationalization_changed &&
           backend.v4.cluster_display_changed && backend.driver_distraction_changed &&
           backend.v4.v3.v2.vehicle_changed);
    void* context = api.backend_context;

    unsigned requests = 0;
    OpenNaviBackendServicesV2 services{};
    services.v1 = {OPEN_NAVI_BACKEND_ABI_V2, sizeof(services), &requests, request};
    services.publish_cluster = publish_cluster;
    OpenNaviBackendServicesV2 wrong = services;
    wrong.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V1;
    assert(backend.v4.v3.start_v2(context, &wrong) != 0);
    assert(backend.v4.v3.start_v2(context, &services) == 0);

    // Before attach: recorded (latest kept), not yet forwarded.
    OpenNaviBackendVehicleV2 vehicle{};
    vehicle.abi_version = OPEN_NAVI_BACKEND_ABI_V2;
    vehicle.struct_size = sizeof(vehicle);
    vehicle.day_night_status = 1;
    assert(backend.v4.v3.v2.vehicle_changed(context, &vehicle) == 0);
    vehicle.day_night_status = 2;
    assert(backend.v4.v3.v2.vehicle_changed(context, &vehicle) == 0);
    OpenNaviBackendDriverDistractionV1 distraction{};
    distraction.abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
    distraction.struct_size = sizeof(distraction);
    assert(backend.driver_distraction_changed(context, &distraction) == 0);
    distraction.struct_size = 8;
    assert(backend.driver_distraction_changed(context, &distraction) != 0);
    distraction.struct_size = sizeof(distraction);
    assert(probe->vehicle_events == 0);

    auto surface = target();
    assert(api.surface_created(context, &surface) == 0);
    assert(probe->outbound && probe->outbound->publish_cluster == publish_cluster);
    assert(probe->vehicle_events == 1 && probe->vehicle.day_night_status == 2);
    assert(probe->distraction_events == 1 && probe->visibility_events == 0);
    // Attached: forwarded at once.
    OpenNaviBackendInternationalizationV1 language{};
    language.abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
    language.struct_size = sizeof(language);
    assert(backend.v4.internationalization_changed(context, &language) == 0);
    assert(probe->language_events == 1);
    OpenNaviBackendVisibilityV1 visible{OPEN_NAVI_BACKEND_ABI_V1, sizeof(visible), 0, 1, 1};
    assert(api.visibility_changed(context, &visible) == 0);
    assert(probe->visibility_events == 1);
    assert(probe->visibility.area_width == 0 && probe->visibility.area_height == 0);
    // Window area capability: a V2 payload keeps the split-screen area.
    OpenNaviBackendVisibilityV2 split{OPEN_NAVI_BACKEND_VISIBILITY_ABI_V2, sizeof(split), 0,
                                      OPEN_NAVI_BACKEND_HMI_VISIBLE_SPLIT_SCREEN, 2,
                                      129, 160, 765, 323};
    assert(api.visibility_changed(context,
        reinterpret_cast<const OpenNaviBackendVisibilityV1*>(&split)) == 0);
    assert(probe->visibility_events == 2);
    assert(probe->visibility.visibility == OPEN_NAVI_BACKEND_HMI_VISIBLE_SPLIT_SCREEN);
    assert(probe->visibility.area_x == 129 && probe->visibility.area_width == 765);
    split.struct_size = sizeof(OpenNaviBackendVisibilityV1);  // V2 tag, V1 size
    assert(api.visibility_changed(context,
        reinterpret_cast<const OpenNaviBackendVisibilityV1*>(&split)) != 0);
    api.surface_destroyed(context);
    api.stop(context);

    // A V1 start gives no outbound services and replays nothing old.
    auto plain = services.v1;
    plain.abi_version = OPEN_NAVI_BACKEND_ABI_V1;
    plain.struct_size = sizeof(plain);
    assert(api.start(context, &plain) == 0);
    probe = std::make_shared<Probe>();
    assert(api.surface_created(context, &surface) == 0);
    assert(probe->outbound_calls == 1 && probe->outbound == nullptr);
    assert(probe->vehicle_events == 0 && probe->distraction_events == 0);
    api.surface_destroyed(context);
    api.stop(context);
}

uint32_t publish_guidance(void*, const OpenNaviBackendGuidanceStateV1*) { return 0; }

// Query V5 selects backend V6: the complete V5 descriptor, started through
// start_v3 with the guidance state service; guidance stop requests are
// forwarded only while attached and never replayed.
void backend_v6()
{
    probe = std::make_shared<Probe>();
    current = 7;
    OpenNaviBackendPluginV5 plugin{};
    assert(open_navi_backend_query_v1(OPEN_NAVI_PLUGIN_QUERY_ABI_V5, sizeof(plugin) - 1,
                                     reinterpret_cast<OpenNaviBackendPluginV1*>(&plugin)) != 0);
    assert(open_navi_backend_query_v1(OPEN_NAVI_PLUGIN_QUERY_ABI_V5, sizeof(plugin),
                                     reinterpret_cast<OpenNaviBackendPluginV1*>(&plugin)) == 0);
    const auto& v1 = plugin.v4.v3.v2.v1;
    static const uint8_t build_id[] = OPEN_NAVI_PLUGIN_V5_EXPECTED_BUILD_ID_INITIALIZER;
    assert(v1.query_abi_version == OPEN_NAVI_PLUGIN_QUERY_ABI_V5);
    assert(v1.struct_size == sizeof(plugin));
    assert(v1.backend_abi_version == OPEN_NAVI_BACKEND_ABI_V6);
    assert(std::equal(std::begin(build_id), std::end(build_id), v1.build_id));
    assert(plugin.v4.capabilities_v4 == OPEN_NAVI_PLUGIN_V4_REQUIRED_CAPABILITIES);
    assert(plugin.v4.backend_v5_size == sizeof(OpenNaviBackendV5));
    assert(plugin.capabilities_v5 == OPEN_NAVI_PLUGIN_V5_REQUIRED_CAPABILITIES);
    assert(plugin.services_v3_size == sizeof(OpenNaviBackendServicesV3));
    assert(plugin.guidance_state_v1_size == sizeof(OpenNaviBackendGuidanceStateV1));
    assert(plugin.guidance_stop_v1_size == sizeof(OpenNaviBackendGuidanceStopV1));
    assert(plugin.backend_v6_size == sizeof(OpenNaviBackendV6));
    const auto& backend = plugin.backend_v6;
    const auto& api = backend.v5.v4.v3.v2.v1;
    assert(api.abi_version == OPEN_NAVI_BACKEND_ABI_V6 && api.struct_size == sizeof(backend));
    assert(backend.start_v3 && backend.guidance_stop_requested &&
           backend.v5.v4.v3.start_v2 && backend.v5.driver_distraction_changed);
    void* context = api.backend_context;

    unsigned requests = 0;
    OpenNaviBackendServicesV3 services{};
    services.v2.v1 = {OPEN_NAVI_BACKEND_ABI_V3, sizeof(services), &requests, request};
    services.v2.publish_cluster = publish_cluster;
    services.publish_guidance_state = publish_guidance;
    OpenNaviBackendServicesV3 wrong = services;
    wrong.v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V2;
    assert(backend.start_v3(context, &wrong) != 0);
    wrong = services;
    wrong.publish_guidance_state = nullptr;
    assert(backend.start_v3(context, &wrong) != 0);
    assert(backend.start_v3(context, &services) == 0);

    OpenNaviBackendGuidanceStopV1 stop{};
    stop.abi_version = OPEN_NAVI_BACKEND_PAYLOAD_ABI_V1;
    stop.struct_size = sizeof(stop);
    stop.source_slot = 13;
    stop.request_handle = 0x2513;
    // Started, not attached: accepted, nothing to stop, not replayed.
    assert(backend.guidance_stop_requested(context, &stop) == 0);
    auto surface = target();
    assert(api.surface_created(context, &surface) == 0);
    assert(probe->guidance_stops == 0);
    assert(probe->outbound && probe->outbound->publish_cluster == publish_cluster);
    assert(probe->outbound->v1.abi_version == OPEN_NAVI_BACKEND_ABI_V2);
    assert(probe->guidance_outbound_calls == 1 && probe->guidance_outbound &&
           probe->guidance_outbound->publish_guidance_state == publish_guidance);
    // Attached: forwarded at once.
    assert(backend.guidance_stop_requested(context, &stop) == 0);
    assert(probe->guidance_stops == 1 && probe->guidance_stop_handle == 0x2513);
    stop.struct_size = 8;
    assert(backend.guidance_stop_requested(context, &stop) != 0);
    assert(probe->guidance_stops == 1);
    api.surface_destroyed(context);
    api.stop(context);
    stop.struct_size = sizeof(stop);
    assert(backend.guidance_stop_requested(context, &stop) != 0);

    // A V5 start (start_v2) gives no guidance service.
    OpenNaviBackendServicesV2 v2 = services.v2;
    v2.v1.abi_version = OPEN_NAVI_BACKEND_ABI_V2;
    v2.v1.struct_size = sizeof(v2);
    assert(backend.v5.v4.v3.start_v2(context, &v2) == 0);
    probe = std::make_shared<Probe>();
    assert(api.surface_created(context, &surface) == 0);
    assert(probe->guidance_outbound_calls == 1 && probe->guidance_outbound == nullptr);
    api.surface_destroyed(context);
    api.stop(context);
}

void obf_directory()
{
    using navicore_osmand::obf_files;
    char tmpl[] = "/tmp/obf-dir-XXXXXX";
    assert(::mkdtemp(tmpl));
    const std::string dir = tmpl;
    for (const char* name : {"b.obf", "a.obf", "notes.txt", ".hidden.obf", "c.obf.md5"})
        std::ofstream(dir + "/" + name) << "x";
    const auto files = obf_files(dir);
    assert(files.size() == 2);
    assert(files[0] == dir + "/a.obf" && files[1] == dir + "/b.obf");
    assert(obf_files(dir + "/a.obf") == std::vector<std::string>{dir + "/a.obf"});
    assert(obf_files(dir + "/missing").empty());
    assert(obf_files("").empty());
    using navicore_osmand::is_basemap;
    assert(is_basemap("/navi_map/osm/World_basemap_2.obf"));
    assert(is_basemap("/x/world_BASEMAP_mini.obf"));
    assert(!is_basemap("/navi_map/osm/Ukraine_kyiv_europe_2.obf"));
    assert(!is_basemap("/basemap-dir/Ukraine.obf"));
    for (const char* name : {"b.obf", "a.obf", "notes.txt", ".hidden.obf", "c.obf.md5"})
        std::remove((dir + "/" + name).c_str());
    ::rmdir(dir.c_str());
}

int main()
{
    obf_directory();
    lifecycle();
    failures();
    frames_and_contexts();
    c_boundary();
    backend_v5();
    backend_v6();
    std::cout << "OsmAnd session and C boundary tests passed\n";
}
