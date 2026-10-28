// Shim for Organic Maps indexer/feature_decl.hpp. m_index is the adapter's dense
// number for an OsmAnd road id; m_mwmId is 0 for real roads and -1 (invalid) for
// the fake start/finish edges.
#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <tuple>

struct FeatureID
{
  FeatureID() = default;
  FeatureID(int mwmId, std::uint32_t index) : m_mwmId(mwmId), m_index(index) {}

  bool IsValid() const { return m_mwmId >= 0; }
  std::string GetMwmName() const { return "osmand"; }

  bool operator==(FeatureID const & r) const { return m_mwmId == r.m_mwmId && m_index == r.m_index; }
  bool operator!=(FeatureID const & r) const { return !(*this == r); }
  bool operator<(FeatureID const & r) const { return std::tie(m_mwmId, m_index) < std::tie(r.m_mwmId, r.m_index); }

  int m_mwmId = -1;
  std::uint32_t m_index = 0;
};

inline std::string DebugPrint(FeatureID const & id)
{
  std::ostringstream out;
  out << "{ " << id.m_mwmId << ", " << id.m_index << " }";
  return out.str();
}
