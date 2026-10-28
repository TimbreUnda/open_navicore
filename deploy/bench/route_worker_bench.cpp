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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
const auto started = Clock::now();
std::atomic<bool> ready{false};
bool wait_for_ch = true;
long long elapsed() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count(); }
void poll_pause() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
}
namespace navicore_osmand {
// Keep diagnostic worker traces out of the running product's log.
void trace(const char* message) noexcept {
    std::printf("%lld %s\n", elapsed(), message);
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
    if (argc != 10) {
        std::fprintf(stderr, "usage: worker MAP XML SIDECAR LAT1 LON1 LAT2 LON2 route|reject|cold\n");
        return 2;
    }
    wait_for_ch = std::strcmp(argv[4], "-") != 0;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // A timeout exits only this diagnostic process, never the navigation service.
    std::thread([] { std::this_thread::sleep_for(std::chrono::seconds(120));
        std::fprintf(stderr, "diagnostic timeout after 120 seconds\n"); std::_Exit(124); }).detach();
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
    const ui::GeoPoint origin{std::atof(argv[5]), std::atof(argv[6])};
    ui::Destination destination;
    destination.id = "bench"; destination.title = "Bench destination";
    destination.latitude = std::atof(argv[7]); destination.longitude = std::atof(argv[8]);
    std::printf("case=%s origin=%.7f,%.7f destination=%.7f,%.7f\n", argv[9],
                origin.latitude, origin.longitude, destination.latitude, destination.longitude);
    {
        NavigationWorker worker(obfs, {}, maps, xml, std::string(argv[2]) + "/search.idx", wait_for_ch ? argv[4] : "");
        if (std::strcmp(argv[9], "cold") != 0) while (!ready) poll_pause();
        for (unsigned run = 0; run < 2; ++run) {
            const auto submitted = Clock::now();
            worker.submit_route(run + 2, destination, origin, false, {});
            NavigationWorker::RouteResult result;
            while (!worker.take_route_result(result)) poll_pause();
            const auto wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - submitted).count();
            std::printf("bench=complete run=%u wall_ms=%lld routes=%zu distance_m=%.0f points=%zu\n", run,
                static_cast<long long>(wall_ms), result.routes.size(),
                result.routes.empty() ? 0.0 : result.routes.front().distance_metres,
                result.routes.empty() ? 0u : result.routes.front().geometry.size());
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
    std::printf("bench=passed\n");
    // OsmAnd's QNX global teardown is process-owned, as in route_bench.
    std::_Exit(0);
    return 0;
}
