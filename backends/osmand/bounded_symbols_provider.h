#pragma once

#include <OsmAndCore/Map/MapObjectsSymbolsProvider.h>

#include <cstddef>
#include <memory>

namespace navicore_osmand {

// Keeps OsmAnd's retained GLES2 symbol path, but bounds the number of symbol
// groups published for each tile. OsmAnd still owns rasterization, placement,
// collision and GPU resource lifetimes.
class BoundedSymbolsProvider final
    : public OsmAnd::MapObjectsSymbolsProvider {
public:
    BoundedSymbolsProvider(
        const std::shared_ptr<OsmAnd::MapPrimitivesProvider>& primitives,
        std::size_t maximum_groups_per_tile);

    bool obtainData(
        const OsmAnd::IMapDataProvider::Request& request,
        std::shared_ptr<OsmAnd::IMapDataProvider::Data>& output,
        std::shared_ptr<OsmAnd::Metric>* metric = nullptr) override;

private:
    const std::size_t maximum_groups_per_tile_;
};

} // namespace navicore_osmand
