// Shim for Organic Maps geometry/mercator.hpp + distance_on_sphere: FromLatLon,
// ToLatLon and DistanceOnEarth with the upstream formulas (X = lon in degrees,
// Y = mercator latitude in degrees, Earth radius 6378000 m).
#pragma once

#include "base/math.hpp"
#include "geometry/point2d.hpp"

#include <algorithm>
#include <cmath>

namespace ms
{
double constexpr kEarthRadiusMeters = 6378000.0;

struct LatLon
{
  double m_lat = 0.0;
  double m_lon = 0.0;
};

inline double DistanceOnSphere(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg)
{
  double const lat1 = math::DegToRad(lat1Deg);
  double const lat2 = math::DegToRad(lat2Deg);
  double const dlat = std::sin((lat2 - lat1) * 0.5);
  double const dlon = std::sin((math::DegToRad(lon2Deg) - math::DegToRad(lon1Deg)) * 0.5);
  double const y = dlat * dlat + dlon * dlon * std::cos(lat1) * std::cos(lat2);
  return 2.0 * std::atan2(std::sqrt(y), std::sqrt(std::max(0.0, 1.0 - y)));
}

inline double DistanceOnEarth(LatLon const & ll1, LatLon const & ll2)
{
  return kEarthRadiusMeters * DistanceOnSphere(ll1.m_lat, ll1.m_lon, ll2.m_lat, ll2.m_lon);
}
}  // namespace ms

namespace mercator
{
inline double ClampY(double d)
{
  return math::Clamp(d, -180.0, 180.0);
}

inline double YToLat(double y)
{
  return math::RadToDeg(2.0 * std::atan(std::tanh(0.5 * math::DegToRad(y))));
}

inline double LatToY(double lat)
{
  double const sinx = std::sin(math::DegToRad(math::Clamp(lat, -86.0, 86.0)));
  double const res = math::RadToDeg(0.5 * std::log((1.0 + sinx) / (1.0 - sinx)));
  return ClampY(res);
}

inline m2::PointD FromLatLon(double lat, double lon)
{
  return m2::PointD(lon, LatToY(lat));
}

inline ms::LatLon ToLatLon(m2::PointD const & point)
{
  return {YToLat(point.y), point.x};
}

inline double DistanceOnEarth(m2::PointD const & p1, m2::PointD const & p2)
{
  return ms::DistanceOnEarth(ToLatLon(p1), ToLatLon(p2));
}
}  // namespace mercator
