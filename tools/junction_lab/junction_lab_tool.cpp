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

#include "omaps_adapter.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

// TurnType values 1..11 by name (turnType.h).
const char* turn_name(int value)
{
    static const char* const names[] = {"", "C", "TL", "TSLL", "TSHL", "TR", "TSLR", "TSHR",
                                        "KL", "KR", "TU", "TRU"};
    return value >= 0 && value <= 11 ? names[value] : "?";
}

// The incoming road's turn:lanes as OsmAnd reads it (getTurnLanesString in
// routeResultPreparation.cpp, not exported). Empty: the lanes OsmAnd shows
// are its own, made from the lanes count and the junction.
std::string turn_lanes_tag(const SHARED_PTR<RouteSegmentResult>& segment)
{
    if (segment->object->getOneway() != 0) return segment->object->getValue("turn:lanes");
    return segment->object->getValue(segment->isForwardDirection() ? "turn:lanes:forward"
                                                                    : "turn:lanes:backward");
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
        << ",\"lanes\":" << quoted(lanes) << ",\"lane_values\":[";
    const auto& values = segment->turnType->getLanes();
    for (std::size_t k = 0; k < values.size(); ++k) {
        out << (k ? "," : "") << "{\"active\":" << ((values[k] & 1) ? "true" : "false") << ",\"turns\":[";
        const int turns[] = {TurnType::getPrimaryTurn(values[k]), TurnType::getSecondaryTurn(values[k]),
                             TurnType::getTertiaryTurn(values[k])};
        bool first_turn = true;
        for (const int t : turns) {
            if (!t) continue;
            out << (first_turn ? "" : ",") << "\"" << turn_name(t) << "\"";
            first_turn = false;
        }
        out << "]}";
    }
    out << "]";
    if (index > 0 && segments[index - 1] && segments[index - 1]->object) {
        const auto& previous = segments[index - 1];
        out << ",\"incoming_turn_lanes\":" << quoted(turn_lanes_tag(previous))
            << ",\"incoming_lanes\":" << previous->object->getLanes();
    }
    out << ",\"road\":" << quoted(segment->getStreetName("", false, segments, static_cast<int>(index)))
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
    const auto started = std::chrono::steady_clock::now();
    const auto ms_since = [](std::chrono::steady_clock::time_point t) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
    };
    auto context = front_end.buildRoutingContext(config);
    std::vector<int> ix, iy;
    nav::Segments segments = front_end.searchRoute(
        context, get31TileNumberX(lon1), get31TileNumberY(lat1), get31TileNumberX(lon2),
        get31TileNumberY(lat2), ix, iy, nullptr);
    if (segments.empty())
        return "{\"ok\":false,\"error\":\"no route\"}";
    // JUNCTION_LAB_ROADS=1: every road the map has at each segment start
    // (stderr), including those the router does not attach (one-way roads
    // that only enter the node).
    if (std::getenv("JUNCTION_LAB_ROADS")) {
        for (std::size_t i = 1; i < segments.size(); ++i) {
            const auto& s = segments[i];
            if (!s || !s->object) continue;
            const int start = s->getStartPointIndex();
            const ::LatLon p = s->getPoint(start);
            std::fprintf(stderr, "node %.6f,%.6f route road=%lld turn=%s\n", p.lat, p.lon,
                         (long long)s->object->getId(),
                         s->turnType ? s->turnType->toString().c_str() : "-");
            for (auto& rs : context->loadRouteSegment(s->object->pointsX[start], s->object->pointsY[start])) {
                const auto& road = rs->getRoad();
                const int at = rs->getSegmentStart();
                const int n = static_cast<int>(road->getPointsLength());
                std::fprintf(stderr, "   road=%lld \"%s\" %s oneway=%d lanes=%d turn:lanes=\"%s\" point=%d/%d",
                             (long long)road->getId(), road->getName().c_str(), road->getHighway().c_str(),
                             road->getOneway(), road->getLanes(), road->getValue("turn:lanes").c_str(), at, n);
                if (at + 1 < n)
                    std::fprintf(stderr, " fwd=%.0f", road->directionRoute(at, true, 10.0f) / M_PI * 180.0);
                if (at > 0)
                    std::fprintf(stderr, " back=%.0f", road->directionRoute(at, false, 10.0f) / M_PI * 180.0);
                std::fprintf(stderr, "\n");
            }
        }
    }
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
    // Every junction on the route, also where OsmAnd gives no turn: what
    // the router saw where the driver may expect a manoeuvre.
    out << "],\"junctions\":[";
    first = true;
    for (std::size_t i = 1; i < segments.size(); ++i) {
        if (!segments[i]) continue;
        const auto j = nav::junction_of(segments, i);
        if (!j.valid || j.branch_count == 0) continue;
        const ::LatLon p = segments[i]->getPoint(segments[i]->getStartPointIndex());
        out << (first ? "" : ",") << "{\"segment\":" << i << ",\"lat\":" << p.lat << ",\"lon\":" << p.lon
            << ",\"turn\":" << quoted(segments[i]->turnType ? segments[i]->turnType->toString() : "-")
            << ",\"route_angle\":" << j.route_angle << ",\"branches\":[";
        for (std::size_t k = 0; k < j.branch_count; ++k)
            out << (k ? "," : "") << j.branches[k];
        out << "]}";
        first = false;
    }
    // The same route through Organic Maps' car turn generator (omaps_adapter).
    const double route_ms = ms_since(started);
    const auto omaps_started = std::chrono::steady_clock::now();
    const auto omaps = junction_lab::omaps::annotate(*context, segments, lat1, lon1, lat2, lon2);
    if (std::getenv("JUNCTION_LAB_TIMING"))
        std::fprintf(stderr, "timing: route+osmand_json=%.0fms omaps=%.0fms segments=%zu\n", route_ms,
                     ms_since(omaps_started), segments.size());
    out << "],\"omaps\":[";
    for (std::size_t i = 0; i < omaps.maneuvers.size(); ++i) {
        const auto& m = omaps.maneuvers[i];
        out << (i ? "," : "") << "{\"segment\":" << m.segment << ",\"lat\":" << m.lat << ",\"lon\":" << m.lon
            << ",\"turn\":" << quoted(m.turn) << ",\"exit\":" << m.exit << ",\"lanes\":[";
        for (std::size_t k = 0; k < m.lanes.size(); ++k) {
            out << (k ? "," : "") << "{\"active\":" << (m.lanes[k].active ? "true" : "false") << ",\"turns\":[";
            for (std::size_t t = 0; t < m.lanes[k].turns.size(); ++t)
                out << (t ? "," : "") << quoted(m.lanes[k].turns[t]);
            out << "]}";
        }
        out << "],\"road\":" << quoted(m.road) << "}";
    }
    out << "]";
    if (!omaps.ok)
        out << ",\"omaps_error\":" << quoted(omaps.error);
    out << "}";
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
