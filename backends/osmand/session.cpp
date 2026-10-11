#include "session.h"
#include "diagnostics.h"

#include <limits>
#include <utility>

namespace navicore_osmand {

int Wakeup::request() const noexcept
{
    if (!active_.load())
        return -1;
    // A request already in flight during revoke can reach the old service.
    // The runtime validates its immutable lifecycle token; it cannot target a
    // later start. Within a start it is only a coalescible frame wakeup.
    try {
        return services_.request_frame(services_.runtime_context);
    } catch (...) {
        trace("error=exception_request_frame");
        return -1;
    }
}

Session::~Session()
{
    stop();
    // A poisoned dependency graph must not run destructors over partial core
    // or GPU state. The runtime retains the plugin DSO for process lifetime.
    if (state_ == State::Quarantined)
        (void)engine_.release();
}

int Session::start(const MockNavBackendServicesV1& services, Config config) noexcept
{
    if (state_ != State::Stopped)
        return -1;
    config_ = std::move(config);
    services_ = services;
    state_ = State::Started;
    trace("start");
    return 0;
}

bool Session::accepts_events() const noexcept
{
    return state_ == State::Started || state_ == State::Attached;
}

void Session::revoke() noexcept
{
    if (wakeup_)
        wakeup_->revoke();
    wakeup_.reset();
}

void Session::release(ReleaseMode mode) noexcept
{
    revoke();
    try {
        if (engine_ && engine_->release(mode) != ReleaseResult::Released) {
            state_ = State::Quarantined;
            trace("error=release_quarantined_process_restart_required");
            return;
        }
        engine_.reset();
        context_ = 0;
        state_ = State::Started;
        trace("surface_released");
    } catch (...) {
        state_ = State::Quarantined;
        trace("error=exception_release_process_restart_required");
    }
}

int Session::attach(const MockNavBackendRenderTargetV1& target) noexcept
{
    if (state_ != State::Started ||
        generation_ == std::numeric_limits<uint64_t>::max())
        return -1;
    state_ = State::Attaching;
    context_ = target.native_egl_context;
    pending_count_ = 0;
    prepare_reported_ = false;
    render_reported_ = false;
    first_presented_ = false;
    try {
        wakeup_ = std::make_shared<Wakeup>(services_, ++generation_);
        engine_ = factory_();
        if (engine_ && engine_->initialize(config_, target, wakeup_)) {
            state_ = State::Attached;
            if (has_gps_ && engine_->gps(gps_) != 0)
                trace("warning=initial_gps_rejected");
            trace("surface_created=kyiv");
            (void)wakeup_->request();
            return 0;
        }
    } catch (...) {
        trace("error=exception_surface_created");
    }
    // The loader never calls surface_destroyed after a failed attach.
    // Roll back here, while surface_created still has the expected context.
    release(ReleaseMode::CurrentContext);
    return -1;
}

int Session::gps(const MockNavBackendGpsV1& event) noexcept
{
    if (state_ != State::Started && state_ != State::Attached)
        return -1;
    gps_ = event;
    has_gps_ = true;
    if (state_ == State::Started || !engine_)
        return 0;
    try {
        return engine_->gps(event);
    } catch (...) {
        trace("error=exception_gps");
        return -1;
    }
}

void Session::pending(PendingStage stage) noexcept
{
    bool& reported = stage == PendingStage::Prepare ? prepare_reported_ : render_reported_;
    if (!reported) {
        reported = true;
        trace(stage == PendingStage::Prepare ? "frame_pending=prepare"
                                            : "frame_pending=render_wait_or_fail");
    }
    if (pending_count_ != std::numeric_limits<uint64_t>::max())
        ++pending_count_;
    // Never turn an ambiguous false into a permanent loader fallback. Report
    // persistent lack of progress every 1024 frames, including stage changes.
    if (pending_count_ % 1024u == 0u)
        trace("warning=1024_more_pending_frames_check_core_stage_diagnostics");
}

FrameResult Session::frame(const MockNavBackendRenderTargetV1& target) noexcept
{
    if (state_ != State::Attached)
        return {FrameOutcome::Fault};
    FrameResult result{FrameOutcome::Fault};
    try {
        if (target.native_egl_context == context_)
            result = engine_->frame(target);
    } catch (...) {
        trace("error=exception_render");
    }
    if (result.outcome == FrameOutcome::Fault) {
        state_ = State::Faulted;
        revoke();
        trace("error=frame_fault");
    } else {
        if (result.outcome == FrameOutcome::Pending)
            pending(result.pending);
        else {
            pending_count_ = 0;
            if (!first_presented_) {
                trace("frame_rendered=kyiv");
                first_presented_ = true;
            }
        }
        if (result.request_again)
            (void)wakeup_->request();
    }
    return result;
}

int Session::pointer(const MockNavBackendPointerV1& event) noexcept
{
    if (state_ != State::Attached || !engine_)
        return -1;
    try {
        return engine_->pointer(event);
    } catch (...) {
        trace("error=exception_pointer");
        return -1;
    }
}

void Session::detach(uintptr_t current_context) noexcept
{
    if (state_ == State::Attached || state_ == State::Faulted)
        release(context_ != 0 && current_context == context_
                    ? ReleaseMode::CurrentContext : ReleaseMode::ContextLost);
}

void Session::stop() noexcept
{
    revoke();
    // stop runs after the runtime has released its current context. It also
    // handles callers that omitted surface_destroyed, without issuing GL calls.
    if (state_ == State::Attached || state_ == State::Faulted)
        release(ReleaseMode::ContextLost);
    services_ = {};
    gps_ = {};
    has_gps_ = false;
    if (state_ != State::Quarantined)
        state_ = State::Stopped;
}

} // namespace navicore_osmand
