#pragma once

#include "engine.h"
#include <atomic>
#include <cstdint>

namespace navicore_osmand {

// Each attachment gets an immutable service snapshot and a distinct gate.
// Workers capture this gate, never the Session or a future lifecycle's services.
class Wakeup {
public:
    Wakeup(OpenNaviBackendServicesV1 services, uint64_t generation) noexcept
        : services_(services), generation_(generation) {}
    int request() const noexcept;
    void revoke() noexcept { active_.store(false); }
    uint64_t generation() const noexcept { return generation_; }
private:
    const OpenNaviBackendServicesV1 services_;
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

    int start(const OpenNaviBackendServicesV1&, Config) noexcept;
    // V3+: the same start with the outbound services kept for the engine.
    int start_v2(const OpenNaviBackendServicesV2&, Config) noexcept;
    // Platform inputs: the latest of each is kept and forwarded (replayed
    // after attach).
    int input(const OpenNaviBackendVisibilityV1&) noexcept;
    int input(const OpenNaviBackendScreenV1&) noexcept;
    int input(const OpenNaviBackendVehicleV2&) noexcept;
    int input(const OpenNaviBackendInternationalizationV1&) noexcept;
    int input(const OpenNaviBackendClusterDisplayV1&) noexcept;
    int input(const OpenNaviBackendDriverDistractionV1&) noexcept;
    int attach(const OpenNaviBackendRenderTargetV1&) noexcept;
    FrameResult frame(const OpenNaviBackendRenderTargetV1&) noexcept;
    int gps(const OpenNaviBackendGpsV1&) noexcept;
    int pointer(const OpenNaviBackendPointerV2&) noexcept;
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
    OpenNaviBackendServicesV1 services_{};
    std::unique_ptr<Engine> engine_;
    std::shared_ptr<Wakeup> wakeup_;
    uintptr_t context_ = 0;
    uint64_t generation_ = 0;
    uint64_t pending_count_ = 0;
    bool prepare_reported_ = false;
    bool render_reported_ = false;
    bool first_presented_ = false;
    OpenNaviBackendGpsV1 gps_{};
    bool has_gps_ = false;
    OpenNaviBackendServicesV2 services_v2_{};
    bool has_services_v2_ = false;
    template <typename T> struct Latest { T value{}; bool has = false; };
    Latest<OpenNaviBackendVisibilityV1> visibility_;
    Latest<OpenNaviBackendScreenV1> screen_;
    Latest<OpenNaviBackendVehicleV2> vehicle_;
    Latest<OpenNaviBackendInternationalizationV1> internationalization_;
    Latest<OpenNaviBackendClusterDisplayV1> cluster_display_;
    Latest<OpenNaviBackendDriverDistractionV1> driver_distraction_;
    template <typename T>
    int record(Latest<T>& latest, const T& event, void (Engine::*forward)(const T&)) noexcept;
    void replay_inputs();
};

} // namespace navicore_osmand
