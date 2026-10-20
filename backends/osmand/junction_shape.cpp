#include "junction_shape.h"

#include <open_navicore/cluster.h>

#include <cmath>

namespace navicore_osmand {

namespace {

using namespace junction;

double normalise(double angle) noexcept
{
    angle = std::fmod(angle, 360.0);
    if (angle > 180.0) angle -= 360.0;
    if (angle <= -180.0) angle += 360.0;
    return angle;
}

double difference(double a, double b) noexcept
{
    return std::fabs(normalise(a - b));
}

enum class Band { Through, Diagonal, Perpendicular, Behind, Reverse, Gap };

Band band_of(double angle) noexcept
{
    const double a = std::fabs(angle);
    if (a <= kThrough) return Band::Through;
    if (a < kDiagonalMin) return Band::Gap;
    if (a <= kDiagonalMax) return Band::Diagonal;
    if (a < kPerpendicularMin) return Band::Gap;
    if (a <= kPerpendicularMax) return Band::Perpendicular;
    if (a < kBehindMin) return Band::Gap;
    if (a <= kBehindMax) return Band::Behind;
    return Band::Reverse;
}

constexpr int kLeft = 0;
constexpr int kRight = 1;

// The side arms by band: [kLeft] and [kRight].
struct Arms {
    bool ambiguous = false;
    bool through = false;
    bool diagonal[2] = {};
    bool perpendicular[2] = {};
    bool behind[2] = {};
    double diagonal_angle[2] = {};  // |angle| of the widest ahead diagonal

    bool any(int side) const noexcept
    {
        return diagonal[side] || perpendicular[side] || behind[side];
    }
    bool only_diagonal(int side) const noexcept
    {
        return diagonal[side] && !perpendicular[side] && !behind[side];
    }
    bool only_perpendicular(int side) const noexcept
    {
        return perpendicular[side] && !diagonal[side] && !behind[side];
    }
    bool only_behind(int side) const noexcept
    {
        return behind[side] && !diagonal[side] && !perpendicular[side];
    }
};

Arms arms_of(const ui::JunctionShape& j) noexcept
{
    Arms arms;
    arms.ambiguous = j.truncated;
    const std::size_t count =
        j.branch_count < ui::JunctionShape::kMaxBranches ? j.branch_count
                                                         : ui::JunctionShape::kMaxBranches;
    for (std::size_t i = 0; i < count; ++i) {
        const double b = j.branches[i];
        const int side = b < 0.0 ? kLeft : kRight;
        switch (band_of(b)) {
        case Band::Through: arms.through = true; break;
        case Band::Diagonal:
            arms.diagonal[side] = true;
            if (std::fabs(b) > arms.diagonal_angle[side])
                arms.diagonal_angle[side] = std::fabs(b);
            break;
        case Band::Perpendicular: arms.perpendicular[side] = true; break;
        case Band::Behind: arms.behind[side] = true; break;
        case Band::Reverse: break;
        case Band::Gap: arms.ambiguous = true; break;
        }
    }
    return arms;
}

uint8_t pick(int side, uint8_t left, uint8_t right) noexcept
{
    return side == kLeft ? left : right;
}

// A three-way split; route_side -1 = the route is the middle road.
uint8_t three_way(int route_side, bool narrow) noexcept
{
    if (route_side < 0)
        return narrow ? OPEN_NAVI_CLUSTER_SYMBOL_SPLIT_STRAIGHT : OPEN_NAVI_CLUSTER_SYMBOL_FAN_STRAIGHT;
    if (narrow)
        return pick(route_side, OPEN_NAVI_CLUSTER_SYMBOL_SPLIT_LEFT,
                    OPEN_NAVI_CLUSTER_SYMBOL_SPLIT_RIGHT);
    return pick(route_side, OPEN_NAVI_CLUSTER_SYMBOL_FAN_LEFT, OPEN_NAVI_CLUSTER_SYMBOL_FAN_RIGHT);
}

// The route turns 90 degrees-ish to `side` (TL/TR).
uint8_t turn(const Arms& a, int side) noexcept
{
    const int other = 1 - side;
    if (a.through && a.any(other))
        return pick(side, OPEN_NAVI_CLUSTER_SYMBOL_LEFT, OPEN_NAVI_CLUSTER_SYMBOL_RIGHT);
    if (a.through)
        return pick(side, OPEN_NAVI_CLUSTER_SYMBOL_T_TURN_LEFT,
                    OPEN_NAVI_CLUSTER_SYMBOL_T_TURN_RIGHT);
    if (a.any(other))
        return pick(side, OPEN_NAVI_CLUSTER_SYMBOL_T_END_LEFT, OPEN_NAVI_CLUSTER_SYMBOL_T_END_RIGHT);
    return 0;
}

// The route leaves on an ahead diagonal to `side` (slight turn, keep, fork).
uint8_t branch(const Arms& a, int side, double route_angle, int8_t lane_change) noexcept
{
    const int other = 1 - side;
    if (a.any(side))
        return 0;  // more roads on the route's side than any picture has
    if (a.through && a.only_behind(other))
        // '|' crossed by a diagonal, arrow on its ahead arm: '\' to the
        // left (33), '/' to the right (30).
        return pick(side, OPEN_NAVI_CLUSTER_SYMBOL_CROSS_BACKSLASH_LEFT,
                    OPEN_NAVI_CLUSTER_SYMBOL_CROSS_SLASH_RIGHT);
    if (a.through && a.only_diagonal(other))
        return three_way(side, std::fabs(route_angle) <= kNarrowSplit &&
                                   a.diagonal_angle[other] <= kNarrowSplit);
    if (a.through && !a.any(other)) {
        const int toward = side == kLeft ? -1 : 1;
        if (lane_change == toward)
            return pick(side, OPEN_NAVI_CLUSTER_SYMBOL_LANE_CHANGE_LEFT,
                        OPEN_NAVI_CLUSTER_SYMBOL_LANE_CHANGE_RIGHT);
        return pick(side, OPEN_NAVI_CLUSTER_SYMBOL_EXIT_LEFT, OPEN_NAVI_CLUSTER_SYMBOL_EXIT_RIGHT);
    }
    if (!a.through && a.only_diagonal(other))
        return pick(side, OPEN_NAVI_CLUSTER_SYMBOL_FORK_LEFT, OPEN_NAVI_CLUSTER_SYMBOL_FORK_RIGHT);
    return 0;
}

// The route goes straight on (|angle| <= kThrough). keep_side: the side
// OsmAnd says to keep (-1: none); a branch on that side would contradict it.
uint8_t straight(const Arms& a, int keep_side) noexcept
{
    if (a.through)
        return 0;  // two roads ahead: a narrow fork, the turn type says which
    const bool left = a.any(kLeft), right = a.any(kRight);
    if (a.only_perpendicular(kLeft) && a.only_perpendicular(kRight))
        return OPEN_NAVI_CLUSTER_SYMBOL_CROSSING_STRAIGHT;
    if (a.only_perpendicular(kLeft) && !right)
        return OPEN_NAVI_CLUSTER_SYMBOL_SIDE_LEFT_STRAIGHT;
    if (a.only_perpendicular(kRight) && !left)
        return OPEN_NAVI_CLUSTER_SYMBOL_SIDE_RIGHT_STRAIGHT;
    if (a.only_diagonal(kRight) && a.only_behind(kLeft))
        return OPEN_NAVI_CLUSTER_SYMBOL_CROSS_SLASH_STRAIGHT;
    if (a.only_diagonal(kLeft) && a.only_behind(kRight))
        return OPEN_NAVI_CLUSTER_SYMBOL_CROSS_BACKSLASH_STRAIGHT;
    if (a.only_diagonal(kLeft) && a.only_diagonal(kRight))
        return three_way(-1, a.diagonal_angle[kLeft] <= kNarrowSplit &&
                                 a.diagonal_angle[kRight] <= kNarrowSplit);
    if (a.only_diagonal(kLeft) && !right && keep_side != kLeft)
        return OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT_PAST_LEFT;
    if (a.only_diagonal(kRight) && !left && keep_side != kRight)
        return OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT_PAST_RIGHT;
    return 0;
}

} // namespace

ui::JunctionShape make_junction_shape(double route_angle, const std::vector<double>& arms)
{
    ui::JunctionShape shape;
    if (!std::isfinite(route_angle))
        return shape;
    shape.valid = true;
    shape.route_angle = static_cast<float>(normalise(route_angle));
    for (const double raw : arms) {
        if (!std::isfinite(raw)) continue;
        const double angle = normalise(raw);
        if (difference(angle, shape.route_angle) <= kSameArm) continue;
        bool duplicate = false;
        for (std::size_t i = 0; i < shape.branch_count; ++i)
            duplicate = duplicate || difference(angle, shape.branches[i]) <= kSameArm;
        if (duplicate) continue;
        if (shape.branch_count == ui::JunctionShape::kMaxBranches) {
            shape.truncated = true;
            continue;
        }
        shape.branches[shape.branch_count++] = static_cast<float>(angle);
        if (std::fabs(angle) <= kThrough) shape.through_road = true;
    }
    return shape;
}

void summarize_lanes(const std::vector<ui::Lane>& lanes, ui::JunctionShape& shape) noexcept
{
    shape.lane_count = 0;
    shape.recommended_lanes = 0;
    shape.lane_change = 0;
    if (lanes.empty() || lanes.size() > 16)
        return;
    shape.lane_count = static_cast<uint8_t>(lanes.size());
    for (std::size_t i = 0; i < lanes.size(); ++i)
        if (lanes[i].recommended)
            shape.recommended_lanes = static_cast<uint16_t>(shape.recommended_lanes | (1u << i));
    const unsigned all = (1u << lanes.size()) - 1u;
    const unsigned rec = shape.recommended_lanes;
    if (rec == 0 || rec == all)
        return;
    // A contiguous block at an edge: from the left edge 0b0..0111, from
    // the right edge 0b1..1000 (bit 0 = leftmost).
    const bool from_left = (rec & (rec + 1u)) == 0u;
    const unsigned shifted = rec >> __builtin_ctz(rec);
    const bool contiguous = (shifted & (shifted + 1u)) == 0u;
    const bool to_right = contiguous && (rec | (rec - 1u)) == all;
    shape.lane_change = from_left ? int8_t{-1} : to_right ? int8_t{1} : int8_t{0};
}

uint8_t junction_symbol(const ui::Maneuver& m) noexcept
{
    const auto& j = m.junction;
    if (!j.valid)
        return 0;
    const Arms a = arms_of(j);
    if (a.ambiguous)
        return 0;
    const double r = j.route_angle;
    const double size = std::fabs(r);
    const int side = r < 0.0 ? kLeft : kRight;
    using K = ui::ManeuverKind;
    switch (m.kind) {
    case K::Left:
    case K::Right: {
        const int want = m.kind == K::Left ? kLeft : kRight;
        if (side != want || size <= kThrough) return 0;
        return turn(a, want);
    }
    case K::SlightLeft:
    case K::SlightRight: {
        const int want = m.kind == K::SlightLeft ? kLeft : kRight;
        if (side != want || size <= kThrough || size > kRouteSlightMax) return 0;
        return branch(a, want, r, j.lane_change);
    }
    case K::SharpLeft:
    case K::SharpRight: {
        // '|' crossed by a diagonal, arrow on its behind arm: '/' to the
        // left (31), '\' to the right (32).
        const int want = m.kind == K::SharpLeft ? kLeft : kRight;
        const int other = 1 - want;
        if (side != want || size < kRouteSharpMin || size > kBehindMax) return 0;
        if (a.through && a.only_diagonal(other) && !a.any(want))
            return pick(want, OPEN_NAVI_CLUSTER_SYMBOL_CROSS_SLASH_LEFT,
                        OPEN_NAVI_CLUSTER_SYMBOL_CROSS_BACKSLASH_RIGHT);
        return 0;
    }
    case K::KeepLeft:
    case K::KeepRight:
    case K::Straight: {
        const int keep = m.kind == K::KeepLeft ? kLeft : m.kind == K::KeepRight ? kRight : -1;
        if (size <= kThrough)
            return straight(a, keep);
        if (keep >= 0 && side == keep && size <= kRouteSlightMax)
            return branch(a, keep, r, j.lane_change);
        return 0;
    }
    case K::UTurn:
    case K::Roundabout:
    case K::Arrive:
        return 0;
    }
    return 0;
}

} // namespace navicore_osmand
