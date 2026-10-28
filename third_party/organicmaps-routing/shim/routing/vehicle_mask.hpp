// Shim for Organic Maps routing/vehicle_mask.hpp (VehicleType only).
#pragma once

#include <string>

namespace routing
{
enum class VehicleType
{
  Pedestrian = 0,
  Bicycle = 1,
  Car = 2,
  Transit = 3,
  Count = 4
};

inline std::string DebugPrint(VehicleType type)
{
  switch (type)
  {
  case VehicleType::Pedestrian: return "Pedestrian";
  case VehicleType::Bicycle: return "Bicycle";
  case VehicleType::Car: return "Car";
  case VehicleType::Transit: return "Transit";
  case VehicleType::Count: return "Count";
  }
  return "?";
}
}  // namespace routing
