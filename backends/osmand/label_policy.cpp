#include "label_policy.h"

#include <algorithm>

namespace navicore_osmand {
namespace {
int street_rank(const std::string& highway)
{
    struct Entry { const char* value; int rank; };
    static const Entry ranks[] = {
        {"motorway", 0}, {"motorway_link", 1},
        {"trunk", 2}, {"trunk_link", 3},
        {"primary", 4}, {"primary_link", 5},
        {"secondary", 6}, {"secondary_link", 7},
        {"tertiary", 8}, {"tertiary_link", 9},
        {"unclassified", 10}, {"residential", 11}, {"living_street", 12},
    };
    for (const auto& entry : ranks)
        if (highway == entry.value)
            return entry.rank;
    return -1;   // footway, path, cycleway, service, track, steps, ...
}

int place_rank(const std::string& place)
{
    if (place == "city") return 0;
    if (place == "town") return 1;
    if (place == "suburb" || place == "village") return 2;
    if (place == "quarter" || place == "neighbourhood") return 3;
    return -1;
}

int driver_poi_rank(const std::string& amenity)
{
    if (amenity == "fuel") return 0;
    if (amenity == "charging_station") return 1;
    if (amenity == "parking") return 2;
    if (amenity == "hospital") return 3;
    if (amenity == "police") return 4;
    return -1;
}

int class_order(LabelClass label_class)
{
    switch (label_class) {
    case LabelClass::Shield: return 0;
    case LabelClass::Street: return 1;
    case LabelClass::Place: return 2;
    case LabelClass::DriverPoi: return 3;
    case LabelClass::Drop: break;
    }
    return 4;
}
} // namespace

LabelDecision classify_label(const LabelTags& tags)
{
    if (!tags.highway.empty()) {
        const int rank = street_rank(tags.highway);
        if (rank < 0)
            return {};
        if (tags.on_path)
            return {LabelClass::Street, rank};
        // Non-path road text is a route reference shield; road icons
        // (e.g. traffic signals) are clutter.
        return tags.icon ? LabelDecision{} : LabelDecision{LabelClass::Shield, rank};
    }
    if (!tags.place.empty()) {
        const int rank = place_rank(tags.place);
        return rank < 0 || tags.icon ? LabelDecision{}
                                     : LabelDecision{LabelClass::Place, rank};
    }
    if (!tags.amenity.empty() && tags.icon) {
        const int rank = driver_poi_rank(tags.amenity);
        if (rank >= 0)
            return {LabelClass::DriverPoi, rank};
    }
    return {};
}

std::vector<std::size_t> select_labels(
    const std::vector<LabelCandidate>& candidates, const LabelBudget& budget)
{
    std::vector<std::size_t> order;
    order.reserve(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i)
        if (candidates[i].decision.label_class != LabelClass::Drop)
            order.push_back(i);
    std::stable_sort(order.begin(), order.end(),
        [&](std::size_t a, std::size_t b) {
            const auto& left = candidates[a];
            const auto& right = candidates[b];
            const int left_class = class_order(left.decision.label_class);
            const int right_class = class_order(right.decision.label_class);
            if (left_class != right_class) return left_class < right_class;
            if (left.decision.rank != right.decision.rank)
                return left.decision.rank < right.decision.rank;
            return left.order < right.order;
        });
    std::size_t used[5] = {};
    const std::size_t limits[5] = {budget.shields, budget.streets,
                                   budget.places, budget.driver_pois, 0};
    std::vector<std::size_t> selected;
    for (const std::size_t index : order) {
        const int slot = class_order(candidates[index].decision.label_class);
        if (used[slot] < limits[slot]) {
            ++used[slot];
            selected.push_back(index);
        }
    }
    return selected;
}

} // namespace navicore_osmand
