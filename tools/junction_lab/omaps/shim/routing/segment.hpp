// Trimmed Organic Maps routing/segment.hpp + segment.cpp: class Segment only
// (no RoadPoint / RouteWeight / SegmentEdge). Member functions copied unchanged.
#pragma once

#include "routing_common/num_mwm_id.hpp"

#include <cstdint>
#include <sstream>
#include <string>

namespace routing
{
// This is directed road segment used as vertex in index graph.
class Segment final
{
public:
  Segment() = default;

  constexpr Segment(NumMwmId mwmId, uint32_t featureId, uint32_t segmentIdx, bool forward)
    : m_featureId(featureId)
    , m_segmentIdx(segmentIdx)
    , m_mwmId(mwmId)
    , m_forward(forward)
  {}

  NumMwmId GetMwmId() const { return m_mwmId; }
  uint32_t GetFeatureId() const { return m_featureId; }
  uint32_t GetSegmentIdx() const { return m_segmentIdx; }
  bool IsForward() const { return m_forward; }

  uint32_t GetPointId(bool front) const { return m_forward == front ? m_segmentIdx + 1 : m_segmentIdx; }
  uint32_t GetMinPointId() const { return m_segmentIdx; }
  uint32_t GetMaxPointId() const { return m_segmentIdx + 1; }

  bool operator<(Segment const & seg) const
  {
    if (m_featureId != seg.m_featureId)
      return m_featureId < seg.m_featureId;
    if (m_segmentIdx != seg.m_segmentIdx)
      return m_segmentIdx < seg.m_segmentIdx;
    if (m_mwmId != seg.m_mwmId)
      return m_mwmId < seg.m_mwmId;
    return m_forward < seg.m_forward;
  }

  bool operator==(Segment const & seg) const
  {
    return m_featureId == seg.m_featureId && m_segmentIdx == seg.m_segmentIdx && m_mwmId == seg.m_mwmId &&
           m_forward == seg.m_forward;
  }
  bool operator!=(Segment const & seg) const { return !(*this == seg); }

  bool IsInverse(Segment const & seg) const
  {
    return m_featureId == seg.m_featureId && m_segmentIdx == seg.m_segmentIdx && m_mwmId == seg.m_mwmId &&
           m_forward != seg.m_forward;
  }
  Segment GetReversed() const { return {m_mwmId, m_featureId, m_segmentIdx, !m_forward}; }

  void Next(bool forward) { forward ? ++m_segmentIdx : --m_segmentIdx; }

private:
  uint32_t m_featureId = 0;
  uint32_t m_segmentIdx = 0;
  NumMwmId m_mwmId = kFakeNumMwmId;
  bool m_forward = false;
};

inline std::string DebugPrint(Segment const & segment)
{
  std::ostringstream out;
  out << std::boolalpha << "Segment(" << segment.GetMwmId() << ", " << segment.GetFeatureId() << ", "
      << segment.GetSegmentIdx() << ", " << segment.IsForward() << ")";
  return out.str();
}
}  // namespace routing
