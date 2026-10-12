#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace navicore_osmand {

// Car-only label policy. Pure (no OsmAnd types) so it is host-testable; the
// symbol worker fills LabelTags from the source map object.
enum class LabelClass { Drop, Street, Shield, Place, DriverPoi };

struct LabelTags {
    std::string highway;
    std::string place;
    std::string amenity;
    bool on_path = false;   // OsmAnd on-path text (a street name)
    bool icon = false;      // icon content rather than caption text
};

struct LabelDecision {
    LabelClass label_class = LabelClass::Drop;
    int rank = 0;           // lower = more important within the class
};

// Streets: names along roads a car uses, ranked by road class; footways,
// paths, cycleways, service roads and steps are dropped. Shields: non-path
// symbols of drivable roads (route refs). Places: city..neighbourhood.
// Driver POIs: icons only, for fuel, charging, parking, hospital, police.
// Everything else (churches, libraries, shops, buildings...) is dropped.
LabelDecision classify_label(const LabelTags& tags);

struct LabelBudget {
    std::size_t streets = 12;
    std::size_t shields = 4;
    std::size_t places = 3;
    std::size_t driver_pois = 8;
};

struct LabelCandidate {
    LabelDecision decision;
    int order = 0;          // OsmAnd's own priority, tie-breaker
};

// Indices of the candidates to keep: per-class budgets, best rank first,
// then OsmAnd order, then input order. Result is in priority order
// (shields, streets, places, driver POIs) so atlas space goes to them first.
std::vector<std::size_t> select_labels(
    const std::vector<LabelCandidate>& candidates, const LabelBudget& budget);

} // namespace navicore_osmand
