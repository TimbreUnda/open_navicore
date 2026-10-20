#pragma once

#include <memory>
#include <string>
#include <vector>

// Core and core-legacy both ship a Logging.h with the same public names but
// different include guards. The provider uses Core's logger consistently.
#include <OsmAndCore/Logging.h>
#define _OSMAND_LOGGING_H_
#include "binaryRead.h"
#include "commonOsmAndCore.h"
#include "routePlannerFrontEnd.h"
#include "routeSegmentResult.h"
#include "routingContext.h"

namespace navicore_osmand {

// Routes with a contraction hierarchy built offline from the same OBF maps (a
// "sidecar", tools/ch) and hands the result to the core's own result
// preparation, so turn instructions, street names and attached roads are
// exactly what the A* and HH routers give. The search itself takes well under
// a millisecond; what remains is loading the route's roads from the maps.
class ChPlanner {
public:
    using Segments = std::vector<SHARED_PTR<RouteSegmentResult>>;

    struct Timing {
        double search_ms = 0;    // snap start/end, hierarchy query, unpack
        double roads_ms = 0;     // load the route's roads from the maps
        double prepare_ms = 0;   // prepareResult + makeStartEndPointsPrecise
        std::size_t road_runs = 0;
        std::size_t base_edges = 0;
        std::size_t settled = 0;
    };

    // Maps the sidecar and checks it was built from exactly these map files
    // (same names and sizes, whatever the order). On failure returns null and
    // says why in `error`; the caller then routes without it.
    static std::unique_ptr<ChPlanner> open(const std::string& sidecar_path,
                                           const std::vector<std::string>& routable_maps,
                                           std::string& error);
    ~ChPlanner();

    // The fastest route between two points (31-bit tile numbers), or empty
    // when the sidecar cannot serve it (a point too far from every road,
    // start and end on one road node, a road missing from the loaded maps);
    // `why` then names the reason and the caller falls back to HH or A*.
    Segments route(RoutePlannerFrontEnd& front_end, const SHARED_PTR<RoutingContext>& context,
                   int start_x, int start_y, int end_x, int end_y, std::string& why);

    const Timing& last_timing() const { return timing_; }
    // One-line description for the log: nodes, edges, maps.
    std::string describe() const;

private:
    class Impl;
    explicit ChPlanner(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    Timing timing_;
};

} // namespace navicore_osmand
