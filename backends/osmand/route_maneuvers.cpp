#include "route_maneuvers.h"

#include "junction_shape.h"

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

} // namespace navicore_osmand
