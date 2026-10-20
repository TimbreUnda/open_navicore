#pragma once

#include "ui/navigation_model.h"

#include <cstdint>
#include <vector>

namespace navicore_osmand {

// Junction-shape classification for the instrument-cluster pictures
// (include/open_navicore/cluster.h, analysis/Cluster_TBT_Symbols.md). Pure:
// no OsmAnd types; navigation_worker.cpp fills ui::JunctionShape from the
// router's attached roads.
//
// Angle bands (|angle| from straight on, degrees). Arms in a gap between
// two bands are ambiguous: the classifier then falls back to the turn-type
// picture.
//   straight / through road  0..25
//   ahead diagonal          30..62
//   perpendicular           68..112
//   behind diagonal        118..160
//   above 160: the incoming road's other carriageway or a road doubling
//   back along it; ignored.
namespace junction {
constexpr double kThrough = 25.0;
constexpr double kDiagonalMin = 30.0;
constexpr double kDiagonalMax = 62.0;
constexpr double kPerpendicularMin = 68.0;
constexpr double kPerpendicularMax = 112.0;
constexpr double kBehindMin = 118.0;
constexpr double kBehindMax = 160.0;
// The route as a slight turn / branch, and as a sharp turn.
constexpr double kRouteSlightMax = 75.0;
constexpr double kRouteSharpMin = 115.0;
// A three-way split whose side roads all leave within this angle is drawn
// as the gradual split '\|/' (53..55), a wider one as the fan '/|\' (44..46).
constexpr double kNarrowSplit = 40.0;
// Attached roads within this angle of the route or of another arm are the
// same arm (overlapping ways, a split carriageway node).
constexpr double kSameArm = 8.0;
} // namespace junction

// The junction description from the route angle and the angles of every
// other road leaving the node (relative to the incoming direction,
// positive clockwise; any range, normalised here). Non-finite angles are
// skipped; a non-finite route angle gives an invalid shape.
ui::JunctionShape make_junction_shape(double route_angle, const std::vector<double>& arms);

// Fills the lane fields of `shape` from the manoeuvre's lanes (left to right).
void summarize_lanes(const std::vector<ui::Lane>& lanes, ui::JunctionShape& shape) noexcept;

// The junction picture for a manoeuvre (cluster symbol), or 0 when no
// junction picture applies: no or ambiguous geometry, a geometry that does
// not agree with OsmAnd's turn type, roundabouts, U-turns and arrival. The
// caller then uses the turn-type picture.
uint8_t junction_symbol(const ui::Maneuver& maneuver) noexcept;

} // namespace navicore_osmand
