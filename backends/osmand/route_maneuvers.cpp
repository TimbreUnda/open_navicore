#include "route_maneuvers.h"

#include "junction_shape.h"

#include <algorithm>
#include <cmath>

namespace navicore_osmand {

ui::ManeuverKind maneuver_kind(int value)
{
    switch (value) {
    case TurnType::TL: return ui::ManeuverKind::Left;
    case TurnType::TSLL: return ui::ManeuverKind::SlightLeft;
    case TurnType::TSHL: return ui::ManeuverKind::SharpLeft;
    case TurnType::TR: return ui::ManeuverKind::Right;
    case TurnType::TSLR: return ui::ManeuverKind::SlightRight;
    case TurnType::TSHR: return ui::ManeuverKind::SharpRight;
    case TurnType::KL: return ui::ManeuverKind::KeepLeft;
    case TurnType::KR: return ui::ManeuverKind::KeepRight;
    case TurnType::TU:
    case TurnType::TRU: return ui::ManeuverKind::UTurn;
    case TurnType::RNDB:
    case TurnType::RNLB: return ui::ManeuverKind::Roundabout;
    default: return ui::ManeuverKind::Straight;
    }
}

// OsmAnd lane value: bit 0 = the route uses the lane, bits 1-4 its main
// turn, bits 5-9 a second turn (TurnType::getPrimaryTurn/getSecondaryTurn).
std::vector<ui::Lane> lanes_of(const SHARED_PTR<TurnType>& turn)
{
    std::vector<ui::Lane> lanes;
    if (!turn)
        return lanes;
    for (const int value : turn->getLanes()) {
        ui::Lane lane;
        const int primary = TurnType::getPrimaryTurn(value);
        const int secondary = TurnType::getSecondaryTurn(value);
        lane.kind = maneuver_kind(primary ? primary : TurnType::C);
        lane.combined = secondary != 0;
        if (lane.combined)
            lane.also = maneuver_kind(secondary);
        lane.recommended = (value & 1) != 0;
        lanes.push_back(lane);
    }
    return lanes;
}

// Bearings over the first 10 m of each road, as OsmAnd's own turn decision
// (getTurnInfo, DIST_BEARING_DETECT).
constexpr float kJunctionBearingMetres = 10.0f;

// The junction at the start of segments[index] (where OsmAnd puts the
// turn): the route's and every other road's direction, relative to the
// incoming direction (positive clockwise). Other roads: the segment's
// attached roads at its start point (prepareResult /
// attachRoadSegments: the incoming road's continuation and each road the
// router could leave the node by; one-way roads that only enter the node
// are not attached), plus the route's own road behind the node, which
// OsmAnd does not attach (same way id).
ui::JunctionShape junction_of(Segments& segments, std::size_t index)
{
    if (index == 0 || index >= segments.size()) return {};
    const auto& previous = segments[index - 1];
    const auto& current = segments[index];
    if (!previous || !current || !previous->object || !current->object) return {};
    const double incoming =
        previous->getBearingEnd(previous->getEndPointIndex(), kJunctionBearingMetres);
    const int start = current->getStartPointIndex();
    const double route = current->getBearingBegin(start, kJunctionBearingMetres);
    std::vector<double> arms;
    for (const auto& attached : current->getAttachedRoutes(start)) {
        if (!attached || !attached->object) continue;
        arms.push_back(degreesDiff(attached->getBearingBegin(), incoming));
    }
    const bool forward = current->getEndPointIndex() > start;
    const int points = static_cast<int>(current->object->getPointsLength());
    if (current->object->getId() != previous->object->getId() &&
        (forward ? start > 0 : start < points - 1)) {
        const double behind =
            current->object->directionRoute(start, !forward, kJunctionBearingMetres) / M_PI *
            180.0;
        arms.push_back(degreesDiff(behind, incoming));
    }
    return make_junction_shape(degreesDiff(route, incoming), arms);
}

bool maneuver_at(Segments& segments, std::size_t index, ui::Maneuver& maneuver)
{
    const auto& segment = segments[index];
    if (!segment || !segment->turnType || segment->turnType->getValue() == TurnType::C)
        return false;
    maneuver.kind = maneuver_kind(segment->turnType->getValue());
    maneuver.roundabout_exit = segment->turnType->getExitOut();
    maneuver.lanes = lanes_of(segment->turnType);
    maneuver.left_side = segment->turnType->isLeftSide();
    maneuver.turn_angle_degrees = segment->turnType->getTurnAngle();
    if (!segment->turnType->isRoundAbout()) {
        maneuver.junction = junction_of(segments, index);
        summarize_lanes(maneuver.lanes, maneuver.junction);
    }
    return true;
}

std::vector<std::size_t> segment_start_points(const Segments& segments)
{
    std::vector<std::size_t> starts(segments.size(), 0);
    std::size_t points = 0;
    int last_x = 0, last_y = 0;
    for (std::size_t s = 0; s < segments.size(); ++s) {
        const auto& segment = segments[s];
        if (!segment || !segment->object) {
            starts[s] = points ? points - 1 : 0;
            continue;
        }
        const int start = segment->getStartPointIndex();
        const int end = segment->getEndPointIndex();
        const int step = start <= end ? 1 : -1;
        for (int i = start;; i += step) {
            const int x = static_cast<int>(segment->object->pointsX[i]);
            const int y = static_cast<int>(segment->object->pointsY[i]);
            const bool same = points > 0 && x == last_x && y == last_y;
            if (i == start) starts[s] = same ? points - 1 : points;
            if (!same) {
                ++points;
                last_x = x;
                last_y = y;
            }
            if (i == end) break;
        }
    }
    return starts;
}

ui::Maneuver maneuver_from(const omaps::Turn& turn)
{
    ui::Maneuver maneuver;
    maneuver.kind = turn.kind;
    maneuver.road = turn.road;
    maneuver.lanes = turn.lanes;
    maneuver.left_side = turn.left_side;
    maneuver.turn_angle_degrees = turn.route_angle;
    if (turn.kind != ui::ManeuverKind::Roundabout) {
        maneuver.junction = make_junction_shape(turn.route_angle, turn.other_roads);
        summarize_lanes(maneuver.lanes, maneuver.junction);
    }
    return maneuver;
}

std::vector<PlacedManeuver> route_maneuvers(Segments& segments, const omaps::Result* turns)
{
    const std::vector<std::size_t> starts = segment_start_points(segments);
    std::vector<PlacedManeuver> placed;
    if (turns && !turns->ok) return placed;  // no silent fallback to OsmAnd's turns
    const bool organic = turns != nullptr;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        PlacedManeuver p;
        if (!maneuver_at(segments, i, p.maneuver)) continue;
        if (organic && p.maneuver.kind != ui::ManeuverKind::Roundabout) continue;
        p.segment = i;
        p.route_point = starts[i];
        placed.push_back(std::move(p));
    }
    if (!organic) return placed;
    for (const auto& turn : turns->turns) {
        if (turn.roundabout) continue;  // OsmAnd's roundabout, above
        PlacedManeuver p;
        p.segment = turn.segment;
        p.route_point = turn.route_point;
        p.maneuver = maneuver_from(turn);
        placed.push_back(std::move(p));
    }
    std::stable_sort(placed.begin(), placed.end(), [](const PlacedManeuver& a, const PlacedManeuver& b) {
        return a.route_point < b.route_point;
    });
    return placed;
}

} // namespace navicore_osmand
