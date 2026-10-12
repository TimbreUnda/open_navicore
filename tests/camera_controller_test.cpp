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
        assert(pose.anchor_x == 336 + 688 / 2);
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
        const auto tilted = camera.update(vehicle(120.0, 10.0), 0.016);
        assert(tilted.elevation_degrees == settings.tilted_elevation);
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
