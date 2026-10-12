#include "precache_worker.h"

#include "caching_raster_provider.h"
#include "diagnostics.h"
#include "thread_priority.h"

#include <OsmAndCore/FunctorQueryController.h>
#include <OsmAndCore/ObfsCollection.h>
#include <OsmAndCore/Map/IMapTiledDataProvider.h>
#include <OsmAndCore/Map/IRasterMapLayerProvider.h>
#include <OsmAndCore/Map/MapPresentationEnvironment.h>
#include <OsmAndCore/Map/MapPrimitivesProvider.h>
#include <OsmAndCore/Map/MapPrimitiviser.h>
#include <OsmAndCore/Map/MapRasterLayerProvider_Software.h>
#include <OsmAndCore/Map/ObfMapObjectsProvider.h>
#include <OsmAndCore/Map/ResolvedMapStyle.h>

#include <chrono>
#include <cstdio>

namespace navicore_osmand {

PrecacheWorker::PrecacheWorker(Job job) : job_(std::move(job))
{
    // Start last, after every member is constructed.
    thread_ = std::thread([this]() { run(); });
}

PrecacheWorker::~PrecacheWorker() { stop(); }

void PrecacheWorker::stop() noexcept
{
    stopping_ = true;
    if (thread_.joinable())
        thread_.join();
}

void PrecacheWorker::write_progress(std::uint64_t done, std::uint64_t total,
                                    std::uint64_t failed) const
{
    const std::string path = job_.manifest_path + ".progress";
    const std::string temporary = path + ".tmp";
    if (std::FILE* file = std::fopen(temporary.c_str(), "w")) {
        std::fprintf(file, "%llu %llu %llu\n",
                     static_cast<unsigned long long>(done),
                     static_cast<unsigned long long>(total),
                     static_cast<unsigned long long>(failed));
        std::fclose(file);
        std::rename(temporary.c_str(), path.c_str());
    }
}

void PrecacheWorker::run() noexcept
{
    try {
        (void)lower_current_thread_priority(4);
        auto presentation = std::make_shared<OsmAnd::MapPresentationEnvironment>(
            job_.style, 1.0f, 1.0f, 1.0f);
        presentation->setLocaleLanguageId(job_.locale);
        presentation->setSettings(job_.style_settings);
        auto primitiviser = std::make_shared<OsmAnd::MapPrimitiviser>(presentation);
        auto objects = std::make_shared<OsmAnd::ObfMapObjectsProvider>(job_.obfs);
        auto primitives = std::make_shared<OsmAnd::MapPrimitivesProvider>(
            objects, primitiviser, job_.store.tile_size);
        OsmAnd::MapRasterLayerProvider_Software raster(primitives);
        TilePackStore store(job_.store);

        std::uint64_t total = 0;
        for (const auto& range : job_.ranges)
            total += range.count();
        std::uint64_t done = 0;
        std::uint64_t failed = 0;
        std::vector<std::uint8_t> pixels;
        // OsmAnd providers dereference request.queryController; it also
        // lets stop() abort a tile that is being rasterized.
        const auto controller = std::make_shared<OsmAnd::FunctorQueryController>(
            [this](const OsmAnd::FunctorQueryController*) {
                return stopping_.load();
            });
        trace("precache=start");
        write_progress(0, total, 0);
        for (const auto& range : job_.ranges) {
            for (std::uint32_t y = range.min_y; y <= range.max_y; ++y) {
                for (std::uint32_t x = range.min_x; x <= range.max_x; ++x) {
                    if (stopping_) {
                        write_progress(done, total, failed);
                        trace("precache=stopped");
                        finished_ = true;
                        return;
                    }
                    // Back off while driving so the renderer keeps its CPU.
                    while (throttled_ && !stopping_)
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(200));
                    const TileKey key{range.zoom, x, y};
                    if (!store.contains(key)) {
                        OsmAnd::IMapTiledDataProvider::Request request;
                        request.tileId = OsmAnd::TileId::fromXY(
                            static_cast<int32_t>(x), static_cast<int32_t>(y));
                        request.zoom = static_cast<OsmAnd::ZoomLevel>(range.zoom);
                        request.detailedZoom = request.zoom;
                        request.queryController = controller;
                        std::shared_ptr<OsmAnd::IMapDataProvider::Data> data;
                        const auto raster_data = raster.obtainData(request, data)
                            ? std::dynamic_pointer_cast<
                                  OsmAnd::IRasterMapLayerProvider::Data>(data)
                            : nullptr;
                        const bool stored = raster_data &&
                            !raster_data->images.isEmpty() &&
                            raster_data->images.constBegin().value() &&
                            encode_tile(*raster_data->images.constBegin().value(),
                                        job_.store.format, job_.store.tile_size,
                                        pixels) &&
                            store.put(key, pixels.data(), pixels.size());
                        if (!stored)
                            ++failed;
                    }
                    ++done;
                    if (done % 200 == 0) {
                        write_progress(done, total, failed);
                        trace_result("precache=progress", done, total);
                    }
                }
            }
        }
        write_progress(done, total, failed);
        trace_result("precache=done", done, failed);
    } catch (...) {
        trace("error=precache_exception");
    }
    finished_ = true;
}

} // namespace navicore_osmand
