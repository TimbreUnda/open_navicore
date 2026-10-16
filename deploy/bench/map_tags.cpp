// Map data survey for the radio (bench only, not in navigation.ifs).
//
//   route_bench_run map-tags MAP ZOOM LAT_TOP LON_LEFT LAT_BOTTOM LON_RIGHT [KEY...]
//
// Loads the map objects the vector map would read at detail ZOOM inside the
// box and prints how often each tag=value of KEY (all keys when none given)
// appears on points, lines and areas.

#include <OsmAndCore/Data/BinaryMapObject.h>
#include <OsmAndCore/ObfDataInterface.h>
#include <OsmAndCore/ObfsCollection.h>
#include <OsmAndCore/Utilities.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

extern "C" __attribute__((visibility("default")))
int map_tags_main(int argc, char** argv)
{
    if (argc < 7) {
        std::fprintf(stderr, "usage: map-tags MAP ZOOM LAT_TOP LON_LEFT LAT_BOTTOM LON_RIGHT [KEY...]\n");
        return 2;
    }
    const int zoom = std::atoi(argv[2]);
    std::set<std::string> keys;
    for (int i = 7; i < argc; ++i)
        keys.insert(argv[i]);

    OsmAnd::ObfsCollection collection;
    collection.addFile(QString::fromLocal8Bit(argv[1]));
    const auto data = collection.obtainDataInterface();
    if (!data)
        return 3;
    const OsmAnd::AreaI box(
        OsmAnd::Utilities::get31TileNumberY(std::atof(argv[3])),
        OsmAnd::Utilities::get31TileNumberX(std::atof(argv[4])),
        OsmAnd::Utilities::get31TileNumberY(std::atof(argv[5])),
        OsmAnd::Utilities::get31TileNumberX(std::atof(argv[6])));
    QList<std::shared_ptr<const OsmAnd::BinaryMapObject>> objects;
    if (!data->loadBinaryMapObjects(&objects, nullptr, nullptr,
                                    static_cast<OsmAnd::ZoomLevel>(zoom), &box))
        return 4;

    // tag=value -> counts for point, line, area.
    std::map<std::string, std::array<int, 3>> counts;
    int kinds[3] = {};
    for (const auto& object : objects) {
        const int kind = object->points31.size() == 1 ? 0 : object->isArea ? 2 : 1;
        ++kinds[kind];
        for (const bool additional : {false, true}) {
            const auto& ids = additional ? object->additionalAttributeIds : object->attributeIds;
            for (int i = 0; i < ids.size(); ++i) {
                const auto* attribute =
                    object->resolveAttributeByIndex(static_cast<std::uint32_t>(i), additional);
                if (!attribute)
                    continue;
                const std::string tag = attribute->tag.toStdString();
                if (!keys.empty() && !keys.count(tag))
                    continue;
                ++counts[tag + "=" + attribute->value.toStdString() +
                         (additional ? " (+)" : "")][kind];
            }
        }
    }
    std::printf("zoom=%d objects=%d points=%d lines=%d areas=%d\n", zoom, objects.size(),
                kinds[0], kinds[1], kinds[2]);
    std::vector<std::pair<std::string, std::array<int, 3>>> rows(counts.begin(), counts.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.second[0] + a.second[1] + a.second[2] > b.second[0] + b.second[1] + b.second[2];
    });
    std::printf("%-48s %7s %7s %7s\n", "tag", "points", "lines", "areas");
    for (const auto& row : rows)
        std::printf("%-48s %7d %7d %7d\n", row.first.c_str(), row.second[0], row.second[1],
                    row.second[2]);
    return 0;
}
