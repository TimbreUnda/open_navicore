// Shim for the parts of Organic Maps base/math.hpp the turn generator uses
// (same formulas as upstream).
#pragma once

#include "base/assert.hpp"
#include "base/logging.hpp"
#include "base/stl_helpers.hpp"

#include <algorithm>
#include <cmath>

namespace math
{
double constexpr pi = 3.14159265358979323846;
double constexpr pi2 = pi / 2.0;
double constexpr pi4 = pi / 4.0;

template <typename T>
T DegToRad(T deg)
{
  return deg * pi / 180.0;
}

template <typename T>
T RadToDeg(T rad)
{
  return rad * 180.0 / pi;
}

template <typename T>
T Clamp(T x, T xmin, T xmax)
{
  return std::min(std::max(x, xmin), xmax);
}

template <typename T>
bool Between(T a, T b, T x)
{
  return a <= x && x <= b;
}

template <typename T>
T Pow2(T x)
{
  return x * x;
}
}  // namespace math

template <typename Float>
bool AlmostEqualAbs(Float x, Float y, Float eps)
{
  return std::fabs(x - y) < eps;
}
