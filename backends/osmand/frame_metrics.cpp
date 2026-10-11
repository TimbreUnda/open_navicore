#include "frame_metrics.h"

#include "diagnostics.h"

#include <algorithm>
#include <cstdio>

namespace navicore_osmand {

void FrameMetrics::Distribution::add(std::uint32_t microseconds) noexcept
{
    const std::size_t bucket = std::min<std::size_t>(
        microseconds / 1000u, bucket_count - 1u);
    ++buckets[bucket];
    ++count;
    total_us += microseconds;
    maximum_us = std::max(maximum_us, microseconds);
}

std::uint32_t FrameMetrics::Distribution::percentile(
    std::uint32_t numerator, std::uint32_t denominator) const noexcept
{
    if (count == 0u || denominator == 0u)
        return 0u;
    const std::uint64_t wanted =
        (static_cast<std::uint64_t>(count) * numerator + denominator - 1u) /
        denominator;
    std::uint64_t seen = 0u;
    for (std::size_t index = 0; index < buckets.size(); ++index) {
        seen += buckets[index];
        if (seen >= wanted)
            return static_cast<std::uint32_t>(index);
    }
    return static_cast<std::uint32_t>(bucket_count - 1u);
}

void FrameMetrics::Distribution::clear() noexcept
{
    buckets.fill(0u);
    total_us = 0u;
    count = 0u;
    maximum_us = 0u;
}

FrameMetrics::FrameMetrics(bool enabled) noexcept
    : enabled_(enabled), window_started_(std::chrono::steady_clock::now())
{
}

void FrameMetrics::record(const FrameMeasurement& measurement) noexcept
{
    if (!enabled_)
        return;
    switch (measurement.outcome) {
    case MeasuredFrameOutcome::Presented: ++presented_; break;
    case MeasuredFrameOutcome::PreparePending: ++prepare_pending_; break;
    case MeasuredFrameOutcome::RenderPending: ++render_pending_; break;
    case MeasuredFrameOutcome::Fault: ++faults_; break;
    }
    total_.add(measurement.total_us);
    update_.add(measurement.update_us);
    prepare_.add(measurement.prepare_us);
    map_render_.add(measurement.map_render_us);
    ui_render_.add(measurement.ui_render_us);
    ui_composite_.add(measurement.ui_composite_us);
    ui_upload_bytes_ += measurement.ui_upload_bytes;

    const auto now = std::chrono::steady_clock::now();
    if (now - window_started_ >= std::chrono::seconds(1))
        report(now);
}

void FrameMetrics::report(std::chrono::steady_clock::time_point now) noexcept
{
    const auto elapsed_ms = std::max<std::int64_t>(1,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - window_started_).count());
    char line[512] = {};
    (void)std::snprintf(
        line, sizeof(line),
        "perf window_ms=%lld presented=%u fps_x10=%llu pending_prepare=%u "
        "pending_render=%u faults=%u total_p50_ms=%u total_p95_ms=%u "
        "total_p99_ms=%u total_max_us=%u update_p95_ms=%u prepare_p95_ms=%u "
        "map_p95_ms=%u ui_render_p95_ms=%u ui_composite_p95_ms=%u "
        "ui_upload_bytes=%llu",
        static_cast<long long>(elapsed_ms), presented_,
        static_cast<unsigned long long>(presented_) * 10000u /
            static_cast<unsigned long long>(elapsed_ms),
        prepare_pending_, render_pending_, faults_,
        total_.percentile(50u, 100u), total_.percentile(95u, 100u),
        total_.percentile(99u, 100u), total_.maximum_us,
        update_.percentile(95u, 100u), prepare_.percentile(95u, 100u),
        map_render_.percentile(95u, 100u),
        ui_render_.percentile(95u, 100u),
        ui_composite_.percentile(95u, 100u),
        static_cast<unsigned long long>(ui_upload_bytes_));
    trace(line);

    presented_ = 0u;
    prepare_pending_ = 0u;
    render_pending_ = 0u;
    faults_ = 0u;
    ui_upload_bytes_ = 0u;
    total_.clear();
    update_.clear();
    prepare_.clear();
    map_render_.clear();
    ui_render_.clear();
    ui_composite_.clear();
    window_started_ = now;
}

} // namespace navicore_osmand
