// Shim for Organic Maps geometry/point_with_altitude.hpp (altitude is always 0 here).
#pragma once

#include "geometry/point2d.hpp"

#include <cstdint>
#include <limits>
#include <string>

namespace geometry
{
using Altitude = int16_t;
Altitude constexpr kInvalidAltitude = std::numeric_limits<Altitude>::min();
Altitude constexpr kDefaultAltitudeMeters = 0;

class PointWithAltitude
{
public:
  PointWithAltitude() = default;
  PointWithAltitude(m2::PointD const & point, Altitude altitude = kDefaultAltitudeMeters)
    : m_point(point)
    , m_altitude(altitude)
  {}
  operator m2::PointD() const { return m_point; }

  bool operator==(PointWithAltitude const & r) const { return m_point == r.m_point && m_altitude == r.m_altitude; }
  bool operator!=(PointWithAltitude const & r) const { return !(*this == r); }
  bool operator<(PointWithAltitude const & r) const
  {
    if (m_point != r.m_point)
      return m_point < r.m_point;
    return m_altitude < r.m_altitude;
  }

  m2::PointD const & GetPoint() const { return m_point; }
  Altitude GetAltitude() const { return m_altitude; }

private:
  m2::PointD m_point;
  Altitude m_altitude = kDefaultAltitudeMeters;
};

inline std::string DebugPrint(PointWithAltitude const & p)
{
  return "PointWithAltitude{" + m2::DebugPrint(p.GetPoint()) + "}";
}
}  // namespace geometry
