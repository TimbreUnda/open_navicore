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
    MockNavBackendGpsV1 gps{};
    unsigned gps_events = 0;
};
std::shared_ptr<Probe> probe;
std::vector<std::string> diagnostics;
uintptr_t current = 7;

class FakeEngine final : public Engine {
    const std::shared_ptr<Probe> p_ = probe;
public:
    FakeEngine() { ++p_->created; }
    ~FakeEngine() override { ++p_->destroyed; }
    bool initialize(const Config& config, const MockNavBackendRenderTargetV1&,
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
    FrameResult frame(const MockNavBackendRenderTargetV1&) override
    {
        ++p_->frames;
        if (p_->failure == Failure::FrameException)
            throw std::runtime_error("frame");
        return p_->frame;
    }
    int gps(const MockNavBackendGpsV1& event) override
    {
        p_->gps = event;
        ++p_->gps_events;
        return 0;
    }
    int pointer(const MockNavBackendPointerV1&) override { return 0; }
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

MockNavBackendServicesV1 services(unsigned& requests)
{
    return {MOCK_NAV_BACKEND_ABI_V1, sizeof(MockNavBackendServicesV1), &requests, request};
}
MockNavBackendRenderTargetV1 target()
{
    MockNavBackendRenderTargetV1 value{};
    value.abi_version = MOCK_NAV_BACKEND_ABI_V1;
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

    probe = std::make_shared<Probe>();
    unsigned requests = 0, new_requests = 0;
    Session session(make_engine);
    assert(session.attach(target()) != 0);
    assert(session.frame(target()).outcome == FrameOutcome::Fault);
    session.detach(7);
    session.stop();
    assert(session.start(services(requests), {"resources", "map", "fonts"}) == 0);
    assert(session.start(services(new_requests), {}) != 0);
    MockNavBackendGpsV1 gps{};
    gps.abi_version = MOCK_NAV_BACKEND_ABI_V1;
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
    MockNavBackendPluginV1 plugin{};
    assert(mock_nav_backend_query_v1(999, sizeof(plugin), &plugin) != 0);
    assert(mock_nav_backend_query_v1(1, sizeof(plugin) - 1, &plugin) != 0);
    assert(mock_nav_backend_query_v1(1, sizeof(plugin), nullptr) != 0);
    assert(mock_nav_backend_query_v1(1, sizeof(plugin), &plugin) == 0);
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
    MockNavBackendFrameV1 frame{1, sizeof(frame), 1, 0};
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
    CHECK_EVENT(api.visibility_changed, MockNavBackendVisibilityV1, 1);
    CHECK_EVENT(api.screen_changed, MockNavBackendScreenV1, 1);
    CHECK_EVENT(api.gps_changed, MockNavBackendGpsV1, 1);
    CHECK_EVENT(api.pointer_changed, MockNavBackendPointerV1, 1);
    CHECK_EVENT(plugin.backend.vehicle_changed, MockNavBackendVehicleV2, 2);
    CHECK_EVENT(plugin.backend.gps_interval_changed, MockNavBackendGpsIntervalV2, 2);
    CHECK_EVENT(plugin.backend.audio_changed, MockNavBackendAudioV2, 2);
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

int main()
{
    lifecycle();
    failures();
    frames_and_contexts();
    c_boundary();
    std::cout << "OsmAnd session and C boundary tests passed\n";
}
