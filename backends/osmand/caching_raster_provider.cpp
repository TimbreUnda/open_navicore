#include "caching_raster_provider.h"

#include <OsmAndCore/Map/IMapTiledDataProvider.h>
#include <OsmAndCore/Map/IRasterMapLayerProvider.h>

#include <OsmAndCore/ignore_warnings_on_external_includes.h>
#include <SkData.h>
#include <SkImage.h>
#include <SkImageInfo.h>
#include <SkPixmap.h>
#include <OsmAndCore/restore_internal_warnings.h>

namespace navicore_osmand {
namespace {
SkImageInfo tile_info(TileFormat format, std::uint32_t tile_size)
{
    const int size = static_cast<int>(tile_size);
    return format == TileFormat::Rgb565
        ? SkImageInfo::Make(size, size, kRGB_565_SkColorType,
                            kOpaque_SkAlphaType)
        : SkImageInfo::Make(size, size, kRGBA_8888_SkColorType,
                            kPremul_SkAlphaType);
}
} // namespace

bool encode_tile(const SkImage& image, TileFormat format,
                 std::uint32_t tile_size, std::vector<std::uint8_t>& pixels)
{
    if (image.width() != static_cast<int>(tile_size) ||
        image.height() != static_cast<int>(tile_size))
        return false;
    const SkImageInfo info = tile_info(format, tile_size);
    pixels.resize(tile_bytes(format, tile_size));
    // Skia converts from the rasterizer's colour type as needed.
    return image.readPixels(nullptr, info, pixels.data(), info.minRowBytes(),
                            0, 0, SkImage::kDisallow_CachingHint);
}

sk_sp<const SkImage> decode_tile(const std::vector<std::uint8_t>& pixels,
                                 TileFormat format, std::uint32_t tile_size)
{
    const SkImageInfo info = tile_info(format, tile_size);
    if (pixels.size() != info.computeMinByteSize())
        return nullptr;
    // One copy into an immutable SkData the image owns.
    auto data = SkData::MakeWithCopy(pixels.data(), pixels.size());
    return SkImage::MakeRasterData(info, std::move(data), info.minRowBytes());
}

CachingRasterProvider::CachingRasterProvider(
    const std::shared_ptr<OsmAnd::MapPrimitivesProvider>& primitives)
    : OsmAnd::MapRasterLayerProvider_Software(primitives)
{
}

void CachingRasterProvider::set_cache(std::shared_ptr<TileCache> cache)
{
    std::lock_guard<std::mutex> lock(mutex_);
    cache_ = std::move(cache);
}

std::shared_ptr<TileCache> CachingRasterProvider::cache() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_;
}

bool CachingRasterProvider::obtainData(
    const OsmAnd::IMapDataProvider::Request& request,
    std::shared_ptr<OsmAnd::IMapDataProvider::Data>& output,
    std::shared_ptr<OsmAnd::Metric>* metric)
{
    const auto tiled = dynamic_cast<const
        OsmAnd::IMapTiledDataProvider::Request*>(&request);
    const auto store = cache();
    if (!tiled || !store || tiled->zoom < 0)
        return OsmAnd::MapRasterLayerProvider_Software::obtainData(
            request, output, metric);

    const TileKey key{static_cast<std::uint32_t>(tiled->zoom),
                      static_cast<std::uint32_t>(tiled->tileId.x),
                      static_cast<std::uint32_t>(tiled->tileId.y)};
    // Not thread_local: dynamic TLS in this dlopen()ed plugin (static
    // libstdc++, QNX 6.6) is not trusted; one allocation per tile is cheap
    // next to rasterizing it.
    std::vector<std::uint8_t> pixels;
    if (store->get(key, pixels)) {
        if (auto image = decode_tile(pixels, store->format(),
                                     store->tile_size())) {
            output = std::make_shared<OsmAnd::IRasterMapLayerProvider::Data>(
                tiled->tileId, tiled->zoom,
                store->format() == TileFormat::Rgb565
                    ? OsmAnd::AlphaChannelPresence::NotPresent
                    : OsmAnd::AlphaChannelPresence::Present,
                getTileDensityFactor(), image);
            return true;
        }
    }
    if (!OsmAnd::MapRasterLayerProvider_Software::obtainData(
            request, output, metric))
        return false;
    const auto raster = std::dynamic_pointer_cast<
        OsmAnd::IRasterMapLayerProvider::Data>(output);
    if (raster && !raster->images.isEmpty()) {
        const auto image = raster->images.constBegin().value();
        if (image && encode_tile(*image, store->format(), store->tile_size(),
                                 pixels))
            (void)store->put(key, pixels.data(), pixels.size());
    }
    return true;
}

} // namespace navicore_osmand
