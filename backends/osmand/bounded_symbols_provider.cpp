#include "bounded_symbols_provider.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace navicore_osmand {
namespace {

int group_order(const std::shared_ptr<OsmAnd::MapSymbolsGroup>& group)
{
    int order = std::numeric_limits<int>::max();
    if (!group)
        return order;
    for (const auto& symbol : group->symbols) {
        if (symbol && !symbol->isHidden)
            order = std::min(order, symbol->order);
    }
    return order;
}

} // namespace

BoundedSymbolsProvider::BoundedSymbolsProvider(
    const std::shared_ptr<OsmAnd::MapPrimitivesProvider>& primitives,
    std::size_t maximum_groups_per_tile)
    : OsmAnd::MapObjectsSymbolsProvider(primitives, 256u)
    , maximum_groups_per_tile_(maximum_groups_per_tile)
{
}

bool BoundedSymbolsProvider::obtainData(
    const OsmAnd::IMapDataProvider::Request& request,
    std::shared_ptr<OsmAnd::IMapDataProvider::Data>& output,
    std::shared_ptr<OsmAnd::Metric>* metric)
{
    if (!OsmAnd::MapObjectsSymbolsProvider::obtainData(
            request, output, metric))
        return false;
    const auto data = std::dynamic_pointer_cast<
        OsmAnd::MapObjectsSymbolsProvider::Data>(output);
    if (!data || maximum_groups_per_tile_ == 0u ||
        static_cast<std::size_t>(data->symbolsGroups.size()) <=
            maximum_groups_per_tile_)
        return true;

    std::vector<std::shared_ptr<OsmAnd::MapSymbolsGroup>> groups;
    groups.reserve(static_cast<std::size_t>(data->symbolsGroups.size()));
    for (const auto& group : data->symbolsGroups)
        groups.push_back(group);
    std::stable_sort(groups.begin(), groups.end(),
        [](const auto& left, const auto& right) {
            return group_order(left) < group_order(right);
        });

    data->symbolsGroups.clear();
    for (std::size_t index = 0u;
         index < maximum_groups_per_tile_ && index < groups.size(); ++index) {
        if (groups[index])
            data->symbolsGroups.push_back(groups[index]);
    }
    return true;
}

} // namespace navicore_osmand
