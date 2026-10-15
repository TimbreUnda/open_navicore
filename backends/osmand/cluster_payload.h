#pragma once

#include "ui/navigation_model.h"

#include <open_navicore/backend.h>

#include <ctime>
#include <string>

namespace navicore_osmand {

// The instrument-cluster payload for the current navigation state, using
// only the recovered fields (include/open_navicore/cluster.h semantic mask;
// no maneuver symbol or lanes). The road spans point into the strings, so
// keep the payload object alive (and unmoved) while publishing.
struct ClusterPayload {
    MockNavBackendClusterV1 cluster{};
    std::string current_road;
    std::string next_road;
    // Points the spans at the strings; call after any copy or move.
    void bind() noexcept;
    bool same_as(const ClusterPayload& other) const noexcept;
};

struct ClusterInputs {
    ui::Screen screen = ui::Screen::FreeDrive;
    bool calculating_route = false;
    ui::GuidanceState guidance;
    bool heading_valid = false;  // vehicle moving with a known heading
    double heading_degrees = 0.0;
    std::time_t now = 0;         // for the arrival clock time
};

ClusterPayload build_cluster_payload(const ClusterInputs& inputs);

} // namespace navicore_osmand
