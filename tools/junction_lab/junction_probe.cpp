// junction_probe: the cluster picture the OsmAnd backend sends for one
// junction, computed by the real backend code (junction_shape.cpp,
// cluster_payload.cpp). Used by junction_lab.py; prints one JSON object.
//
//   junction_probe --kind left --route 87 --arms=-92,3,175
//                  [--lanes '..*'] [--turn-angle 87] [--left-side]
//
// Angles are degrees relative to the incoming direction of travel,
// positive clockwise (as navigation_worker.cpp's junction_of() measures
// them). --lanes: one character per lane, left to right, '*' for a
// recommended lane.

#include "cluster_payload.h"
#include "junction_shape.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using navicore_osmand::ui::ManeuverKind;

struct KindName {
    const char* name;
    ManeuverKind kind;
};

constexpr KindName kind_names[] = {
    {"straight", ManeuverKind::Straight},       {"left", ManeuverKind::Left},
    {"slight_left", ManeuverKind::SlightLeft},  {"sharp_left", ManeuverKind::SharpLeft},
    {"right", ManeuverKind::Right},             {"slight_right", ManeuverKind::SlightRight},
    {"sharp_right", ManeuverKind::SharpRight},  {"keep_left", ManeuverKind::KeepLeft},
    {"keep_right", ManeuverKind::KeepRight},    {"uturn", ManeuverKind::UTurn},
    {"roundabout", ManeuverKind::Roundabout},   {"arrive", ManeuverKind::Arrive},
};

bool parse_kind(const char* text, ManeuverKind& kind)
{
    for (const auto& entry : kind_names)
        if (std::strcmp(entry.name, text) == 0) {
            kind = entry.kind;
            return true;
        }
    return false;
}

std::vector<double> parse_angles(const char* text)
{
    std::vector<double> angles;
    const char* cursor = text;
    while (*cursor) {
        char* end = nullptr;
        const double value = std::strtod(cursor, &end);
        if (end == cursor) break;
        angles.push_back(value);
        cursor = *end == ',' ? end + 1 : end;
    }
    return angles;
}

int usage()
{
    std::fprintf(stderr,
                 "usage: junction_probe --kind KIND --route DEG [--arms=DEG,...] "
                 "[--lanes PATTERN] [--turn-angle DEG] [--left-side]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    namespace nav = navicore_osmand;
    nav::ui::Maneuver maneuver;
    double route_angle = NAN;
    std::vector<double> arms;
    std::string lanes;
    bool kind_given = false;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        const bool has_value = i + 1 < argc;
        if (option == "--kind" && has_value) {
            if (!parse_kind(argv[++i], maneuver.kind)) return usage();
            kind_given = true;
        } else if (option == "--route" && has_value) {
            route_angle = std::strtod(argv[++i], nullptr);
        } else if (option.rfind("--arms=", 0) == 0) {
            arms = parse_angles(option.c_str() + 7);
        } else if (option == "--lanes" && has_value) {
            lanes = argv[++i];
        } else if (option == "--turn-angle" && has_value) {
            maneuver.turn_angle_degrees = std::strtod(argv[++i], nullptr);
        } else if (option == "--left-side") {
            maneuver.left_side = true;
        } else {
            return usage();
        }
    }
    if (!kind_given) return usage();

    for (const char c : lanes) {
        nav::ui::Lane lane;
        lane.recommended = c == '*';
        maneuver.lanes.push_back(lane);
    }
    // As navigation_worker.cpp: roundabouts get no junction geometry.
    if (maneuver.kind != ManeuverKind::Roundabout) {
        maneuver.junction = nav::make_junction_shape(route_angle, arms);
        nav::summarize_lanes(maneuver.lanes, maneuver.junction);
    }
    nav::ui::Maneuver without_junction = maneuver;
    without_junction.junction = {};

    const auto& j = maneuver.junction;
    std::printf("{\"valid\":%s,\"route_angle\":%.1f,\"branches\":[",
                j.valid ? "true" : "false", static_cast<double>(j.route_angle));
    for (std::size_t i = 0; i < j.branch_count; ++i)
        std::printf("%s%.1f", i ? "," : "", static_cast<double>(j.branches[i]));
    std::printf("],\"truncated\":%s,\"through_road\":%s,\"lane_count\":%u,"
                "\"recommended_lanes\":%u,\"lane_change\":%d,",
                j.truncated ? "true" : "false", j.through_road ? "true" : "false",
                static_cast<unsigned>(j.lane_count), static_cast<unsigned>(j.recommended_lanes),
                static_cast<int>(j.lane_change));
    std::printf("\"junction_symbol\":%u,\"turn_type_symbol\":%u,\"symbol\":%u,",
                static_cast<unsigned>(nav::junction_symbol(maneuver)),
                static_cast<unsigned>(nav::maneuver_symbol(without_junction)),
                static_cast<unsigned>(nav::maneuver_symbol(maneuver)));
    namespace k = nav::junction;
    std::printf("\"bands\":{\"through\":%g,\"diagonal_min\":%g,\"diagonal_max\":%g,"
                "\"perpendicular_min\":%g,\"perpendicular_max\":%g,\"behind_min\":%g,"
                "\"behind_max\":%g,\"route_slight_max\":%g,\"route_sharp_min\":%g,"
                "\"narrow_split\":%g,\"same_arm\":%g}}\n",
                k::kThrough, k::kDiagonalMin, k::kDiagonalMax, k::kPerpendicularMin,
                k::kPerpendicularMax, k::kBehindMin, k::kBehindMax, k::kRouteSlightMax,
                k::kRouteSharpMin, k::kNarrowSplit, k::kSameArm);
    return 0;
}
