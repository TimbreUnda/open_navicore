// Shim for Organic Maps platform/measurement_utils.hpp (KmphToMps only).
#pragma once

namespace measurement_utils
{
inline double KmphToMps(double kmph)
{
  return kmph * 1000.0 / 3600.0;
}
}  // namespace measurement_utils
