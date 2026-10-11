#pragma once

#include <OsmAndCore/Map/MapObjectsSymbolsProvider.h>
#include <OsmAndCore/Map/MapRasterLayerProvider_Software.h>

#include <memory>

namespace navicore_osmand {

// Flattens a bounded set of OsmAnd's already-rasterized map symbols into the
// corresponding basemap tile.  The map renderer then retains and draws one
// texture per tile instead of maintaining hundreds of independently placed
// symbol resources during every moving frame.
class BakedSymbolsRasterProvider final
    : public OsmAnd::MapRasterLayerProvider_Software {
public:
    BakedSymbolsRasterProvider(
        const std::shared_ptr<OsmAnd::MapPrimitivesProvider>& primitives,
        const std::shared_ptr<OsmAnd::MapObjectsSymbolsProvider>& symbols,
        int viewport_width,
        int viewport_height);

    bool obtainData(
        const OsmAnd::IMapDataProvider::Request& request,
        std::shared_ptr<OsmAnd::IMapDataProvider::Data>& output,
        std::shared_ptr<OsmAnd::Metric>* metric = nullptr) override;

private:
    const std::shared_ptr<OsmAnd::MapObjectsSymbolsProvider> symbols_;
    const OsmAnd::PointI viewport_size_;
};

} // namespace navicore_osmand
