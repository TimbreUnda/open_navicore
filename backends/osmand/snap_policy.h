#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace navicore_osmand {

// Resolve the endpoint before optimizing travel time. Only geometrically tied
// roads may compete (junctions and overlapping map ways); a cheaper journey
// must not move a pin to another street. One metre allows map quantization.
template<class Snap>
void keep_nearest_roads(std::vector<Snap>& snaps)
{
    snaps.erase(std::remove_if(snaps.begin(), snaps.end(), [](const Snap& s) {
        return !std::isfinite(s.metres) || s.metres < 0 || s.metres > 1000;
    }), snaps.end());
    if (snaps.empty()) return;
    const double nearest = std::min_element(snaps.begin(), snaps.end(),
        [](const Snap& a, const Snap& b) { return a.metres < b.metres; })->metres;
    snaps.erase(std::remove_if(snaps.begin(), snaps.end(), [nearest](const Snap& s) {
        return s.metres > nearest + 1.0;
    }), snaps.end());
}

} // namespace navicore_osmand
