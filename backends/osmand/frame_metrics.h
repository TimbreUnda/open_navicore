#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace navicore_osmand {

enum class MeasuredFrameOutcome { Presented, PreparePending, RenderPending, Fault };

struct FrameMeasurement {
    MeasuredFrameOutcome outcome = MeasuredFrameOutcome::Fault;
    std::uint32_t total_us = 0;
    std::uint32_t update_us = 0;
    std::uint32_t prepare_us = 0;
    std::uint32_t map_render_us = 0;
    std::uint32_t ui_render_us = 0;
    std::uint32_t ui_composite_us = 0;
    std::uint32_t ui_upload_bytes = 0;
};

// Allocation-free rolling telemetry for the context-owning render thread.
// Buckets are one millisecond wide; the final bucket contains >=127 ms.
// When enabled it also writes one line per frame to
// /tmp/open-navicore-frames.log (exact 1% lows and per-hitch stage times;
// buffered, flushed with each one-second report).
class FrameMetrics {
public:
    explicit FrameMetrics(bool enabled) noexcept;
    void record(const FrameMeasurement& measurement) noexcept;

private:
    static constexpr std::size_t bucket_count = 128;
    struct Distribution {
        std::array<std::uint32_t, bucket_count> buckets{};
        std::uint64_t total_us = 0;
        std::uint32_t count = 0;
        std::uint32_t maximum_us = 0;
        void add(std::uint32_t microseconds) noexcept;
        std::uint32_t percentile(std::uint32_t numerator,
                                 std::uint32_t denominator) const noexcept;
        void clear() noexcept;
    };

    void report(std::chrono::steady_clock::time_point now) noexcept;
    bool enabled_ = false;
    std::chrono::steady_clock::time_point window_started_{};
    // Presented-to-presented intervals include swap and pending frames, so
    // they measure what the driver sees. Coarse bins let a parser compute a
    // run-wide 1% low; slower than one second counts as a pause, not a frame.
    std::chrono::steady_clock::time_point last_presented_{};
    static constexpr std::size_t interval_bin_count = 6;
    std::array<std::uint32_t, interval_bin_count> interval_bins_{};
    std::uint32_t interval_max_us_ = 0;
    std::uint32_t presented_ = 0;
    std::uint32_t prepare_pending_ = 0;
    std::uint32_t render_pending_ = 0;
    std::uint32_t faults_ = 0;
    std::uint64_t ui_upload_bytes_ = 0;
    Distribution total_;
    Distribution update_;
    Distribution prepare_;
    Distribution map_render_;
    Distribution ui_render_;
    Distribution ui_composite_;
};

} // namespace navicore_osmand
