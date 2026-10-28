#include "cluster_guidance.h"
#include "cluster_payload.h"
#include <open_navicore/cluster.h>

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

using namespace navicore_osmand;
using ui::Screen;

static ui::GuidanceState turn(double distance)
{
    ui::GuidanceState g;
    g.position_available = true;
    g.distance_to_maneuver_metres = distance;
    g.maneuver.kind = ui::ManeuverKind::Right;
    g.maneuver.geometry_index = 42;
    g.maneuver.distance_from_start_metres = 6000;
    g.maneuver.road = "Next street";
    return g;
}

static void preview_and_approach()
{
    ClusterGuidance p;
    auto g = turn(5000);
    assert(p.update(Screen::Guidance, g, 50 / 3.6, 0) == ClusterPhase::Preview);
    g.distance_to_maneuver_metres = 4900;
    g.maneuver.road = "Updated spelling";
    g.road = "Current street";
    assert(p.update(Screen::Guidance, g, 50 / 3.6, 11.99) == ClusterPhase::Preview);
    assert(p.update(Screen::Guidance, g, 50 / 3.6, 12) == ClusterPhase::Quiet);
    assert(p.update(Screen::Guidance, g, 50 / 3.6, 2) == ClusterPhase::Quiet);
    assert(p.update(Screen::Guidance, g, 0, 60) == ClusterPhase::Quiet);
    g.distance_to_maneuver_metres = 360;
    assert(p.update(Screen::Guidance, g, 50 / 3.6, 61) == ClusterPhase::Quiet);
    g.distance_to_maneuver_metres = 340;
    assert(p.update(Screen::Guidance, g, 50 / 3.6, 62) == ClusterPhase::Approach);
    // Braking and distance jitter must not dismiss an approach already shown.
    g.distance_to_maneuver_metres = 355;
    assert(p.update(Screen::Guidance, g, 0, 63) == ClusterPhase::Approach);
    assert(p.update(Screen::Guidance, g, 0, 600) == ClusterPhase::Approach);
    // A lost fix does not restart the preview or forget the approach.
    g.position_available = false;
    assert(p.update(Screen::Guidance, g, 0, 601) == ClusterPhase::Quiet);
    g.position_available = true;
    assert(p.update(Screen::Guidance, g, 0, 602) == ClusterPhase::Approach);
    // A close second turn stays up even at a standstill.
    ++g.maneuver.geometry_index;
    g.maneuver.distance_from_start_metres += 250;
    g.distance_to_maneuver_metres = 250;
    assert(p.update(Screen::Guidance, g, 0, 603) == ClusterPhase::Approach);
    assert(p.update(Screen::Guidance, g, 0, 630) == ClusterPhase::Approach);
    // A distant third turn gets its own preview, even with the same arrow/name.
    ++g.maneuver.geometry_index;
    g.maneuver.distance_from_start_metres += 5000;
    g.distance_to_maneuver_metres = 5000;
    assert(p.update(Screen::Guidance, g, 50 / 3.6, 631) == ClusterPhase::Preview);
    assert(p.update(Screen::Guidance, g, 50 / 3.6, 643) == ClusterPhase::Quiet);
    p.reset(); // a replacement route may have exactly the same first turn
    assert(p.update(Screen::Guidance, g, 50 / 3.6, 644) == ClusterPhase::Preview);
    p.update(Screen::FreeDrive, g, 0, 645);
    assert(p.update(Screen::Guidance, g, 50 / 3.6, 700) == ClusterPhase::Preview);
    g.recalculating = true;
    p.update(Screen::Guidance, g, 0, 710);
    g.recalculating = false;
    assert(p.update(Screen::Guidance, g, 50 / 3.6, 730) == ClusterPhase::Preview);
}

static void speeds_and_missing_input()
{
    for (const auto& sample : {std::pair<double, double>{0, 150}, {30, 208.333333},
                              {50, 347.222222}, {90, 1125}, {130, 1625}, {250, 2000}}) {
        const double threshold = cluster_approach_metres(sample.first / 3.6);
        assert(std::abs(threshold - sample.second) < 0.001);
        ClusterGuidance p;
        auto g = turn(threshold + 10);
        p.update(Screen::Guidance, g, sample.first / 3.6, 0);
        assert(p.update(Screen::Guidance, g, sample.first / 3.6, 12) == ClusterPhase::Quiet);
        g.distance_to_maneuver_metres = threshold - 1;
        assert(p.update(Screen::Guidance, g, sample.first / 3.6, 13) == ClusterPhase::Approach);
    }
    ClusterGuidance p;
    auto g = turn(1500);
    p.update(Screen::Guidance, g, 50 / 3.6, 0);
    assert(p.update(Screen::Guidance, g, 50 / 3.6, 12) == ClusterPhase::Quiet);
    assert(p.update(Screen::Guidance, g, 130 / 3.6, 13) == ClusterPhase::Approach);
    p.reset();
    const double missing = std::numeric_limits<double>::quiet_NaN();
    p.update(Screen::Guidance, g, missing, 0);
    assert(p.update(Screen::Guidance, g, missing, 30) == ClusterPhase::Preview);
    // Missing speed keeps guidance visible but does not latch forever.
    assert(p.update(Screen::Guidance, g, 50 / 3.6, 31) == ClusterPhase::Quiet);
    g.distance_to_maneuver_metres = 149;
    assert(p.update(Screen::Guidance, g, missing, 32) == ClusterPhase::Approach);
}

static void units_and_validity()
{
    OpenNaviBackendVehicleV2 v{};
    v.struct_size = sizeof(v);
    v.valid_fields = OPEN_NAVI_BACKEND_VEHICLE_SPEED;
    const auto set = [&](float value, uint32_t unit) {
        std::memcpy(&v.displayed_vehicle_speed_bits, &value, sizeof(value));
        v.displayed_vehicle_speed_unit = unit;
    };
    set(90, 1);
    assert(cluster_vehicle_speed_mps(v) == 25);
    set(60, 2);
    assert(std::abs(cluster_vehicle_speed_mps(v) - 26.8224) < 0.0001);
    set(0, 1);
    assert(cluster_vehicle_speed_mps(v) == 0); // stopped is valid
    for (float value : {-1.0f, 326.0f, std::numeric_limits<float>::infinity(),
                         std::numeric_limits<float>::quiet_NaN()}) {
        set(value, 1);
        assert(std::isnan(cluster_vehicle_speed_mps(v)));
    }
    set(90, 0);
    assert(std::isnan(cluster_vehicle_speed_mps(v)));
    set(90, 1);
    v.struct_size = 32; // older provider's prefix has no known unit
    assert(std::isnan(cluster_vehicle_speed_mps(v)));
    v.struct_size = sizeof(v);
    v.valid_fields = 0;
    assert(std::isnan(cluster_vehicle_speed_mps(v)));
}

static void cluster_visibility()
{
    ClusterInputs in;
    in.screen = Screen::Guidance;
    in.guidance = turn(5000);
    in.guidance.remaining_metres = 10000;
    in.guidance.remaining_seconds = 600;
    ClusterGuidance p;
    in.phase = p.update(in.screen, in.guidance, 50 / 3.6, 0);
    auto preview = build_cluster_payload(in);
    constexpr unsigned visible = OPEN_NAVI_CLUSTER_ROUTE_SYMBOL | OPEN_NAVI_CLUSTER_BAR_PERCENT;
    assert((preview.cluster.valid_attributes & visible) == visible);
    in.phase = p.update(in.screen, in.guidance, 50 / 3.6, 12);
    auto quiet = build_cluster_payload(in);
    assert(!(quiet.cluster.valid_attributes & visible));
    assert(quiet.cluster.route_guidance_state == OPEN_NAVI_CLUSTER_ROUTE_FOLLOW_STREET);
    assert(quiet.cluster.valid_attributes & OPEN_NAVI_CLUSTER_DISTANCE_DESTINATION);
    assert(quiet.cluster.valid_attributes & OPEN_NAVI_CLUSTER_ETA);
    assert(quiet.cluster.route_guidance_symbol == 0 && quiet.next_road.empty());
    in.guidance.distance_to_maneuver_metres = 1600;
    in.phase = p.update(in.screen, in.guidance, 130 / 3.6, 13);
    auto approach = build_cluster_payload(in);
    assert((approach.cluster.valid_attributes & visible) == visible);
    assert(approach.cluster.route_guidance_state == OPEN_NAVI_CLUSTER_ROUTE_MANOEUVRE_ANNOUNCEMENT);
    assert(approach.cluster.route_guidance_symbol == preview.cluster.route_guidance_symbol);
    assert(!apply_text_redraw(approach, quiet));
    in.guidance.distance_to_maneuver_metres = 40;
    assert(build_cluster_payload(in).cluster.route_guidance_state == OPEN_NAVI_CLUSTER_ROUTE_MANOEUVRE);
    // Status messages must never be suppressed by a stale quiet phase.
    in.phase = ClusterPhase::Quiet;
    in.guidance.recalculating = true;
    assert((build_cluster_payload(in).cluster.valid_attributes & visible) == visible);
    in.guidance.recalculating = false;
    in.guidance.arrived = true;
    assert((build_cluster_payload(in).cluster.valid_attributes & visible) == visible);
}

int main()
{
    preview_and_approach();
    speeds_and_missing_input();
    units_and_validity();
    cluster_visibility();
    std::cout << "cluster_guidance_test: ok\n";
}
