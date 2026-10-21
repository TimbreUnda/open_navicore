#pragma once

#include <chrono>

namespace navicore_osmand {

// When the car has left the route long enough to ask for a new one, and how
// long to wait after a failed request.
class RerouteState {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr double kOffRouteMetres = 50.0;
    static constexpr std::chrono::seconds kOffRouteTime{4};
    static constexpr std::chrono::seconds kRetryDelay{10};

    // Call each guidance update. True once the car has stayed further than
    // kOffRouteMetres from the route for kOffRouteTime.
    bool off_route_long_enough(bool arrived, double off_route_metres, Clock::time_point now)
    {
        if (arrived || off_route_metres <= kOffRouteMetres) {
            off_route_ = false;
            return false;
        }
        if (!off_route_) {
            off_route_ = true;
            off_route_since_ = now;
            return false;
        }
        return now - off_route_since_ >= kOffRouteTime;
    }
    void clear_off_route() { off_route_ = false; }

    // A failed request waits kRetryDelay before the next one.
    void request_failed(Clock::time_point now) { retry_at_ = now + kRetryDelay; }
    bool retry_blocked(Clock::time_point now) const { return now < retry_at_; }

private:
    bool off_route_ = false;
    Clock::time_point off_route_since_{};
    Clock::time_point retry_at_{};
};

// Bench detour (simulate-detour): once, past a share of the simulated route,
// drive beside it until a recalculated route arrives or a timeout passes.
class BenchDetour {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr double kStartRouteFraction = 0.35;
    static constexpr double kOffsetMetres = 150.0;
    static constexpr std::chrono::seconds kTimeout{30};

    void enable(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    bool driving() const { return phase_ == Phase::Driving; }

    // Advances the detour for this frame. Returns true on the frame it starts.
    bool advance(double progress_metres, double route_metres, Clock::time_point now)
    {
        bool started = false;
        if (enabled_ && phase_ == Phase::NotStarted &&
            progress_metres > kStartRouteFraction * route_metres) {
            phase_ = Phase::Driving;
            started_at_ = now;
            started = true;
        }
        if (phase_ == Phase::Driving && now - started_at_ > kTimeout)
            phase_ = Phase::Done;
        return started;
    }
    // A recalculated route arrived.
    void route_recovered()
    {
        if (phase_ == Phase::Driving)
            phase_ = Phase::Done;
    }

private:
    enum class Phase { NotStarted, Driving, Done };
    bool enabled_ = false;
    Phase phase_ = Phase::NotStarted;
    Clock::time_point started_at_{};
};

} // namespace navicore_osmand
