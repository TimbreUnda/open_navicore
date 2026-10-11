#include <OsmAndCore.h>
#include <OsmAndCore/CoreResourcesEmbeddedBundle.h>
#include <OsmAndCore/Logging.h>

#define _OSMAND_LOGGING_H_
#include "binaryRead.h"
#include "commonOsmAndCore.h"
#include "routePlannerFrontEnd.h"
#include "routeSegmentResult.h"
#include "routingConfiguration.h"

#include <QByteArray>
#include <QString>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

bool write_all(int fd, const QByteArray& bytes)
{
    int offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::write(fd, bytes.constData() + offset,
                                      static_cast<std::size_t>(bytes.size() - offset));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += static_cast<int>(count);
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::fprintf(stderr, "usage: qnx-osmand-route-smoke RESOURCES.so MAP.obf FONTS\n");
        return 2;
    }
    const auto resources = OsmAnd::CoreResourcesEmbeddedBundle::loadFromLibrary(
        QString::fromLocal8Bit(argv[1]));
    if (!resources) return 3;
    if (OsmAnd::InitializeCore(resources, argv[3]) == 0) return 4;

    bool resource_ok = false;
    const QByteArray xml = resources->getResource(
        QStringLiteral("routing/routing.xml"), &resource_ok);
    if (!resource_ok || xml.isEmpty()) return 5;
    char path[] = "/tmp/open-navicore-route-smoke-XXXXXX";
    const int fd = ::mkstemp(path);
    if (fd < 0 || !write_all(fd, xml) || ::close(fd) != 0) return 6;
    // The second argument namespaces add-on routing files.  The primary
    // bundled configuration must stay in the root namespace.
    const auto builder = parseRoutingConfigurationFromXml(path, "");
    (void)::unlink(path);
    if (!builder) return 7;
    if (builder->routers.find("car") == builder->routers.end()) return 12;
    MAP_STR_STR parameters;
    const auto config = builder->build("car", 64, parameters);
    if (!config || !config->router) return 8;
    const auto map = initBinaryMapFile(argv[2], false, true);
    if (!map) return 9;
    std::printf("osmand-route:indexes routing=%u map=%u\n",
                static_cast<unsigned int>(map->routingIndexes.size()),
                static_cast<unsigned int>(map->mapIndexes.size()));
    std::fflush(stdout);
    if (map->routingIndexes.empty()) {
        (void)closeBinaryMapFile(argv[2]);
        OsmAnd::ReleaseCore();
        return 11;
    }

    RoutePlannerFrontEnd front_end;
    const auto context = front_end.buildRoutingContext(config);
    std::vector<int> intermediates_x;
    std::vector<int> intermediates_y;
    const auto route = front_end.searchRoute(
        context,
        get31TileNumberX(30.5234), get31TileNumberY(50.4501),
        get31TileNumberX(30.4710), get31TileNumberY(50.4547),
        intermediates_x, intermediates_y, nullptr);
    double metres = 0.0;
    double seconds = 0.0;
    std::size_t points = 0;
    for (const auto& segment : route) {
        if (!segment) continue;
        metres += segment->distance;
        seconds += segment->segmentTime;
        points += static_cast<std::size_t>(
            std::abs(segment->getEndPointIndex() - segment->getStartPointIndex()) + 1);
    }
    (void)closeBinaryMapFile(argv[2]);
    OsmAnd::ReleaseCore();
    std::printf("osmand-route:ok segments=%u points=%u metres=%.0f seconds=%.0f\n",
                static_cast<unsigned int>(route.size()),
                static_cast<unsigned int>(points), metres, seconds);
    return route.empty() || points < 2 ? 10 : 0;
}
