// Runs the product worker in a separate process against the installed maps.
// No screen, GPS injection, persistent markers, or navigation service restart.
#include "navigation_worker.h"
#include "config.h"
#include "diagnostics.h"
#include "session.h"
#include <OsmAndCore.h>
#include <OsmAndCore/CoreResourcesEmbeddedBundle.h>
#include <OsmAndCore/ObfsCollection.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
const auto started = Clock::now();
std::atomic<bool> ready{false};
std::atomic<long long> deadline{120000};
std::atomic<unsigned> turn_errors{0};
bool wait_for_ch = true;
long long elapsed() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count(); }
void poll_pause() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
double metres(navicore_osmand::ui::GeoPoint a, navicore_osmand::ui::GeoPoint b) {
    constexpr double rad = 3.14159265358979323846 / 180.0;
    const double p = (b.latitude - a.latitude) * rad;
    const double q = (b.longitude - a.longitude) * rad;
    const double h = std::sin(p/2)*std::sin(p/2) + std::cos(a.latitude*rad)*std::cos(b.latitude*rad)*
                     std::sin(q/2)*std::sin(q/2);
    return 12742000.0 * std::asin(std::sqrt(std::min(1.0, std::max(0.0, h))));
}
std::uint64_t fingerprint(const std::string& text) {
    std::uint64_t value = 14695981039346656037ull;
    for (unsigned char c : text) { value ^= c; value *= 1099511628211ull; }
    return value;
}
struct Case {
    std::string id;
    navicore_osmand::ui::GeoPoint origin, destination;
    double heading = -1;
};
}
namespace navicore_osmand {
// Keep diagnostic worker traces out of the running product's log.
void trace(const char* message) noexcept {
    std::printf("%lld %s\n", elapsed(), message);
    if (std::strncmp(message, "worker=omaps_error", 18) == 0) ++turn_errors;
    if (std::strncmp(message, "worker=ch ", 10) == 0 ||
        std::strncmp(message, "worker=ch_off ", 14) == 0) ready = true;
    if (!wait_for_ch && std::strncmp(message, "worker=router ", 14) == 0) ready = true;
}
void trace_result(const char* stage, std::uint64_t revision, std::size_t count) noexcept {
    char line[160];
    std::snprintf(line, sizeof(line), "%s revision=%llu count=%zu", stage,
                  static_cast<unsigned long long>(revision), count);
    trace(line);
}
int Wakeup::request() const noexcept { return 0; }
}

extern "C" __attribute__((visibility("default")))
int route_worker_bench_main(int argc, char** argv) {
    const bool batch = argc == 7 && std::strcmp(argv[6], "batch") == 0;
    if (argc != 10 && !batch) {
        std::fprintf(stderr, "usage: worker MAP XML SIDECAR LAT1 LON1 LAT2 LON2 route|reject|cold\n"
                             "       worker MAP XML SIDECAR CASES.tsv batch\n");
        return 2;
    }
    wait_for_ch = std::strcmp(argv[4], "-") != 0;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // A timeout exits only this diagnostic process, never the navigation service.
    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (elapsed() > deadline.load()) {
                std::fprintf(stderr, "diagnostic timeout: no request completed within 120 seconds\n");
                std::_Exit(124);
            }
        }
    }).detach();
    using namespace navicore_osmand;
    auto resources = OsmAnd::CoreResourcesEmbeddedBundle::loadFromLibrary(
        QString::fromLatin1("/open-navicore/libOsmAndCore_ResourcesBundle_shared.so"));
    if (!resources || !OsmAnd::InitializeCore(resources, "/datastore/navicore-backend/fonts")) return 2;
    const auto maps = obf_files(argv[2]);
    auto obfs = std::make_shared<OsmAnd::ObfsCollection>();
    for (const auto& file : maps) if (!is_basemap(file)) obfs->addFile(QString::fromLocal8Bit(file.c_str()));
    std::ifstream input(argv[3]);
    const std::string xml((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (xml.empty()) return 2;
    std::vector<Case> cases;
    if (batch) {
        std::ifstream file(argv[5]);
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream row(line);
            Case item;
            if (!(row >> item.id >> item.origin.latitude >> item.origin.longitude >>
                  item.destination.latitude >> item.destination.longitude)) return 2;
            row >> item.heading; // optional heading in degrees
            cases.push_back(std::move(item));
        }
        if (cases.empty()) return 2;
    } else {
        Case item{"single", {std::atof(argv[5]), std::atof(argv[6])},
                             {std::atof(argv[7]), std::atof(argv[8])}};
        cases = {item, item};
    }
    std::printf("bench=cases count=%zu mode=%s\n", cases.size(), batch ? "batch" : argv[9]);
    {
        NavigationWorker worker(obfs, {}, maps, xml, std::string(argv[2]) + "/search.idx", wait_for_ch ? argv[4] : "");
        if (batch || std::strcmp(argv[9], "cold") != 0) while (!ready) poll_pause();
        for (std::size_t run = 0; run < cases.size(); ++run) {
            const auto& item = cases[run];
            ui::Destination destination;
            destination.id = item.id; destination.title = item.id;
            destination.latitude = item.destination.latitude;
            destination.longitude = item.destination.longitude;
            turn_errors = 0;
            deadline = elapsed() + 120000;
            std::printf("batch=begin id=%s origin=%.8f,%.8f destination=%.8f,%.8f\n",
                item.id.c_str(), item.origin.latitude, item.origin.longitude,
                destination.latitude, destination.longitude);
            const auto submitted = Clock::now();
            worker.submit_route(run + 2, destination, item.origin, false,
                item.heading >= 0 ? std::optional<double>(item.heading) : std::nullopt);
            NavigationWorker::RouteResult result;
            while (!worker.take_route_result(result)) poll_pause();
            const auto wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - submitted).count();
            std::printf("bench=complete run=%zu wall_ms=%lld routes=%zu distance_m=%.0f points=%zu\n", run,
                static_cast<long long>(wall_ms), result.routes.size(),
                result.routes.empty() ? 0.0 : result.routes.front().distance_metres,
                result.routes.empty() ? 0u : result.routes.front().geometry.size());
            if (batch) {
                double start_gap = -1, end_gap = -1, geometry = 0, max_step = 0;
                bool finite = true;
                std::size_t maneuvers = 0;
                std::uint64_t geometry_hash = 0, turns_hash = 0;
                double duration = 0;
                if (!result.routes.empty()) {
                    const auto& route = result.routes.front();
                    maneuvers = route.maneuvers.size();
                    duration = route.duration_seconds;
                    std::ostringstream shape, turns;
                    shape.precision(15); turns.precision(15);
                    for (const auto& p : route.geometry) shape << p.latitude << ',' << p.longitude << ';';
                    for (const auto& m : route.maneuvers) {
                        turns << static_cast<int>(m.kind) << ',' << m.road << ',' << m.distance_from_start_metres
                              << ',' << m.geometry_index << ',' << m.roundabout_exit << ',' << m.left_side
                              << ',' << m.turn_angle_degrees << ',' << m.junction.valid << ',' << m.junction.route_angle
                              << ',' << m.junction.through_road << ',' << m.junction.truncated << ','
                              << m.junction.recommended_lanes << ',' << static_cast<int>(m.junction.lane_change);
                        for (const auto& lane : m.lanes) turns << ':' << static_cast<int>(lane.kind) << ','
                            << static_cast<int>(lane.also) << ',' << lane.combined << ',' << lane.recommended;
                        for (unsigned j = 0; j < m.junction.branch_count; ++j) turns << '/' << m.junction.branches[j];
                        turns << ';';
                    }
                    geometry_hash = fingerprint(shape.str()); turns_hash = fingerprint(turns.str());
                    if (!route.geometry.empty()) {
                        start_gap = metres(item.origin, route.geometry.front());
                        end_gap = metres(item.destination, route.geometry.back());
                    }
                    finite = std::isfinite(route.distance_metres) && std::isfinite(route.duration_seconds);
                    for (std::size_t i = 0; i < route.geometry.size(); ++i) {
                        const auto& point = route.geometry[i];
                        finite = finite && std::isfinite(point.latitude) && std::isfinite(point.longitude) &&
                            std::fabs(point.latitude) <= 90 && std::fabs(point.longitude) <= 180;
                        if (i) {
                            const double step = metres(route.geometry[i-1], point);
                            geometry += step; max_step = std::max(max_step, step);
                        }
                    }
                }
                std::printf("batch=case id=%s status=%s wall_ms=%lld routes=%zu air_m=%.1f distance_m=%.1f "
                    "start_gap_m=%.1f end_gap_m=%.1f geometry_m=%.1f max_step_m=%.1f finite=%d maneuvers=%zu omaps_errors=%u duration_s=%.3f geometry_hash=%llu turns_hash=%llu\n",
                    item.id.c_str(), result.revision != run + 2 ? "revision_mismatch" : result.routes.empty() ? "no_route" : "ok",
                    static_cast<long long>(wall_ms), result.routes.size(), metres(item.origin, item.destination),
                    result.routes.empty() ? 0 : result.routes.front().distance_metres,
                    start_gap, end_gap, geometry, max_step, finite ? 1 : 0, maneuvers, turn_errors.load(), duration,
                    static_cast<unsigned long long>(geometry_hash), static_cast<unsigned long long>(turns_hash));
                continue;  // A failed route is a recorded observation, not the end of the survey.
            }
            const bool reject = std::strcmp(argv[9], "reject") == 0;
            const char* limit = std::getenv("ROUTE_WORKER_MAX_MS");
            if (result.revision != run + 2 || result.routes.empty() != reject ||
                (limit && wall_ms > std::atoll(limit))) {
                std::fprintf(stderr, "bench=failed unexpected revision, route count, or latency\n");
                std::_Exit(1);
            }
        }
        worker.stop();
    }
    std::printf("bench=%s\n", batch ? "batch_complete" : "passed");
    // OsmAnd's QNX global teardown is process-owned, as in route_bench.
    std::_Exit(0);
    return 0;
}
