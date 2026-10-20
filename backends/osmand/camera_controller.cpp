#include "camera_controller.h"

#include <algorithm>
#include <cmath>

namespace navicore_osmand {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double radians = pi / 180.0;
// Web-Mercator metres per pixel at zoom 0 for 256-pixel tiles at the equator.
constexpr double metres_per_pixel_z0 = 156543.03392804097;
constexpr double metres_per_degree = 111320.0;
constexpr float max_user_zoom_offset = 3.0f;
constexpr float min_user_zoom_offset = -5.0f;

double metres_per_pixel(double latitude, double zoom)
{
    return metres_per_pixel_z0 * std::cos(latitude * radians) /
           std::pow(2.0, zoom);
}

double smoothing(double dt, double time_constant)
{
    if (time_constant <= 0.0 || dt <= 0.0)
        return 1.0;
    return 1.0 - std::exp(-dt / time_constant);
}

double normalise_degrees(double value)
{
    value = std::fmod(value, 360.0);
    return value < 0.0 ? value + 360.0 : value;
}

// Moves a location by a screen-space pixel offset (x right, y down) for a map
// rotated so that screen-up points at `azimuth` degrees.
void offset_location(double& latitude, double& longitude, double dx, double dy,
                     double zoom, double azimuth)
{
    const double mpp = metres_per_pixel(latitude, zoom);
    const double a = azimuth * radians;
    const double east = (dx * std::cos(a) - dy * std::sin(a)) * mpp;
    const double north = (-dx * std::sin(a) - dy * std::cos(a)) * mpp;
    latitude += north / metres_per_degree;
    const double cos_latitude = std::max(0.01, std::cos(latitude * radians));
    longitude += east / (metres_per_degree * cos_latitude);
    latitude = std::clamp(latitude, -85.0, 85.0);
    if (longitude > 180.0) longitude -= 360.0;
    if (longitude < -180.0) longitude += 360.0;
}
} // namespace

float CameraController::speed_zoom(const Settings& settings,
                                   double speed_mps) noexcept
{
    const double speed = std::max(0.0, speed_mps);
    if (speed <= settings.slow_speed_mps)
        return settings.follow_base_zoom;
    const double doublings = std::log2(speed / settings.slow_speed_mps);
    const double zoom = settings.follow_base_zoom -
                        doublings * settings.zoom_per_doubling;
    return static_cast<float>(std::max<double>(settings.min_speed_zoom, zoom));
}

double CameraController::shortest_angle_delta(double from, double to) noexcept
{
    double delta = std::fmod(to - from, 360.0);
    if (delta > 180.0) delta -= 360.0;
    if (delta < -180.0) delta += 360.0;
    return delta;
}

void CameraController::zoom_by(float steps) noexcept
{
    idle_seconds_ = 0.0;
    // Without a vehicle a follow mode shows the free view; zoom that.
    if (mode_ == Mode::Free || mode_ == Mode::Overview || !following_vehicle_) {
        free_zoom_ = std::clamp(free_zoom_ + steps, settings_.min_zoom,
                                settings_.max_zoom);
        if (mode_ == Mode::Overview)
            mode_ = Mode::Free;
        return;
    }
    user_zoom_offset_ = std::clamp(user_zoom_offset_ + steps,
                                   min_user_zoom_offset, max_user_zoom_offset);
}

void CameraController::toggle_orientation() noexcept
{
    follow_mode_ = follow_mode_ == Mode::HeadingUp ? Mode::NorthUp
                                                   : Mode::HeadingUp;
    if (mode_ == Mode::HeadingUp || mode_ == Mode::NorthUp)
        mode_ = follow_mode_;
    else if (mode_ == Mode::Free)
        free_azimuth_ = follow_mode_ == Mode::NorthUp || !has_heading_
            ? 0.0f : static_cast<float>(last_heading_);
}

void CameraController::zoom_at(float steps, double latitude, double longitude) noexcept
{
    if (following_vehicle_ && (mode_ == Mode::HeadingUp || mode_ == Mode::NorthUp)) {
        zoom_by(steps);
        return;
    }
    idle_seconds_ = 0.0;
    if (mode_ != Mode::Free) {
        // Overview, or a follow mode without a vehicle: it shows the free
        // centre already; keep the view and its rotation.
        free_azimuth_ = azimuth_;
        free_tilted_ = last_elevation_ < 90.0f;
        free_elevation_ = free_tilted_ ? last_elevation_ : settings_.tilted_elevation;
        mode_ = Mode::Free;
    }
    anchored_ = true;
    anchor_latitude_ = latitude;
    anchor_longitude_ = longitude;
    anchor_centre_latitude_ = free_latitude_;
    anchor_centre_longitude_ = free_longitude_;
    anchor_zoom_ = zoom_;
    free_zoom_ = std::clamp(zoom_ + steps, settings_.min_zoom, settings_.max_zoom);
}

void CameraController::recenter() noexcept
{
    mode_ = follow_mode_;
    idle_seconds_ = 0.0;
    anchored_ = false;
}

void CameraController::set_resting_location(double latitude, double longitude) noexcept
{
    if (mode_ != Mode::HeadingUp && mode_ != Mode::NorthUp) return;
    free_latitude_ = latitude;
    free_longitude_ = longitude;
}

void CameraController::begin_free(double latitude, double longitude) noexcept
{
    idle_seconds_ = 0.0;
    anchored_ = false;
    if (mode_ != Mode::Free) {
        free_zoom_ = zoom_;
        free_azimuth_ = azimuth_;
        free_tilted_ = last_elevation_ < 90.0f;
        free_elevation_ = free_tilted_ ? last_elevation_ : settings_.tilted_elevation;
        mode_ = Mode::Free;
    }
    free_latitude_ = latitude;
    free_longitude_ = longitude;
}

void CameraController::set_free_location(double latitude, double longitude) noexcept
{
    idle_seconds_ = 0.0;
    anchored_ = false;
    free_latitude_ = latitude;
    free_longitude_ = longitude;
}

void CameraController::set_free_view(float zoom, float azimuth_degrees) noexcept
{
    if (mode_ != Mode::Free)
        return;
    idle_seconds_ = 0.0;
    anchored_ = false;
    transition_ = false;
    free_zoom_ = std::clamp(zoom, settings_.min_zoom, settings_.max_zoom);
    free_azimuth_ = static_cast<float>(normalise_degrees(azimuth_degrees));
    direct_ = true;
}

void CameraController::pan(int dx, int dy) noexcept
{
    idle_seconds_ = 0.0;
    if (dx == 0 && dy == 0)
        return;
    anchored_ = false;
    if (mode_ != Mode::Free) {
        free_tilted_ = false;
        free_azimuth_ = azimuth_;
        if (mode_ != Mode::Overview) {
            // Start from whatever is currently at the map-area centre so the
            // first drag does not jump the view.
            free_zoom_ = zoom_;
            const double centre_x = viewport_.left + viewport_.width / 2.0;
            const double centre_y = viewport_.top + viewport_.height / 2.0;
            offset_location(free_latitude_, free_longitude_,
                            centre_x - last_anchor_x_, centre_y - last_anchor_y_,
                            zoom_, azimuth_);
        }
        mode_ = Mode::Free;
    }
    // Dragging the content right moves the camera left.
    offset_location(free_latitude_, free_longitude_, -dx, -dy, free_zoom_,
                    azimuth_);
}

void CameraController::overview(double min_latitude, double min_longitude,
                                double max_latitude,
                                double max_longitude) noexcept
{
    if (max_latitude < min_latitude || max_longitude < min_longitude)
        return;
    idle_seconds_ = 0.0;
    anchored_ = false;
    free_latitude_ = (min_latitude + max_latitude) * 0.5;
    free_longitude_ = (min_longitude + max_longitude) * 0.5;
    const double cos_latitude =
        std::max(0.01, std::cos(free_latitude_ * radians));
    const double width_m = std::max(
        50.0, (max_longitude - min_longitude) * metres_per_degree * cos_latitude);
    const double height_m =
        std::max(50.0, (max_latitude - min_latitude) * metres_per_degree);
    // Leave a 12% margin on each side of the map area.
    const double usable_w = std::max(1.0, viewport_.width * 0.76);
    const double usable_h = std::max(1.0, viewport_.height * 0.76);
    const double mpp0 = metres_per_pixel_z0 * cos_latitude;
    const double zoom_w = std::log2(mpp0 * usable_w / width_m);
    const double zoom_h = std::log2(mpp0 * usable_h / height_m);
    free_zoom_ = std::clamp(static_cast<float>(std::min(zoom_w, zoom_h)),
                            settings_.min_zoom, settings_.max_zoom);
    mode_ = Mode::Overview;
}

void CameraController::set_maneuver(bool active, double distance_metres, bool tight) noexcept
{
    maneuver_active_ = active;
    maneuver_distance_m_ = std::max(0.0, distance_metres);
    maneuver_tight_ = tight;
}

float CameraController::dynamic_elevation(double speed_mps) const noexcept
{
    const double range = settings_.fast_speed_mps - settings_.slow_speed_mps;
    const float fast = range > 0.0
        ? static_cast<float>(std::clamp((speed_mps - settings_.slow_speed_mps) / range, 0.0, 1.0))
        : 0.0f;
    float elevation = settings_.tilted_elevation +
                      (settings_.tilted_elevation_fast - settings_.tilted_elevation) * fast;
    elevation += (maneuver_tight_ ? settings_.tight_elevation : settings_.maneuver_elevation) *
                 maneuver_factor_;
    return std::min(elevation, settings_.max_tilted_elevation);
}

CameraController::Pose CameraController::update(const Vehicle& vehicle,
                                                double dt_seconds) noexcept
{
    dt_seconds = std::clamp(dt_seconds, 0.0, 1.0);
    Pose pose;
    const bool following = mode_ == Mode::HeadingUp || mode_ == Mode::NorthUp;
    if (!following) {
        idle_seconds_ += dt_seconds;
        if (vehicle.valid && idle_seconds_ >= settings_.recenter_after_s)
            recenter();
    }
    const bool follow = (mode_ == Mode::HeadingUp || mode_ == Mode::NorthUp) &&
                        vehicle.valid;
    following_vehicle_ = follow;

    if (vehicle.valid && vehicle.speed_mps >= settings_.heading_min_speed_mps) {
        last_heading_ = normalise_degrees(vehicle.heading_degrees);
        has_heading_ = true;
    }

    // 0 far from the next turn, 1 at it; eased, so passing the turn (the
    // distance jumps to the next one) relaxes smoothly.
    const double maneuver_target = maneuver_active_ && settings_.maneuver_distance_m > 0.0f
        ? 1.0 - std::clamp(maneuver_distance_m_ / settings_.maneuver_distance_m, 0.0, 1.0)
        : 0.0;
    maneuver_factor_ += static_cast<float>(
        (maneuver_target - maneuver_factor_) *
        smoothing(dt_seconds, settings_.maneuver_time_constant_s));

    float target_zoom;
    float target_azimuth;
    if (follow) {
        target_zoom = std::clamp(
            speed_zoom(settings_, vehicle.speed_mps) + user_zoom_offset_ +
                settings_.maneuver_zoom * maneuver_factor_,
            settings_.min_zoom, settings_.max_zoom);
        target_azimuth = mode_ == Mode::HeadingUp
            ? static_cast<float>(last_heading_) : 0.0f;
    } else if (mode_ == Mode::Overview) {
        target_zoom = free_zoom_;
        target_azimuth = 0.0f;
    } else if (mode_ == Mode::Free) {
        target_zoom = free_zoom_;
        target_azimuth = free_azimuth_;
    } else {
        // A follow mode without a vehicle keeps the view where it is but
        // still shows the chosen orientation.
        target_zoom = free_zoom_;
        target_azimuth = mode_ == Mode::NorthUp ? 0.0f
            : has_heading_ ? static_cast<float>(last_heading_) : azimuth_;
    }

    if (!initialised_ || (direct_ && mode_ == Mode::Free)) {
        zoom_ = target_zoom;
        azimuth_ = target_azimuth;
        initialised_ = true;
    } else {
        if (std::fabs(target_zoom - last_target_zoom_) > settings_.zoom_jump &&
            std::fabs(target_zoom - zoom_) > 0.05f) {
            transition_ = true;
            transition_from_ = zoom_;
            transition_t_ = 0.0;
        }
        if (transition_) {
            // Ease in and out to the (possibly moving) target, then arrive.
            transition_t_ += dt_seconds;
            const double t = settings_.zoom_transition_s > 0.0f
                ? std::min(1.0, transition_t_ / settings_.zoom_transition_s) : 1.0;
            const double eased = t * t * (3.0 - 2.0 * t);
            zoom_ = static_cast<float>(transition_from_ +
                                       (target_zoom - transition_from_) * eased);
            if (t >= 1.0)
                transition_ = false;
        } else if (mode_ == Mode::Free) {
            // A free camera is driven directly by the user; do not lag.
            zoom_ = target_zoom;
        } else {
            const double zoom_alpha = smoothing(dt_seconds,
                                                settings_.zoom_time_constant_s);
            zoom_ += static_cast<float>((target_zoom - zoom_) * zoom_alpha);
        }
        const double azimuth_alpha = smoothing(dt_seconds,
                                               settings_.azimuth_time_constant_s);
        azimuth_ = static_cast<float>(normalise_degrees(
            azimuth_ + shortest_angle_delta(azimuth_, target_azimuth) *
                           azimuth_alpha));
    }
    last_target_zoom_ = target_zoom;
    direct_ = false;
    if (anchored_ && mode_ == Mode::Free) {
        // Scaling about the anchor keeps it on the same screen point.
        const double scale = std::pow(2.0, static_cast<double>(anchor_zoom_ - zoom_));
        free_latitude_ = anchor_latitude_ +
                         (anchor_centre_latitude_ - anchor_latitude_) * scale;
        free_longitude_ = anchor_longitude_ +
                          (anchor_centre_longitude_ - anchor_longitude_) * scale;
        if (!transition_)
            anchored_ = false;
    }

    // 3D: heading-up following tilts by speed and towards a coming turn;
    // a free camera keeps the tilt it was dragged at; everything else is
    // flat. The angle eases like zoom and azimuth (also for 2D <-> 3D).
    const bool dynamic_tilt = tilted_ && follow && mode_ == Mode::HeadingUp;
    float target_elevation = 90.0f;
    if (dynamic_tilt)
        target_elevation = dynamic_elevation(vehicle.speed_mps);
    else if (tilted_ && mode_ == Mode::Free && free_tilted_)
        target_elevation = free_elevation_;
    if (!elevation_initialised_) {
        elevation_ = target_elevation;
        elevation_initialised_ = true;
    } else {
        elevation_ += static_cast<float>((target_elevation - elevation_) *
            smoothing(dt_seconds, settings_.elevation_time_constant_s));
        if (std::fabs(target_elevation - elevation_) < 0.05f)
            elevation_ = target_elevation;
    }

    const int centre_x = viewport_.left + viewport_.width / 2;
    const int centre_y = viewport_.top + viewport_.height / 2;
    if (follow) {
        pose.latitude = vehicle.latitude;
        pose.longitude = vehicle.longitude;
        // The flatter the view, the lower the car, so the road ahead gets
        // the space: the 2D anchor down to 60 degrees, then lower to 40.
        const float flat = std::clamp((60.0f - elevation_) / 20.0f, 0.0f, 1.0f);
        const float anchor = settings_.heading_up_anchor +
            (settings_.heading_up_anchor_flat - settings_.heading_up_anchor) * flat;
        pose.anchor_x = mode_ == Mode::HeadingUp
            ? centre_x + settings_.heading_up_anchor_dx : centre_x;
        pose.anchor_y = mode_ == Mode::HeadingUp
            ? viewport_.top + static_cast<int>(viewport_.height * anchor)
            : centre_y;
        free_latitude_ = vehicle.latitude;
        free_longitude_ = vehicle.longitude;
    } else {
        pose.latitude = free_latitude_;
        pose.longitude = free_longitude_;
        pose.anchor_x = centre_x;
        pose.anchor_y = centre_y;
    }
    pose.zoom = zoom_;
    pose.tile_zoom = transition_ ? std::min(transition_from_, target_zoom) : -1.0f;
    pose.azimuth_degrees = azimuth_;
    pose.elevation_degrees = elevation_;
    last_elevation_ = pose.elevation_degrees;
    pose.following = follow;
    last_anchor_x_ = pose.anchor_x;
    last_anchor_y_ = pose.anchor_y;
    return pose;
}

void MotionEstimator::reset() noexcept
{
    *this = MotionEstimator(settings_);
}

void MotionEstimator::observe(double latitude, double longitude,
                              double time_s) noexcept
{
    if (!has_fix_ || !has_anchor_ || time_s < fix_time_) {
        has_fix_ = true;
        has_anchor_ = true;
        latitude_ = anchor_latitude_ = latitude;
        longitude_ = anchor_longitude_ = longitude;
        fix_time_ = anchor_time_ = time_s;
        speed_ = east_mps_ = north_mps_ = 0.0;
        return;
    }
    const double cos_latitude = std::max(0.01, std::cos(latitude * radians));
    const double east = (longitude - anchor_longitude_) * metres_per_degree *
                        cos_latitude;
    const double north = (latitude - anchor_latitude_) * metres_per_degree;
    const double distance = std::hypot(east, north);
    const double dt = time_s - anchor_time_;
    if (distance >= settings_.min_heading_distance_m && dt > 0.0) {
        heading_ = normalise_degrees(std::atan2(east, north) / radians);
        const double instantaneous = distance / dt;
        speed_ += (instantaneous - speed_) *
                  smoothing(dt, settings_.speed_time_constant_s);
        anchor_latitude_ = latitude;
        anchor_longitude_ = longitude;
        anchor_time_ = time_s;
    } else if (dt > settings_.speed_time_constant_s) {
        // Not moving far enough: decay toward standstill.
        speed_ *= 1.0 - smoothing(dt - settings_.speed_time_constant_s,
                                  settings_.speed_time_constant_s);
    }
    east_mps_ = speed_ * std::sin(heading_ * radians);
    north_mps_ = speed_ * std::cos(heading_ * radians);
    latitude_ = latitude;
    longitude_ = longitude;
    fix_time_ = time_s;
}

CameraController::Vehicle MotionEstimator::predict(double time_s) const noexcept
{
    CameraController::Vehicle vehicle;
    if (!has_fix_ || time_s - fix_time_ > settings_.stale_after_s)
        return vehicle;
    const double ahead = std::clamp(time_s - fix_time_, 0.0,
                                    settings_.max_extrapolation_s);
    const double cos_latitude = std::max(0.01, std::cos(latitude_ * radians));
    vehicle.valid = true;
    vehicle.latitude = latitude_ + north_mps_ * ahead / metres_per_degree;
    vehicle.longitude = longitude_ +
        east_mps_ * ahead / (metres_per_degree * cos_latitude);
    vehicle.heading_degrees = heading_;
    vehicle.speed_mps = speed_;
    return vehicle;
}

} // namespace navicore_osmand
