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
    using navicore_osmand::check_route_extent;
    // A closed loop can have identical endpoints but nonzero travelled length.
    assert(check_route_extent({50,30}, {50,30}, {50,30}, {50,30}, 300).usable);
    assert(!check_route_extent({50,30}, {50,30.001}, {50,30}, {50,30}, 0).usable);
    // HH's former apparent success at an off-road destination must be rejected.
    assert(!check_route_extent({50,30}, {46,31}, {50,30}, {46.22,31}, 525000).usable);
    // Being inside the global snap radius does not permit HH to skip an
    // isolated endpoint road: this point was on a road, not 98 m from one.
    assert(!check_route_extent({50,30}, {50.01,30}, {50.00088,30}, {50.01,30}, 1200, 3, 3).usable);
    assert(check_route_extent({50,30}, {50.01,30}, {50,30}, {50.01,30}, 1113).usable);
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
