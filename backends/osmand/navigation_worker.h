#pragma once

#include "ui/navigation_model.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace OsmAnd {
class ObfsCollection;
}

namespace navicore_osmand {

class Wakeup;

// Owns all blocking OBF search work. The renderer thread only submits copied
// requests and drains copied DTOs; it never waits for an index scan.
class NavigationWorker {
public:
    struct SearchResult {
        std::uint64_t revision = 0;
        std::vector<ui::Destination> destinations;
    };

    struct RouteResult {
        std::uint64_t revision = 0;
        std::vector<ui::RouteAlternative> routes;
    };

    NavigationWorker(std::shared_ptr<OsmAnd::ObfsCollection> obfs,
                     std::shared_ptr<Wakeup> wakeup,
                     std::string map_path,
                     std::string routing_xml);
    ~NavigationWorker();
    NavigationWorker(const NavigationWorker&) = delete;
    NavigationWorker& operator=(const NavigationWorker&) = delete;

    void submit_search(std::uint64_t revision, std::string query);
    void submit_route(std::uint64_t revision, ui::Destination destination);
    bool take_search_result(SearchResult& output);
    bool take_route_result(RouteResult& output);
    void stop() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace navicore_osmand
