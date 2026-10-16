#include "route_check.h"

#include <cassert>

using navicore_osmand::RouteCheck;
using navicore_osmand::RouteLeg;
using navicore_osmand::check_route_legs;

namespace {
RouteLeg leg(long long road, double lat0, double lon0, double lat1, double lon1)
{
    return {road, lat0, lon0, lat1, lon1};
}
} // namespace

int main()
{
    // An empty route is not usable.
    assert(!check_route_legs({}).ok());

    // Connected map roads pass.
    {
        const RouteCheck check = check_route_legs({leg(10, 50.45, 30.52, 50.46, 30.53),
                                                   leg(11, 50.46, 30.53, 50.47, 30.55)});
        assert(check.ok());
        assert(check.gaps == 0 && check.filler_legs == 0);
    }
    // A leg without a map road is the straight-line filler: rejected.
    {
        const RouteCheck check = check_route_legs({leg(10, 50.45, 30.52, 50.46, 30.53),
                                                   leg(0, 50.46, 30.53, 50.60, 30.80),
                                                   leg(11, 50.60, 30.80, 50.61, 30.81)});
        assert(!check.ok());
        assert(check.filler_legs == 1);
    }
    // A jump between legs (about 11 km) is rejected; a few metres is not.
    {
        const RouteCheck jump = check_route_legs({leg(10, 50.45, 30.52, 50.46, 30.53),
                                                  leg(11, 50.56, 30.53, 50.57, 30.55)});
        assert(!jump.ok() && jump.gaps == 1 && jump.max_gap_metres > 10000.0);
        const RouteCheck near = check_route_legs({leg(10, 50.45, 30.52, 50.46, 30.53),
                                                  leg(11, 50.46002, 30.53, 50.47, 30.55)});
        assert(near.ok());
    }
    return 0;
}
