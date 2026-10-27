#pragma once

#include <chrono>
#include <cmath>
#include <optional>

namespace navicore_osmand {

// The driving direction a new route starts with: the motion heading while the
// car moves, and kept for a while after it stops (lights, a queue). A car that
// stands where it stopped still points that way, so start_heading() also keeps
// it there for good (parked, the nav restarted, a saved record), until the car
// has moved away without a heading (a slow manoeuvre in a car park).
class HeadingMemory {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr double kMinSpeedMps = 1.5;   // the camera's heading threshold
    static constexpr std::chrono::seconds kKeepAfterStop{30};
    static constexpr double kSameSpotMetres = 30.0;   // GPS noise of a standing car

    // Where the car stopped and which way it pointed.
    struct Parked {
        double heading_degrees = 0.0;
        double latitude = 0.0;
        double longitude = 0.0;
    };

    // position: the car's position with the motion, when known.
    void observe(bool valid, double speed_mps, double heading_degrees, Clock::time_point now,
                 const double* latitude = nullptr, const double* longitude = nullptr)
    {
        if (!valid || speed_mps < kMinSpeedMps) {
            // Stopped: a new spot is worth saving once.
            if (moving_ && position_known_ && (!saved_ || !near(saved_spot_, spot_.latitude, spot_.longitude))) {
                saved_spot_ = spot_;
                saved_ = true;
                save_pending_ = true;
            }
            moving_ = false;
            return;
        }
        heading_degrees_ = heading_degrees;
        seen_at_ = now;
        known_ = true;
        live_ = true;
        moving_ = true;
        if (latitude && longitude) {
            spot_ = {heading_degrees, *latitude, *longitude};
            position_known_ = true;
        }
    }
    // The heading if one was seen within max_age.
    std::optional<double> heading(Clock::time_point now,
                                  Clock::duration max_age = kKeepAfterStop) const
    {
        if (!live_ || now - seen_at_ > max_age)
            return std::nullopt;
        return heading_degrees_;
    }
    // The direction a route from `latitude, longitude` starts with: the recent
    // heading, or the one the car had where it stopped, while it is still there.
    std::optional<double> start_heading(Clock::time_point now, double latitude,
                                        double longitude) const
    {
        if (const auto recent = heading(now)) return recent;
        if (!known_ || !position_known_ || !near(spot_, latitude, longitude)) return std::nullopt;
        return spot_.heading_degrees;
    }
    // A saved record from before a restart; real motion replaces it.
    void restore(const Parked& parked)
    {
        if (known_) return;
        heading_degrees_ = parked.heading_degrees;
        spot_ = parked;
        saved_spot_ = parked;
        known_ = position_known_ = saved_ = true;
    }
    // The spot to save, once per stop at a new spot.
    bool take_save(Parked& parked)
    {
        if (!save_pending_) return false;
        save_pending_ = false;
        parked = saved_spot_;
        return true;
    }

private:
    static bool near(const Parked& spot, double latitude, double longitude)
    {
        const double north = (latitude - spot.latitude) * 111320.0;
        const double east = (longitude - spot.longitude) * 111320.0 *
                            std::cos(spot.latitude * 3.14159265358979323846 / 180.0);
        return std::sqrt(north * north + east * east) <= kSameSpotMetres;
    }

    bool known_ = false;   // a heading, seen or restored
    bool live_ = false;    // seen in this run: heading() may use it
    double heading_degrees_ = 0.0;
    Clock::time_point seen_at_{};
    bool moving_ = false;
    bool position_known_ = false;
    Parked spot_;          // the last moving position and heading
    bool saved_ = false;
    Parked saved_spot_;
    bool save_pending_ = false;
};

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
    void clear_off_route()
    {
        off_route_ = false;
        wrong_way_ = false;
    }

    static constexpr double kWrongWayDegrees = 120.0;
    static constexpr double kAlongDegrees = 60.0;
    static constexpr std::chrono::seconds kWrongWayTime{5};

    // Call each guidance update with the angle between the driving direction
    // and the route's at the car's progress, or nothing while the car is not
    // moving. Driving along a route stays near it, so the distance check never
    // fires; this is true once the car has driven more than kWrongWayDegrees
    // against the route for kWrongWayTime. It then waits until the car has
    // driven along a route again: a new route that still starts with a U-turn
    // is the router's answer, not a reason to ask again.
    bool wrong_way_long_enough(bool arrived, std::optional<double> off_degrees,
                               Clock::time_point now)
    {
        if (arrived || !off_degrees) {
            wrong_way_ = false;
            return false;
        }
        const double off = std::fabs(std::remainder(*off_degrees, 360.0));
        if (off <= kAlongDegrees)
            wrong_way_armed_ = true;
        if (off < kWrongWayDegrees || !wrong_way_armed_) {
            wrong_way_ = false;
            return false;
        }
        if (!wrong_way_) {
            wrong_way_ = true;
            wrong_way_since_ = now;
            return false;
        }
        if (now - wrong_way_since_ < kWrongWayTime)
            return false;
        wrong_way_ = false;
        wrong_way_armed_ = false;
        return true;
    }

    // A failed request waits kRetryDelay before the next one.
    void request_failed(Clock::time_point now) { retry_at_ = now + kRetryDelay; }
    bool retry_blocked(Clock::time_point now) const { return now < retry_at_; }

private:
    bool off_route_ = false;
    Clock::time_point off_route_since_{};
    bool wrong_way_ = false;
    bool wrong_way_armed_ = true;
    Clock::time_point wrong_way_since_{};
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
