#pragma once

#include "tile_cache.h"

#include <OsmAndCore/Map/MapRasterLayerProvider_Software.h>

#include <memory>
#include <mutex>

class SkImage;
template <typename T> class sk_sp;

namespace navicore_osmand {

// OsmAnd software raster provider backed by the persistent tile cache. A hit
// returns the stored pixels as an SkImage without touching OBF data or Skia
// rasterization; a miss rasterizes normally and stores the result. In RGB565
// mode hits are uploaded natively, skipping OsmAnd's 8888->565 conversion.
class CachingRasterProvider final
    : public OsmAnd::MapRasterLayerProvider_Software {
public:
    explicit CachingRasterProvider(
        const std::shared_ptr<OsmAnd::MapPrimitivesProvider>& primitives);

    // Swaps the cache, e.g. when night mode changes the style namespace.
    // nullptr disables caching.
    void set_cache(std::shared_ptr<TileCache> cache);

    bool obtainData(
        const OsmAnd::IMapDataProvider::Request& request,
        std::shared_ptr<OsmAnd::IMapDataProvider::Data>& output,
        std::shared_ptr<OsmAnd::Metric>* metric = nullptr) override;

private:
    std::shared_ptr<TileCache> cache() const;

    mutable std::mutex mutex_;
    std::shared_ptr<TileCache> cache_;
};

// Converts an OsmAnd tile image to cache pixels and back.
bool encode_tile(const SkImage& image, TileFormat format,
                 std::uint32_t tile_size, std::vector<std::uint8_t>& pixels);
sk_sp<const SkImage> decode_tile(const std::vector<std::uint8_t>& pixels,
                                 TileFormat format, std::uint32_t tile_size);

} // namespace navicore_osmand
