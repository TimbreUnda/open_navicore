// Shim for Organic Maps geometry/angles.hpp: AngleTo and TwoVectorsAngle (upstream code).
#pragma once

#include "base/math.hpp"
#include "geometry/point2d.hpp"

#include <cmath>

namespace ang
{
template <typename T>
T AngleTo(m2::Point<T> const & p1, m2::Point<T> const & p2)
{
  return std::atan2(p2.y - p1.y, p2.x - p1.x);
}

/// Returns an angle from vector [p, p1] to vector [p, p2]. A counterclockwise rotation.
/// Angle is in range [0, 2 * pi]
template <typename T>
T TwoVectorsAngle(m2::Point<T> const & p, m2::Point<T> const & p1, m2::Point<T> const & p2)
{
  // Each finite AngleTo() result is in [-pi, pi], so one period normalizes a negative difference.
  T a = ang::AngleTo(p, p2) - ang::AngleTo(p, p1);
  if (a < 0)
    a += 2.0 * math::pi;
  return a;
}
}  // namespace ang
