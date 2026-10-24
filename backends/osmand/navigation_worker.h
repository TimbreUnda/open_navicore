#pragma once

#include "ui/navigation_model.h"

#include <cstdint>
#include <memory>
#include <optional>
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

    // Address of a map point (dropped pin): "street, number"; empty when
    // none was found.
    struct AddressResult {
        std::uint64_t revision = 0;
        std::string address;
    };

    // search_index: the address index file (search/search_index.h); when it
    // is missing or unreadable, search falls back to OsmAnd's name search.
    NavigationWorker(std::shared_ptr<OsmAnd::ObfsCollection> obfs,
                     std::shared_ptr<Wakeup> wakeup,
                     std::vector<std::string> map_paths,
                     std::string routing_xml,
                     std::string search_index,
                     std::string ch_sidecar = std::string());
    ~NavigationWorker();
    NavigationWorker(const NavigationWorker&) = delete;
    NavigationWorker& operator=(const NavigationWorker&) = delete;

    // origin: where "near" is (the car, or the map centre without a fix);
    // recent: destinations picked before, which rank higher.
    void submit_search(std::uint64_t revision, std::string query, ui::GeoPoint origin,
                       std::vector<ui::GeoPoint> recent);
    // Searches take milliseconds with the index, seconds without.
    bool has_search_index() const;
    // alternatives: also offer up to two other routes (route preview; a
    // reroute during guidance takes the fastest only). heading_degrees: the
    // car's driving direction (clockwise from north) when known; the route
    // then prefers to start that way instead of turning back.
    void submit_route(std::uint64_t revision, ui::Destination destination,
                      ui::GeoPoint origin, bool alternatives,
                      std::optional<double> heading_degrees);
    // Replaces a pending request like the others (one job at a time).
    void submit_address(std::uint64_t revision, ui::GeoPoint point);
    bool take_search_result(SearchResult& output);
    bool take_route_result(RouteResult& output);
    bool take_address_result(AddressResult& output);
    void stop() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace navicore_osmand
