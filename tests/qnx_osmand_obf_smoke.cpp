#include <cstdio>
#include <memory>

#include <OsmAndCore/ObfDataInterface.h>
#include <OsmAndCore/ObfsCollection.h>
#include <OsmAndCore/Utilities.h>
#include <OsmAndCore/Data/BinaryMapObject.h>

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: qnx-osmand-obf-smoke MAP.obf\n");
        return 2;
    }
    OsmAnd::ObfsCollection collection;
    collection.addFile(QString::fromLocal8Bit(argv[1]));
    const auto data = collection.obtainDataInterface();
    if (!data)
        return 3;

    QList<std::shared_ptr<const OsmAnd::BinaryMapObject>> objects;
    const OsmAnd::AreaI kyiv(
        OsmAnd::Utilities::get31TileNumberY(50.55),
        OsmAnd::Utilities::get31TileNumberX(30.35),
        OsmAnd::Utilities::get31TileNumberY(50.35),
        OsmAnd::Utilities::get31TileNumberX(30.70));
    if (!data->loadBinaryMapObjects(
            &objects, nullptr, nullptr, OsmAnd::ZoomLevel16, &kyiv))
        return 4;

    int highways = 0;
    int buildings = 0;
    for (const auto& object : objects) {
        if (object->containsTag(QStringLiteral("highway"), true))
            ++highways;
        if (object->containsTag(QStringLiteral("building"), true))
            ++buildings;
    }
    std::printf("osmand-kyiv-obf:ok objects=%d highways=%d buildings=%d\n",
                objects.size(), highways, buildings);

    // The vector map reads one 2^17-unit block per zoom level, with the bounding box and the
    // zoom range given to obtainDataInterface; print what each level returns for central Kyiv.
    const OsmAnd::PointI centre = OsmAnd::Utilities::convertLatLonTo31(OsmAnd::LatLon(50.4501, 30.5234));
    const OsmAnd::AreaI block(centre.y - 65536, centre.x - 65536, centre.y + 65536, centre.x + 65536);
    for (int level = 9; level <= 17; ++level) {
        const auto zoom = static_cast<OsmAnd::ZoomLevel>(level);
        QList<std::shared_ptr<const OsmAnd::BinaryMapObject>> found;
        OsmAnd::MapSurfaceType surface = OsmAnd::MapSurfaceType::Undefined;
        const auto filtered = collection.obtainDataInterface(
            &block, zoom, zoom, OsmAnd::ObfDataTypesMask().set(OsmAnd::ObfDataType::Map));
        const bool loaded = filtered &&
            filtered->loadBinaryMapObjects(&found, &surface, nullptr, zoom, &block);
        std::printf("level %2d: interface=%d loaded=%d objects=%d\n", level, filtered ? 1 : 0,
                    loaded ? 1 : 0, found.size());
    }
    return objects.isEmpty() ? 5 : 0;
}
