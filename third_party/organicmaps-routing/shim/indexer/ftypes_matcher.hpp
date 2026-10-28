// Shim for Organic Maps indexer/ftypes_matcher.hpp: only HighwayClass (same enum,
// same order as upstream). The adapter maps OsmAnd highway tags onto it the way
// ftypes::GetHighwayClass maps OM types (see omaps_adapter.cpp).
#pragma once

#include <string>

namespace ftypes
{
enum class HighwayClass
{
  Undefined = 0,  // There has not been any attempt of calculating HighwayClass.
  Trunk,
  Primary,
  Secondary,
  Tertiary,
  LivingStreet,
  Service,
  // OSM highway=service type is widely used even for _significant_ roads.
  // Adding a new type to distinguish mapped driveway or parking_aisle.
  ServiceMinor,
  Pedestrian,
  Transported,  // Vehicles are transported by train or ferry.
  Count         // This value is used for internals only.
};

inline std::string DebugPrint(HighwayClass const cls)
{
  static char const * const names[] = {"Undefined", "Trunk",        "Primary",    "Secondary",   "Tertiary", "LivingStreet",
                                       "Service",   "ServiceMinor", "Pedestrian", "Transported", "Count"};
  return std::string{"[ "} + names[static_cast<int>(cls)] + " ]";
}
}  // namespace ftypes
