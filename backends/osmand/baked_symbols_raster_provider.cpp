#include "baked_symbols_raster_provider.h"

#include <OsmAndCore/Map/BillboardRasterMapSymbol.h>
#include <OsmAndCore/Map/IRasterMapLayerProvider.h>
#include <OsmAndCore/Map/OnPathRasterMapSymbol.h>
#include <OsmAndCore/Map/RasterMapSymbol.h>
#include <OsmAndCore/Utilities.h>

#include <OsmAndCore/ignore_warnings_on_external_includes.h>
#include <SkBitmap.h>
#include <SkCanvas.h>
#include <SkImage.h>
#include <OsmAndCore/restore_internal_warnings.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace navicore_osmand {
namespace {

constexpr std::size_t maximum_symbols_per_tile = 24u;
constexpr int collision_padding_pixels = 2;

struct Rectangle {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct Candidate {
    sk_sp<const SkImage> image;
    OsmAnd::PointI position31;
    OsmAnd::PointI offset;
    OsmAnd::AreaI margin;
    int order = 0;
    std::uintptr_t instance = 0u;
};

bool overlaps(const Rectangle& a, const Rectangle& b)
{
    return a.left < b.right && a.right > b.left &&
           a.top < b.bottom && a.bottom > b.top;
}

void append_candidate(
    std::vector<Candidate>& output,
    const std::shared_ptr<OsmAnd::MapSymbol>& symbol,
    const OsmAnd::PointI& position,
    const OsmAnd::PointI& offset,
    std::uintptr_t instance)
{
    if (!symbol || symbol->isHidden)
        return;
    const auto raster = std::dynamic_pointer_cast<OsmAnd::RasterMapSymbol>(symbol);
    if (!raster || !raster->image || raster->image->width() <= 0 ||
        raster->image->height() <= 0)
        return;
    output.push_back({raster->image, position, offset, raster->margin,
                      symbol->order, instance});
}

void append_original_group(
    std::vector<Candidate>& output,
    const std::shared_ptr<OsmAnd::MapSymbolsGroup>& group)
{
    const auto instance = reinterpret_cast<std::uintptr_t>(group.get());
    for (const auto& symbol : group->symbols) {
        if (const auto billboard =
                std::dynamic_pointer_cast<OsmAnd::BillboardRasterMapSymbol>(symbol)) {
            append_candidate(output, symbol, billboard->position31,
                             billboard->offset, instance);
        } else if (const auto on_path =
                       std::dynamic_pointer_cast<OsmAnd::OnPathRasterMapSymbol>(symbol)) {
            append_candidate(output, symbol, on_path->pinPointOnPath.point31,
                             OsmAnd::PointI(0, 0), instance);
        }
    }
}

void append_additional_instances(
    std::vector<Candidate>& output,
    const std::shared_ptr<OsmAnd::MapSymbolsGroup>& group)
{
    for (const auto& additional : group->additionalInstances) {
        const auto instance = reinterpret_cast<std::uintptr_t>(additional.get());
        for (auto it = additional->symbols.constBegin();
             it != additional->symbols.constEnd(); ++it) {
            const auto& symbol = it.key();
            const auto& parameters = it.value();
            if (const auto billboard =
                    std::dynamic_pointer_cast<OsmAnd::BillboardRasterMapSymbol>(symbol)) {
                auto position = billboard->position31;
                auto offset = billboard->offset;
                if (const auto overrides = std::dynamic_pointer_cast<const
                        OsmAnd::MapSymbolsGroup::AdditionalBillboardSymbolInstanceParameters>(
                            parameters)) {
                    if (overrides->overridesPosition31)
                        position = overrides->position31;
                    if (overrides->overridesOffset)
                        offset = overrides->offset;
                }
                append_candidate(output, symbol, position, offset, instance);
            } else if (const auto on_path =
                           std::dynamic_pointer_cast<OsmAnd::OnPathRasterMapSymbol>(symbol)) {
                auto pin = on_path->pinPointOnPath;
                if (const auto overrides = std::dynamic_pointer_cast<const
                        OsmAnd::MapSymbolsGroup::AdditionalOnPathSymbolInstanceParameters>(
                            parameters)) {
                    if (overrides->overridesPinPointOnPath)
                        pin = overrides->pinPointOnPath;
                }
                append_candidate(output, symbol, pin.point31,
                                 OsmAnd::PointI(0, 0), instance);
            }
        }
    }
}

} // namespace

BakedSymbolsRasterProvider::BakedSymbolsRasterProvider(
    const std::shared_ptr<OsmAnd::MapPrimitivesProvider>& primitives,
    const std::shared_ptr<OsmAnd::MapObjectsSymbolsProvider>& symbols,
    int viewport_width,
    int viewport_height)
    : OsmAnd::MapRasterLayerProvider_Software(primitives)
    , symbols_(symbols)
    , viewport_size_(viewport_width, viewport_height)
{
}

bool BakedSymbolsRasterProvider::obtainData(
    const OsmAnd::IMapDataProvider::Request& request,
    std::shared_ptr<OsmAnd::IMapDataProvider::Data>& output,
    std::shared_ptr<OsmAnd::Metric>* metric)
{
    std::shared_ptr<OsmAnd::IMapDataProvider::Data> base_data;
    if (!OsmAnd::MapRasterLayerProvider_Software::obtainData(
            request, base_data, metric))
        return false;
    if (!base_data || !symbols_) {
        output = base_data;
        return true;
    }

    const auto raster = std::dynamic_pointer_cast<
        OsmAnd::IRasterMapLayerProvider::Data>(base_data);
    const auto tiled_request = dynamic_cast<const
        OsmAnd::IMapTiledDataProvider::Request*>(&request);
    if (!raster || !tiled_request || raster->images.isEmpty()) {
        output = base_data;
        return true;
    }
    const auto base_image = raster->images.constBegin().value();
    if (!base_image) {
        output = base_data;
        return true;
    }

    OsmAnd::IMapTiledSymbolsProvider::Request symbol_request;
    symbol_request.queryController = request.queryController;
    symbol_request.tileId = tiled_request->tileId;
    symbol_request.zoom = tiled_request->zoom;
    symbol_request.detailedZoom = tiled_request->detailedZoom;
    symbol_request.visibleArea31 = tiled_request->visibleArea31;
    symbol_request.areaTime = tiled_request->areaTime;
    symbol_request.cacheOnly = tiled_request->cacheOnly;
    symbol_request.mapState.windowSize = viewport_size_;
    symbol_request.mapState.viewport = OsmAnd::AreaI(
        0, 0, viewport_size_.y, viewport_size_.x);
    symbol_request.mapState.zoomLevel = tiled_request->zoom;
    symbol_request.mapState.surfaceZoomLevel = tiled_request->zoom;

    std::shared_ptr<OsmAnd::IMapTiledSymbolsProvider::Data> symbol_data;
    if (!symbols_->obtainTiledSymbols(symbol_request, symbol_data) ||
        !symbol_data || symbol_data->symbolsGroups.isEmpty()) {
        output = base_data;
        return true;
    }

    std::vector<Candidate> candidates;
    for (const auto& group : symbol_data->symbolsGroups) {
        if (!group)
            continue;
        if (!group->additionalInstancesDiscardOriginal)
            append_original_group(candidates, group);
        append_additional_instances(candidates, group);
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const Candidate& left, const Candidate& right) {
            return left.order < right.order;
        });

    const int tile_size = static_cast<int>(getTileSize());
    SkBitmap bitmap;
    if (!bitmap.tryAllocPixels(SkImageInfo::MakeN32Premul(
            tile_size, tile_size))) {
        output = base_data;
        return true;
    }
    SkCanvas canvas(bitmap);
    canvas.drawImage(base_image.get(), 0.0f, 0.0f);

    const auto bounds = OsmAnd::Utilities::tileBoundingBox31(
        tiled_request->tileId, tiled_request->zoom);
    const double scale_x = static_cast<double>(tile_size) /
                           static_cast<double>(bounds.width());
    const double scale_y = static_cast<double>(tile_size) /
                           static_cast<double>(bounds.height());
    std::vector<std::pair<Rectangle, std::uintptr_t>> accepted;
    accepted.reserve(maximum_symbols_per_tile);
    std::vector<std::pair<const Candidate*, Rectangle>> drawables;
    drawables.reserve(maximum_symbols_per_tile);

    for (const auto& candidate : candidates) {
        const int center_x = static_cast<int>(std::lround(
            static_cast<double>(candidate.position31.x - bounds.left()) *
            scale_x)) + candidate.offset.x;
        const int center_y = static_cast<int>(std::lround(
            static_cast<double>(candidate.position31.y - bounds.top()) *
            scale_y)) + candidate.offset.y;
        const int half_width = candidate.image->width() / 2;
        const int half_height = candidate.image->height() / 2;
        Rectangle rectangle{
            center_x - half_width - candidate.margin.left() -
                collision_padding_pixels,
            center_y - half_height - candidate.margin.top() -
                collision_padding_pixels,
            center_x - half_width + candidate.image->width() +
                candidate.margin.right() + collision_padding_pixels,
            center_y - half_height + candidate.image->height() +
                candidate.margin.bottom() + collision_padding_pixels};

        // A tile cannot render overflow into its neighbor.  Reject boundary
        // crossings instead of showing chopped glyphs.
        if (rectangle.left < 0 || rectangle.top < 0 ||
            rectangle.right > tile_size || rectangle.bottom > tile_size)
            continue;
        bool collides = false;
        for (const auto& placed : accepted) {
            if (placed.second != candidate.instance &&
                overlaps(rectangle, placed.first)) {
                collides = true;
                break;
            }
        }
        if (collides)
            continue;
        accepted.emplace_back(rectangle, candidate.instance);
        drawables.emplace_back(&candidate, rectangle);
        if (drawables.size() == maximum_symbols_per_tile)
            break;
    }

    // Lower-priority symbols go down first; the most important symbol in an
    // accepted overlap group remains visually on top.
    for (auto it = drawables.rbegin(); it != drawables.rend(); ++it) {
        const auto* candidate = it->first;
        const auto& rectangle = it->second;
        const float x = static_cast<float>(
            rectangle.left + candidate->margin.left() +
            collision_padding_pixels);
        const float y = static_cast<float>(
            rectangle.top + candidate->margin.top() +
            collision_padding_pixels);
        canvas.drawImage(candidate->image.get(), x, y);
    }

    bitmap.setImmutable();
    output = std::make_shared<OsmAnd::IRasterMapLayerProvider::Data>(
        tiled_request->tileId, tiled_request->zoom,
        raster->alphaChannelPresence, raster->densityFactor,
        bitmap.asImage());
    return true;
}

} // namespace navicore_osmand
