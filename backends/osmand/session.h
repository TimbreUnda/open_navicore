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
    int attach(const MockNavBackendRenderTargetV1&) noexcept;
    FrameResult frame(const MockNavBackendRenderTargetV1&) noexcept;
    int gps(const MockNavBackendGpsV1&) noexcept;
    int pointer(const MockNavBackendPointerV1&) noexcept;
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
};

} // namespace navicore_osmand
