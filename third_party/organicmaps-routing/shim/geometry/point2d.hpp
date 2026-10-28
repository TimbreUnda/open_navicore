// Shim for Organic Maps geometry/point2d.hpp: the subset of m2::Point the turn
// generator uses (same operators and comparisons as upstream).
#pragma once

#include <cmath>
#include <sstream>
#include <string>

namespace m2
{
template <typename T>
class Point
{
public:
  using value_type = T;

  T x, y;

  constexpr Point() : x(T()), y(T()) {}
  constexpr Point(T x_, T y_) : x(x_), y(y_) {}

  static Point Zero() { return Point(0, 0); }

  bool EqualDxDy(Point const & p, T eps) const { return ((std::fabs(x - p.x) < eps) && (std::fabs(y - p.y) < eps)); }
  T SquaredLength(Point const & p) const { return (x - p.x) * (x - p.x) + (y - p.y) * (y - p.y); }
  double Length(Point const & p) const { return std::sqrt(SquaredLength(p)); }

  bool operator==(Point const & p) const { return x == p.x && y == p.y; }
  bool operator!=(Point const & p) const { return !(*this == p); }
  Point operator+(Point const & pt) const { return Point(x + pt.x, y + pt.y); }
  Point operator-(Point const & pt) const { return Point(x - pt.x, y - pt.y); }
  Point operator-() const { return Point(-x, -y); }
  Point operator*(T scale) const { return Point(x * scale, y * scale); }
  Point operator/(T scale) const { return Point(x / scale, y / scale); }
};

template <typename T>
bool operator<(Point<T> const & l, Point<T> const & r)
{
  if (l.x != r.x)
    return l.x < r.x;
  return l.y < r.y;
}

using PointD = Point<double>;

template <typename T>
std::string DebugPrint(Point<T> const & p)
{
  std::ostringstream out;
  out.precision(20);
  out << "m2::Point(" << p.x << ", " << p.y << ")";
  return out.str();
}
}  // namespace m2
