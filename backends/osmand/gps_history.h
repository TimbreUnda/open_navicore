#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>

namespace navicore_osmand {

// Arrival times of the most recent GPS fixes, for the developer menu's
// last-fix age and fix rate.
class GpsHistory {
public:
    using Clock = std::chrono::steady_clock;

    void record(Clock::time_point at)
    {
        times_[next_] = at;
        next_ = (next_ + 1) % times_.size();
        count_ = std::min(count_ + 1, times_.size());
    }
    bool empty() const { return count_ == 0; }
    Clock::time_point latest() const
    {
        return times_[(next_ + times_.size() - 1) % times_.size()];
    }
    // Fixes per second across the window; 0 until two fixes span some time.
    double rate_hz() const
    {
        if (count_ < 2)
            return 0.0;
        const auto oldest = times_[count_ < times_.size() ? 0 : next_];
        const double span = std::chrono::duration<double>(latest() - oldest).count();
        return span > 0.0 ? (count_ - 1) / span : 0.0;
    }

private:
    std::array<Clock::time_point, 16> times_{};
    std::size_t count_ = 0, next_ = 0;
};

} // namespace navicore_osmand
