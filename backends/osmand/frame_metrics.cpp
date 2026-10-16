#include "frame_metrics.h"

#include "diagnostics.h"

#include <algorithm>
#include <cstdio>

namespace navicore_osmand {
namespace {
// One line per frame: outcome, time since start, present interval and stage
// times in microseconds. A static buffer keeps the render thread off the
// file system between the one-second flushes.
std::FILE* frame_log()
{
    static std::FILE* const file = [] {
        std::FILE* f = std::fopen("/tmp/open-navicore-frames.log", "w");
        static char buffer[64 * 1024];
        if (f) {
            (void)std::setvbuf(f, buffer, _IOFBF, sizeof(buffer));
            (void)std::fputs("# outcome t_ms interval_us total_us update_us prepare_us "
                             "map_us ui_render_us ui_composite_us results_us guidance_us "
                             "tracker_us route_sync_us guidance_ui_us input_us camera_us "
                             "vector_us post_us cpu_us vec_take_us vec_uploads vec_route_us "
                             "vec_want_us vec_draw_us vec_labels_us vec_evict_us vec_upload_max_us "
                             "vec_atlas_us vec_label_buffers_us\n", f);
        }
        return f;
    }();
    return file;
}
} // namespace

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
    const auto now = std::chrono::steady_clock::now();
    std::int64_t interval_us = 0;
    switch (measurement.outcome) {
    case MeasuredFrameOutcome::Presented:
        ++presented_;
        if (last_presented_ != std::chrono::steady_clock::time_point{}) {
            const auto interval = std::chrono::duration_cast<
                std::chrono::microseconds>(now - last_presented_).count();
            interval_us = interval;
            if (interval < 1000000) {
                // Upper bounds: 33, 50, 67, 100, 200 ms, then slower.
                static constexpr std::int64_t bounds[] = {
                    33334, 50000, 66667, 100000, 200000};
                std::size_t bin = 0;
                while (bin < 5u && interval > bounds[bin]) ++bin;
                ++interval_bins_[bin];
                interval_max_us_ = std::max(interval_max_us_,
                                            static_cast<std::uint32_t>(interval));
            }
        }
        last_presented_ = now;
        break;
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

    if (std::FILE* log = frame_log()) {
        static const char outcomes[] = {'P', 'p', 'r', 'F'};
        (void)std::fprintf(
            log, "%c %lld %lld %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
            outcomes[static_cast<int>(measurement.outcome)],
            static_cast<long long>(std::chrono::duration_cast<
                std::chrono::milliseconds>(now.time_since_epoch()).count()),
            static_cast<long long>(interval_us), measurement.total_us,
            measurement.update_us, measurement.prepare_us,
            measurement.map_render_us, measurement.ui_render_us,
            measurement.ui_composite_us, measurement.results_us,
            measurement.guidance_us, measurement.tracker_us,
            measurement.route_sync_us, measurement.guidance_ui_us,
            measurement.input_us, measurement.camera_us, measurement.vector_us,
            measurement.post_us, measurement.cpu_us, measurement.vec_take_us,
            measurement.vec_uploads, measurement.vec_route_us, measurement.vec_want_us,
            measurement.vec_draw_us, measurement.vec_labels_us, measurement.vec_evict_us,
            measurement.vec_upload_max_us, measurement.vec_atlas_us,
            measurement.vec_label_buffers_us);
    }

    if (now - window_started_ >= std::chrono::seconds(1))
        report(now);
}

void FrameMetrics::report(std::chrono::steady_clock::time_point now) noexcept
{
    const auto elapsed_ms = std::max<std::int64_t>(1,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - window_started_).count());
    char line[640] = {};
    (void)std::snprintf(
        line, sizeof(line),
        "perf window_ms=%lld presented=%u fps_x10=%llu pending_prepare=%u "
        "pending_render=%u faults=%u total_p50_ms=%u total_p95_ms=%u "
        "total_p99_ms=%u total_max_us=%u update_p95_ms=%u prepare_p95_ms=%u "
        "map_p95_ms=%u ui_render_p95_ms=%u ui_composite_p95_ms=%u "
        "ui_upload_bytes=%llu interval_le33=%u interval_le50=%u "
        "interval_le67=%u interval_le100=%u interval_le200=%u "
        "interval_gt200=%u interval_max_us=%u",
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
        static_cast<unsigned long long>(ui_upload_bytes_),
        interval_bins_[0], interval_bins_[1], interval_bins_[2],
        interval_bins_[3], interval_bins_[4], interval_bins_[5],
        interval_max_us_);
    trace(line);
    if (std::FILE* log = frame_log())
        (void)std::fflush(log);
    interval_bins_.fill(0u);
    interval_max_us_ = 0u;

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
