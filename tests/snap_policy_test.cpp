#include "snap_policy.h"
#include <cassert>
#include <limits>

struct Snap { int road; double metres; };
int main()
{
    // Real Cherkasy failure: the 194 m road must not beat the requested road
    // even when its driving cost is lower. Input priority order is irrelevant.
    std::vector<Snap> roads{{6,194}, {2,107}, {1,0}, {3,112}};
    navicore_osmand::keep_nearest_roads(roads);
    assert(roads.size() == 1 && roads[0].road == 1);
    // Shared junctions remain available in either legal travel direction.
    roads = {{1,0.02}, {2,0.03}, {3,5.7}};
    navicore_osmand::keep_nearest_roads(roads);
    assert(roads.size() == 2);
    // An off-road pin uses the closest road, without a larger search radius.
    roads = {{1,55}, {2,15}, {3,15.5}};
    navicore_osmand::keep_nearest_roads(roads);
    assert(roads.size() == 2 && roads[0].road == 2);
    roads = {{1,1001}, {2,std::numeric_limits<double>::quiet_NaN()}};
    navicore_osmand::keep_nearest_roads(roads);
    assert(roads.empty());
}
