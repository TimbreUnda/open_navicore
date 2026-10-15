#include "camera_controller.h"

#include <cassert>
#include <cmath>

using navicore_osmand::CameraController;
using navicore_osmand::MotionEstimator;

namespace {
bool near(double a, double b, double tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

CameraController::Vehicle vehicle(double heading, double speed)
{
    CameraController::Vehicle v;
    v.valid = true;
    v.latitude = 50.45;
    v.longitude = 30.52;
    v.heading_degrees = heading;
    v.speed_mps = speed;
    return v;
}

CameraController controller()
{
    CameraController camera;
    // Map area right of a 336 px rail in the 1024x574 navigation band.
    camera.set_viewport({336, 0, 688, 574});
    return camera;
}
} // namespace

int main()
{
    const CameraController::Settings settings;
    // Speed zoom: flat below 30 km/h, zooms out with speed, bounded.
    assert(CameraController::speed_zoom(settings, 0.0) == settings.follow_base_zoom);
    assert(CameraController::speed_zoom(settings, 8.0) == settings.follow_base_zoom);
    assert(near(CameraController::speed_zoom(settings, 16.0),
                settings.follow_base_zoom - settings.zoom_per_doubling, 1e-4));
    assert(CameraController::speed_zoom(settings, 1000.0) == settings.min_speed_zoom);
    // Shortest-arc rotation across north.
    assert(near(CameraController::shortest_angle_delta(350.0, 10.0), 20.0, 1e-9));
    assert(near(CameraController::shortest_angle_delta(10.0, 350.0), -20.0, 1e-9));

    {
        // Heading-up: vehicle anchored in the lower part of the map area and
        // the map rotated to the heading, immediately on the first frame.
        auto camera = controller();
        const auto pose = camera.update(vehicle(90.0, 10.0), 0.016);
        assert(pose.following);
        assert(pose.anchor_x == 336 + 688 / 2 + settings.heading_up_anchor_dx);
        assert(pose.anchor_y > 574 / 2 && pose.anchor_y < 574);
        assert(near(pose.azimuth_degrees, 90.0, 1e-3));
        assert(pose.elevation_degrees == 90.0f);
        assert(near(pose.latitude, 50.45, 1e-9));

        // Rotation is smoothed and takes the short way through north.
        auto turning = controller();
        (void)turning.update(vehicle(350.0, 10.0), 0.016);
        const auto turned = turning.update(vehicle(10.0, 10.0), 0.1);
        const double moved = CameraController::shortest_angle_delta(
            350.0, turned.azimuth_degrees);
        assert(moved > 0.0 && moved < 20.0);

        // Stopped vehicles keep their last heading instead of spinning.
        CameraController::Pose stopped;
        for (int i = 0; i < 10; ++i)
            stopped = turning.update(vehicle(200.0, 0.2), 1.0);
        assert(near(CameraController::shortest_angle_delta(
                        stopped.azimuth_degrees, 10.0), 0.0, 0.5));
    }
    {
        // North-up centres the vehicle; tilt applies only to heading-up.
        auto camera = controller();
        camera.toggle_orientation();
        camera.set_tilt(true);
        const auto pose = camera.update(vehicle(120.0, 10.0), 0.016);
        assert(camera.mode() == CameraController::Mode::NorthUp);
        assert(pose.anchor_y == 574 / 2);
        assert(near(pose.azimuth_degrees, 0.0, 1e-3));
        assert(pose.elevation_degrees == 90.0f);
        camera.toggle_orientation();
        // The tilt eases in (no jump), to the angle for 10 m/s.
        const auto easing = camera.update(vehicle(120.0, 10.0), 0.016);
        assert(easing.elevation_degrees < 90.0f && easing.elevation_degrees > 85.0f);
        CameraController::Pose tilted;
        for (int i = 0; i < 400; ++i)
            tilted = camera.update(vehicle(120.0, 10.0), 0.016);
        const float at_10 = settings.tilted_elevation +
            (settings.tilted_elevation_fast - settings.tilted_elevation) *
                (10.0f - settings.slow_speed_mps) / (settings.fast_speed_mps - settings.slow_speed_mps);
        assert(near(tilted.elevation_degrees, at_10, 0.01));
        // Flatter view, lower car.
        assert(tilted.anchor_y > easing.anchor_y);

        // A finger drag from the tilted view stays tilted at the same zoom
        // and angle and centres where the engine puts it.
        camera.begin_free(50.45, 30.52);
        const auto dragged = camera.update(vehicle(120.0, 10.0), 0.016);
        assert(camera.mode() == CameraController::Mode::Free);
        assert(!dragged.following);
        assert(near(dragged.elevation_degrees, tilted.elevation_degrees, 0.01));
        assert(near(dragged.zoom, tilted.zoom, 1e-3));
        camera.set_free_location(50.46, 30.53);
        const auto moved = camera.update(vehicle(120.0, 10.0), 0.016);
        assert(near(moved.latitude, 50.46, 1e-9) && near(moved.longitude, 30.53, 1e-9));
        camera.recenter();
        assert(camera.update(vehicle(120.0, 10.0), 0.016).following);
    }
    {
        // User zoom offset persists while following and is bounded.
        auto camera = controller();
        (void)camera.update(vehicle(0.0, 0.0), 0.016);
        camera.zoom_by(1.0f);
        for (int i = 0; i < 200; ++i)
            (void)camera.update(vehicle(0.0, 0.0), 0.05);
        const auto zoomed = camera.update(vehicle(0.0, 0.0), 0.05);
        assert(near(zoomed.zoom, settings.follow_base_zoom + 1.0f, 0.05));
        for (int i = 0; i < 20; ++i) camera.zoom_by(1.0f);
        assert(camera.user_zoom_offset() <= 3.0f);
    }
    {
        // Panning frees the camera without jumping, then auto-recenters.
        auto camera = controller();
        const auto start = camera.update(vehicle(0.0, 10.0), 0.016);
        camera.pan(0, 0);
        assert(camera.mode() == CameraController::Mode::HeadingUp);
        camera.pan(40, 0);
        assert(camera.mode() == CameraController::Mode::Free);
        const auto free = camera.update(vehicle(0.0, 10.0), 0.016);
        assert(!free.following);
        // Heading 0: dragging content right moves the camera west.
        assert(free.longitude < start.longitude);
        // The free camera centre started at the old map-area centre, which is
        // north of the heading-up vehicle anchor.
        assert(free.latitude > start.latitude);
        assert(near(free.zoom, start.zoom, 1e-3));
        for (int i = 0; i < 9; ++i)
            (void)camera.update(vehicle(0.0, 10.0), 1.0);
        assert(camera.mode() == CameraController::Mode::Free);
        (void)camera.update(vehicle(0.0, 10.0), 1.0);
        assert(camera.mode() == CameraController::Mode::HeadingUp);
        // Without a vehicle the free camera stays put.
        camera.pan(10, 10);
        CameraController::Vehicle none;
        for (int i = 0; i < 20; ++i)
            (void)camera.update(none, 1.0);
        assert(camera.mode() == CameraController::Mode::Free);
    }
    {
        // Overview fits the box north-up, then recenter returns to follow.
        auto camera = controller();
        (void)camera.update(vehicle(45.0, 10.0), 0.016);
        camera.overview(50.40, 30.45, 50.47, 30.60);
        CameraController::Pose pose;
        for (int i = 0; i < 8; ++i)  // zoom animates out, within the timeout
            pose = camera.update(vehicle(45.0, 10.0), 1.0);
        assert(camera.mode() == CameraController::Mode::Overview);
        assert(!pose.following);
        assert(near(pose.latitude, 50.435, 1e-9));
        assert(pose.zoom > 10.0f && pose.zoom < 14.0f);
        camera.recenter();
        assert(camera.mode() == CameraController::Mode::HeadingUp);
        // Invalid boxes are ignored.
        camera.overview(1.0, 1.0, 0.0, 0.0);
        assert(camera.mode() == CameraController::Mode::HeadingUp);
    }
    {
        // Zoom jumps are eased transitions that arrive in zoom_transition_s
        // (no exponential tail) and hold the coarser tile zoom meanwhile.
        auto camera = controller();
        (void)camera.update(vehicle(45.0, 0.0), 0.016);
        camera.overview(50.30, 30.30, 50.60, 30.80);
        const auto first = camera.update(vehicle(45.0, 0.0), 0.016);
        assert(camera.zoom_transition());
        assert(first.zoom < settings.follow_base_zoom && first.zoom > 16.9f);
        const float target = first.tile_zoom;
        assert(target > 0.0f && target < 13.0f);
        CameraController::Pose pose = first;
        double elapsed = 0.016;
        while (camera.zoom_transition() && elapsed < 2.0) {
            pose = camera.update(vehicle(45.0, 0.0), 0.016);
            elapsed += 0.016;
            if (camera.zoom_transition())
                assert(near(pose.tile_zoom, target, 1e-4));
        }
        assert(near(elapsed, settings.zoom_transition_s + 0.016, 0.02));
        assert(near(pose.zoom, target, 1e-4) && pose.tile_zoom < 0.0f);
        // Recenter eases back in; the tile zoom stays at the overview's.
        camera.recenter();
        pose = camera.update(vehicle(45.0, 0.0), 0.016);
        assert(camera.zoom_transition() && near(pose.tile_zoom, target, 1e-4));
        for (int i = 0; i < 40; ++i)
            pose = camera.update(vehicle(45.0, 0.0), 0.016);
        assert(!camera.zoom_transition());
        assert(near(pose.zoom, settings.follow_base_zoom, 1e-4));
        // A zoom button is a transition too, and arrives exactly.
        camera.zoom_by(1.0f);
        for (int i = 0; i < 40; ++i)
            pose = camera.update(vehicle(45.0, 0.0), 0.016);
        assert(near(pose.zoom, settings.follow_base_zoom + 1.0f, 1e-4));
    }
    {
        // Orientation also turns a dragged map and one without a vehicle.
        auto camera = controller();
        (void)camera.update(vehicle(90.0, 10.0), 0.016);  // heading-up, east
        camera.begin_free(50.45, 30.52);
        CameraController::Pose pose;
        for (int i = 0; i < 100; ++i)
            pose = camera.update(vehicle(90.0, 10.0), 0.05);
        assert(near(pose.azimuth_degrees, 90.0, 0.1));  // free keeps rotation
        camera.toggle_orientation();                    // north-up
        assert(camera.mode() == CameraController::Mode::Free);
        for (int i = 0; i < 100; ++i)
            pose = camera.update(vehicle(90.0, 10.0), 0.05);
        assert(near(CameraController::shortest_angle_delta(pose.azimuth_degrees, 0.0), 0.0, 0.1));
        camera.toggle_orientation();                    // heading-up again
        for (int i = 0; i < 100; ++i)
            pose = camera.update(vehicle(90.0, 10.0), 0.05);
        assert(near(pose.azimuth_degrees, 90.0, 0.1));

        // No vehicle: north-up turns the map north; heading-up with no
        // heading ever known keeps it where it is.
        auto bench = controller();
        CameraController::Vehicle none;
        (void)bench.update(none, 0.016);
        bench.toggle_orientation();
        assert(bench.mode() == CameraController::Mode::NorthUp);
        pose = bench.update(none, 0.05);
        assert(near(pose.azimuth_degrees, 0.0, 1e-3));
        // Zoom buttons still zoom the map without a vehicle.
        const float before = pose.zoom;
        bench.zoom_by(1.0f);
        for (int i = 0; i < 40; ++i)
            pose = bench.update(none, 0.016);
        assert(near(pose.zoom, before + 1.0f, 1e-4));

        // A lost fix after driving: heading-up uses the last heading.
        auto lost = controller();
        lost.toggle_orientation();                      // north-up
        (void)lost.update(vehicle(120.0, 10.0), 0.016);
        lost.toggle_orientation();                      // heading-up
        for (int i = 0; i < 100; ++i)
            pose = lost.update(none, 0.05);
        assert(near(pose.azimuth_degrees, 120.0, 0.1));
    }
    {
        // Double tap: zoom in around the tapped ground point. Free camera:
        // the point keeps its screen position through the whole transition.
        auto camera = controller();
        CameraController::Vehicle none;
        camera.begin_free(50.45, 30.52);
        auto pose = camera.update(none, 0.016);
        const float z0 = pose.zoom;
        const double at_lat = 50.46, at_lon = 30.50;
        camera.zoom_at(1.0f, at_lat, at_lon);
        bool checked_middle = false;
        for (int i = 0; i < 60; ++i) {
            pose = camera.update(none, 0.016);
            const double scale = std::pow(2.0, static_cast<double>(z0 - pose.zoom));
            assert(near(pose.latitude, at_lat + (50.45 - at_lat) * scale, 1e-9));
            assert(near(pose.longitude, at_lon + (30.52 - at_lon) * scale, 1e-9));
            checked_middle = checked_middle || (pose.zoom > z0 + 0.3f && pose.zoom < z0 + 0.7f);
        }
        assert(checked_middle && near(pose.zoom, z0 + 1.0f, 1e-4));
        assert(near(pose.latitude, (at_lat + 50.45) / 2.0, 1e-9));
        // A drag afterwards is not pulled back to the anchor.
        camera.set_free_location(50.40, 30.40);
        pose = camera.update(none, 0.016);
        assert(near(pose.latitude, 50.40, 1e-9));

        // Following a vehicle it zooms like the + button and keeps following.
        auto following = controller();
        (void)following.update(vehicle(0.0, 0.0), 0.016);
        following.zoom_at(1.0f, 50.46, 30.50);
        for (int i = 0; i < 60; ++i)
            pose = following.update(vehicle(0.0, 0.0), 0.016);
        assert(pose.following && near(pose.latitude, 50.45, 1e-9));
        assert(near(pose.zoom, settings.follow_base_zoom + 1.0f, 1e-4));
        assert(following.user_zoom_offset() == 1.0f);

        // No vehicle in a follow mode: the view becomes free, and zooms there.
        auto bench = controller();
        (void)bench.update(none, 0.016);
        assert(!bench.heading_known());
        bench.zoom_at(1.0f, 50.0, 30.0);
        assert(bench.mode() == CameraController::Mode::Free);
    }
    {
        // 3D angle: flatter (lower elevation) with speed, steeper and a
        // little closer approaching a turn, more for a tight one, and
        // easing back after it.
        const auto settle = [](CameraController& camera, double speed) {
            CameraController::Pose pose;
            for (int i = 0; i < 600; ++i)
                pose = camera.update(vehicle(0.0, speed), 0.016);
            return pose;
        };
        auto camera = controller();
        camera.set_tilt(true);
        const auto slow = settle(camera, 5.0);
        assert(near(slow.elevation_degrees, settings.tilted_elevation, 0.01));
        const auto fast = settle(camera, 30.0);
        assert(near(fast.elevation_degrees, settings.tilted_elevation_fast, 0.01));
        assert(fast.anchor_y > slow.anchor_y);  // flatter: car lower

        camera.set_maneuver(true, 400.0, false);  // at the countdown start
        const auto far = settle(camera, 5.0);
        assert(near(far.elevation_degrees, settings.tilted_elevation, 0.05));
        camera.set_maneuver(true, 100.0, false);
        const auto near_turn = settle(camera, 5.0);
        assert(near(near_turn.elevation_degrees,
                    settings.tilted_elevation + settings.maneuver_elevation * 0.75f, 0.05));
        assert(near(near_turn.zoom,
                    settings.follow_base_zoom + settings.maneuver_zoom * 0.75f, 0.01));
        camera.set_maneuver(true, 100.0, true);    // roundabout
        const auto tight = settle(camera, 5.0);
        assert(tight.elevation_degrees > near_turn.elevation_degrees);
        // Past the turn the next one is far: eases back, no jump.
        camera.set_maneuver(true, 2000.0, false);
        const auto after = camera.update(vehicle(0.0, 5.0), 0.016);
        assert(std::fabs(after.elevation_degrees - tight.elevation_degrees) < 1.0f);
        const auto relaxed = settle(camera, 5.0);
        assert(near(relaxed.elevation_degrees, settings.tilted_elevation, 0.05));

        // 2D ignores the angle logic; the turn zoom still applies.
        camera.set_tilt(false);
        camera.set_maneuver(true, 0.0, false);
        const auto flat = settle(camera, 5.0);
        assert(flat.elevation_degrees == 90.0f);
        assert(near(flat.zoom, settings.follow_base_zoom + settings.maneuver_zoom, 0.01));
    }
    {
        // 1 Hz fixes heading east at ~20 m/s: heading/speed are derived and
        // the position is extrapolated between fixes, not stepped.
        MotionEstimator motion;
        assert(!motion.predict(0.0).valid);
        const double metres_per_degree_lon =
            111320.0 * std::cos(50.45 * 3.14159265358979323846 / 180.0);
        for (int second = 0; second <= 6; ++second)
            motion.observe(50.45, 30.52 + 20.0 * second / metres_per_degree_lon,
                           second);
        const auto at_fix = motion.predict(6.0);
        assert(at_fix.valid);
        assert(near(at_fix.heading_degrees, 90.0, 1.0));
        assert(near(at_fix.speed_mps, 20.0, 1.5));
        const auto between = motion.predict(6.5);
        assert(between.longitude > at_fix.longitude);
        assert(near((between.longitude - at_fix.longitude) * metres_per_degree_lon,
                    0.5 * at_fix.speed_mps, 0.5));
        // Extrapolation is capped, and old fixes become invalid.
        const auto capped = motion.predict(9.0);
        assert(near((capped.longitude - at_fix.longitude) * metres_per_degree_lon,
                    1.5 * at_fix.speed_mps, 0.5));
        assert(!motion.predict(12.0).valid);
        // GPS jitter below the threshold keeps the heading; standing still
        // decays speed.
        for (int second = 7; second <= 14; ++second)
            motion.observe(50.45 + (second % 2) * 5e-6,
                           30.52 + 120.0 / metres_per_degree_lon, second);
        const auto parked = motion.predict(14.0);
        assert(near(parked.heading_degrees, 90.0, 1.0));
        assert(parked.speed_mps < 2.0);
        motion.reset();
        assert(!motion.predict(14.0).valid);
    }
    return 0;
}
