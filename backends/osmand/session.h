#pragma once

#include "engine.h"
#include <atomic>
#include <cstdint>

namespace navicore_osmand {

// Each attachment gets an immutable service snapshot and a distinct gate.
// Workers capture this gate, never the Session or a future lifecycle's services.
class Wakeup {
public:
    Wakeup(MockNavBackendServicesV1 services, uint64_t generation) noexcept
        : services_(services), generation_(generation) {}
    int request() const noexcept;
    void revoke() noexcept { active_.store(false); }
    uint64_t generation() const noexcept { return generation_; }
private:
    const MockNavBackendServicesV1 services_;
    const uint64_t generation_;
    std::atomic<bool> active_{true};
};

enum class State { Stopped, Started, Attaching, Attached, Faulted, Quarantined };

class Session {
public:
    explicit Session(EngineFactory factory) noexcept : factory_(factory) {}
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    int start(const MockNavBackendServicesV1&, Config) noexcept;
    // V3+: the same start with the outbound services kept for the engine.
    int start_v2(const MockNavBackendServicesV2&, Config) noexcept;
    // Platform inputs: the latest of each is kept and forwarded (replayed
    // after attach).
    int input(const MockNavBackendVisibilityV1&) noexcept;
    int input(const MockNavBackendScreenV1&) noexcept;
    int input(const MockNavBackendVehicleV2&) noexcept;
    int input(const MockNavBackendInternationalizationV1&) noexcept;
    int input(const MockNavBackendClusterDisplayV1&) noexcept;
    int input(const MockNavBackendDriverDistractionV1&) noexcept;
    int attach(const MockNavBackendRenderTargetV1&) noexcept;
    FrameResult frame(const MockNavBackendRenderTargetV1&) noexcept;
    int gps(const MockNavBackendGpsV1&) noexcept;
    int pointer(const MockNavBackendPointerV2&) noexcept;
    void detach(uintptr_t current_context) noexcept;
    void stop() noexcept;
    bool accepts_events() const noexcept;
    State state() const noexcept { return state_; }

private:
    void revoke() noexcept;
    void release(ReleaseMode) noexcept;
    void pending(PendingStage) noexcept;

    const EngineFactory factory_;
    State state_ = State::Stopped;
    Config config_;
    MockNavBackendServicesV1 services_{};
    std::unique_ptr<Engine> engine_;
    std::shared_ptr<Wakeup> wakeup_;
    uintptr_t context_ = 0;
    uint64_t generation_ = 0;
    uint64_t pending_count_ = 0;
    bool prepare_reported_ = false;
    bool render_reported_ = false;
    bool first_presented_ = false;
    MockNavBackendGpsV1 gps_{};
    bool has_gps_ = false;
    MockNavBackendServicesV2 services_v2_{};
    bool has_services_v2_ = false;
    template <typename T> struct Latest { T value{}; bool has = false; };
    Latest<MockNavBackendVisibilityV1> visibility_;
    Latest<MockNavBackendScreenV1> screen_;
    Latest<MockNavBackendVehicleV2> vehicle_;
    Latest<MockNavBackendInternationalizationV1> internationalization_;
    Latest<MockNavBackendClusterDisplayV1> cluster_display_;
    Latest<MockNavBackendDriverDistractionV1> driver_distraction_;
    template <typename T>
    int record(Latest<T>& latest, const T& event, void (Engine::*forward)(const T&)) noexcept;
    void replay_inputs();
};

} // namespace navicore_osmand
