#pragma once

#include <cstdint>

namespace navicore_osmand {

// Car-oriented map camera. Pure math with no OsmAnd or GL dependency so it is
// host-testable; the engine applies the resulting pose to the renderer.
//
// Follow modes keep the vehicle at a fixed screen anchor: heading-up puts it in
// the lower third of the map area so more road ahead is visible, north-up
// centres it. A manual pan switches to Free; following resumes after an idle
// timeout or an explicit recenter. Zoom and azimuth approach their targets
// exponentially so per-frame jitter in position or speed never reaches the
// screen.
class CameraController {
public:
    enum class Mode { HeadingUp, NorthUp, Free, Overview };

    struct Viewport {
        // Map area inside the render window (excludes opaque UI rails).
        int left = 0;
        int top = 0;
        int width = 1;
        int height = 1;
    };

    struct Vehicle {
        bool valid = false;
        double latitude = 0.0;
        double longitude = 0.0;
        double heading_degrees = 0.0;   // 0 = north, clockwise
        double speed_mps = 0.0;
    };

    struct Pose {
        double latitude = 0.0;          // location placed at (anchor_x, anchor_y)
        double longitude = 0.0;
        int anchor_x = 0;
        int anchor_y = 0;
        float zoom = 16.0f;
        float azimuth_degrees = 0.0f;   // map rotation, 0 = north up
        float elevation_degrees = 90.0f;// 90 = flat top-down
        bool following = true;
    };

    struct Settings {
        float min_zoom = 3.0f;
        float max_zoom = 18.5f;
        float follow_base_zoom = 17.0f;  // zoom at or below slow_speed
        float slow_speed_mps = 8.0f;     // ~30 km/h
        float zoom_per_doubling = 0.75f; // zoom out per doubling of speed
        float min_speed_zoom = 14.5f;
        float tilted_elevation = 60.0f;
        float zoom_time_constant_s = 1.2f;
        float azimuth_time_constant_s = 0.6f;
        float heading_min_speed_mps = 1.5f; // below this keep last azimuth
        float recenter_after_s = 10.0f;
        float heading_up_anchor = 0.70f;    // fraction of map height from top
    };

    CameraController() = default;
    explicit CameraController(Settings settings) : settings_(settings) {}

    void set_viewport(const Viewport& viewport) noexcept { viewport_ = viewport; }
    const Viewport& viewport() const noexcept { return viewport_; }

    // User commands.
    void zoom_by(float steps) noexcept;
    void toggle_orientation() noexcept;  // heading-up <-> north-up
    void set_tilt(bool tilted) noexcept { tilted_ = tilted; }
    bool tilted() const noexcept { return tilted_; }
    void recenter() noexcept;
    // Pans the free camera by a screen delta in pixels.
    void pan(int dx, int dy) noexcept;
    // Free camera centred on a location the engine computed (finger drag):
    // keeps the current zoom and tilt. set_free_location moves it.
    void begin_free(double latitude, double longitude) noexcept;
    void set_free_location(double latitude, double longitude) noexcept;
    // Fits a lat/lon bounding box (route overview) until recenter/timeout.
    void overview(double min_latitude, double min_longitude,
                  double max_latitude, double max_longitude) noexcept;

    Mode mode() const noexcept { return mode_; }
    Mode follow_mode() const noexcept { return follow_mode_; }
    float user_zoom_offset() const noexcept { return user_zoom_offset_; }

    // Advances smoothing by `dt_seconds` and returns the pose to apply.
    Pose update(const Vehicle& vehicle, double dt_seconds) noexcept;

    // Exposed for tests and the vehicle marker.
    static float speed_zoom(const Settings& settings, double speed_mps) noexcept;
    static double shortest_angle_delta(double from, double to) noexcept;

private:
    Settings settings_{};
    Viewport viewport_{};
    Mode mode_ = Mode::HeadingUp;
    Mode follow_mode_ = Mode::HeadingUp;
    bool tilted_ = false;
    bool initialised_ = false;
    float zoom_ = 16.0f;
    float azimuth_ = 0.0f;
    float user_zoom_offset_ = 0.0f;
    double free_latitude_ = 0.0;
    double free_longitude_ = 0.0;
    float free_zoom_ = 16.0f;
    double idle_seconds_ = 0.0;
    double last_heading_ = 0.0;
    int last_anchor_x_ = 0;
    int last_anchor_y_ = 0;
    float last_elevation_ = 90.0f;
    bool free_tilted_ = false;  // a drag from a tilted view stays tilted
};

// Turns sparse position fixes (1 Hz GPS or per-frame simulation) into a
// smooth per-frame vehicle state. Heading and speed come from successive
// fixes; between fixes the position is extrapolated along the velocity for at
// most `max_extrapolation_s`, so a 1 Hz receiver does not step the camera.
class MotionEstimator {
public:
    struct Settings {
        double min_heading_distance_m = 2.0; // ignore jitter below this
        double speed_time_constant_s = 1.0;
        double max_extrapolation_s = 1.5;
        double stale_after_s = 5.0;          // no fix for this long = invalid
    };

    MotionEstimator() = default;
    explicit MotionEstimator(Settings settings) : settings_(settings) {}

    void reset() noexcept;
    void observe(double latitude, double longitude, double time_s) noexcept;
    CameraController::Vehicle predict(double time_s) const noexcept;

private:
    Settings settings_{};
    bool has_fix_ = false;
    double latitude_ = 0.0;
    double longitude_ = 0.0;
    double fix_time_ = 0.0;
    double heading_ = 0.0;
    double speed_ = 0.0;
    double east_mps_ = 0.0;
    double north_mps_ = 0.0;
    bool has_anchor_ = false;
    double anchor_latitude_ = 0.0;   // last fix that changed the heading
    double anchor_longitude_ = 0.0;
    double anchor_time_ = 0.0;
};

} // namespace navicore_osmand
