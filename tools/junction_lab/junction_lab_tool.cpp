// junction_lab_tool: routes with OsmAnd's router on the host and prints each
// manoeuvre as the radio would send it to the cluster. The manoeuvres come
// from the backend's own code (route_maneuvers.cpp, junction_shape.cpp,
// cluster_payload.cpp); only the router build differs (host, not QNX).
//
//   junction_lab_tool MAPS ROUTING_XML
//
// MAPS is one .obf or a directory of them. Then one request per stdin line,
// "LAT1 LON1 LAT2 LON2", one JSON object per line on stdout. Plain A*: the
// worker's router for short routes; every router ends in the same
// prepareResult, which decides the turns.

#include "cluster_payload.h"
#include "junction_shape.h"
#include "route_maneuvers.h"

#include "binaryRead.h"
#include "routePlannerFrontEnd.h"
#include "routingConfiguration.h"
#include "routingContext.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace nav = navicore_osmand;
using nav::ui::ManeuverKind;

const char* kind_name(ManeuverKind kind)
{
    switch (kind) {
    case ManeuverKind::Straight: return "straight";
    case ManeuverKind::Left: return "left";
    case ManeuverKind::SlightLeft: return "slight_left";
    case ManeuverKind::SharpLeft: return "sharp_left";
    case ManeuverKind::Right: return "right";
    case ManeuverKind::SlightRight: return "slight_right";
    case ManeuverKind::SharpRight: return "sharp_right";
    case ManeuverKind::KeepLeft: return "keep_left";
    case ManeuverKind::KeepRight: return "keep_right";
    case ManeuverKind::UTurn: return "uturn";
    case ManeuverKind::Roundabout: return "roundabout";
    case ManeuverKind::Arrive: return "arrive";
    }
    return "?";
}

std::string quoted(const std::string& text)
{
    std::string out = "\"";
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 0x20) {
            char escape[8];
            std::snprintf(escape, sizeof(escape), "\\u%04x", c);
            out += escape;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

std::vector<std::string> map_files(const std::string& path)
{
    std::vector<std::string> files;
    struct stat info {};
    if (::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
        if (DIR* dir = ::opendir(path.c_str())) {
            while (const dirent* entry = ::readdir(dir)) {
                const std::string name = entry->d_name;
                if (name.size() > 4 && name[0] != '.' &&
                    name.compare(name.size() - 4, 4, ".obf") == 0)
                    files.push_back(path + "/" + name);
            }
            ::closedir(dir);
        }
    } else {
        files.push_back(path);
    }
    return files;
}

void print_maneuver(std::ostringstream& out, nav::Segments& segments, std::size_t index,
                    const nav::ui::Maneuver& m)
{
    const auto& segment = segments[index];
    const ::LatLon at = segment->getPoint(segment->getStartPointIndex());
    // The incoming direction, for drawing the junction's arms on the map
    // (junction_of measures them relative to it).
    double incoming = NAN;
    if (index > 0 && segments[index - 1])
        incoming = segments[index - 1]->getBearingEnd(segments[index - 1]->getEndPointIndex(), 10.0f);
    nav::ui::Maneuver without_junction = m;
    without_junction.junction = {};
    const auto& j = m.junction;
    const std::string lanes = segment->turnType->getLanes().empty()
        ? "" : TurnType::toString(segment->turnType->getLanes());
    out << "{\"segment\":" << index << ",\"lat\":" << at.lat << ",\"lon\":" << at.lon
        << ",\"turn\":" << quoted(segment->turnType->toString())
        << ",\"kind\":\"" << kind_name(m.kind) << "\""
        << ",\"turn_angle\":" << m.turn_angle_degrees
        << ",\"left_side\":" << (m.left_side ? "true" : "false")
        << ",\"exit\":" << m.roundabout_exit
        << ",\"lanes\":" << quoted(lanes)
        << ",\"road\":" << quoted(segment->getStreetName("", false, segments, static_cast<int>(index)))
        << ",\"incoming_bearing\":" << (std::isfinite(incoming) ? incoming : 0.0)
        << ",\"junction\":{\"valid\":" << (j.valid ? "true" : "false")
        << ",\"route_angle\":" << j.route_angle << ",\"branches\":[";
    for (std::size_t i = 0; i < j.branch_count; ++i)
        out << (i ? "," : "") << j.branches[i];
    out << "],\"truncated\":" << (j.truncated ? "true" : "false")
        << ",\"through_road\":" << (j.through_road ? "true" : "false")
        << ",\"lane_count\":" << unsigned(j.lane_count)
        << ",\"recommended_lanes\":" << unsigned(j.recommended_lanes)
        << ",\"lane_change\":" << int(j.lane_change) << "}"
        << ",\"junction_symbol\":" << unsigned(nav::junction_symbol(m))
        << ",\"turn_type_symbol\":" << unsigned(nav::maneuver_symbol(without_junction))
        << ",\"symbol\":" << unsigned(nav::maneuver_symbol(m)) << "}";
}

std::string route(RoutePlannerFrontEnd& front_end, const SHARED_PTR<RoutingConfiguration>& config,
                  double lat1, double lon1, double lat2, double lon2)
{
    auto context = front_end.buildRoutingContext(config);
    std::vector<int> ix, iy;
    nav::Segments segments = front_end.searchRoute(
        context, get31TileNumberX(lon1), get31TileNumberY(lat1), get31TileNumberX(lon2),
        get31TileNumberY(lat2), ix, iy, nullptr);
    if (segments.empty())
        return "{\"ok\":false,\"error\":\"no route\"}";
    std::ostringstream out;
    out.precision(8);
    out << "{\"ok\":true,\"geometry\":[";
    bool first_point = true;
    for (const auto& segment : segments) {
        if (!segment) continue;
        const int start = segment->getStartPointIndex();
        const int end = segment->getEndPointIndex();
        const int step = start <= end ? 1 : -1;
        for (int i = start;; i += step) {
            const ::LatLon p = segment->getPoint(i);
            out << (first_point ? "" : ",") << "[" << p.lat << "," << p.lon << "]";
            first_point = false;
            if (i == end) break;
        }
    }
    out << "],\"maneuvers\":[";
    bool first = true;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        nav::ui::Maneuver maneuver;
        if (!nav::maneuver_at(segments, i, maneuver)) continue;
        if (!first) out << ",";
        first = false;
        print_maneuver(out, segments, i, maneuver);
    }
    out << "]}";
    return out.str();
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s MAPS ROUTING_XML\n", argv[0]);
        return 2;
    }
    auto builder = parseRoutingConfigurationFromXml(argv[2], "");
    if (!builder) {
        std::fprintf(stderr, "cannot parse %s\n", argv[2]);
        return 1;
    }
    std::size_t opened = 0;
    for (const auto& path : map_files(argv[1])) {
        const auto file = initBinaryMapFile(path, false, true);
        if (file && !file->routingIndexes.empty()) ++opened;
    }
    if (opened == 0) {
        std::fprintf(stderr, "no routable map in %s\n", argv[1]);
        return 1;
    }
    std::fprintf(stderr, "junction_lab_tool: %zu maps open\n", opened);
    std::cout << "{\"ready\":true,\"maps\":" << opened << "}" << std::endl;
    RoutePlannerFrontEnd front_end;
    std::string line;
    while (std::getline(std::cin, line)) {
        double lat1, lon1, lat2, lon2;
        if (std::sscanf(line.c_str(), "%lf %lf %lf %lf", &lat1, &lon1, &lat2, &lon2) != 4) {
            std::cout << "{\"ok\":false,\"error\":\"expected LAT1 LON1 LAT2 LON2\"}" << std::endl;
            continue;
        }
        MAP_STR_STR parameters;
        auto config = builder->build("car", 256, parameters);
        std::cout << route(front_end, config, lat1, lon1, lat2, lon2) << std::endl;
    }
    return 0;
}
