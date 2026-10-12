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
    if (mode_ == Mode::Free || mode_ == Mode::Overview) {
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
}

void CameraController::recenter() noexcept
{
    mode_ = follow_mode_;
    idle_seconds_ = 0.0;
}

void CameraController::pan(int dx, int dy) noexcept
{
    idle_seconds_ = 0.0;
    if (dx == 0 && dy == 0)
        return;
    if (mode_ != Mode::Free) {
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

    if (vehicle.valid && vehicle.speed_mps >= settings_.heading_min_speed_mps)
        last_heading_ = normalise_degrees(vehicle.heading_degrees);

    float target_zoom;
    float target_azimuth;
    if (follow) {
        target_zoom = std::clamp(
            speed_zoom(settings_, vehicle.speed_mps) + user_zoom_offset_,
            settings_.min_zoom, settings_.max_zoom);
        target_azimuth = mode_ == Mode::HeadingUp
            ? static_cast<float>(last_heading_) : 0.0f;
    } else {
        target_zoom = free_zoom_;
        target_azimuth = mode_ == Mode::Overview ? 0.0f : azimuth_;
    }

    if (!initialised_) {
        zoom_ = target_zoom;
        azimuth_ = target_azimuth;
        initialised_ = true;
    } else {
        const double zoom_alpha = smoothing(dt_seconds,
                                            settings_.zoom_time_constant_s);
        zoom_ += static_cast<float>((target_zoom - zoom_) * zoom_alpha);
        const double azimuth_alpha = smoothing(dt_seconds,
                                               settings_.azimuth_time_constant_s);
        azimuth_ = static_cast<float>(normalise_degrees(
            azimuth_ + shortest_angle_delta(azimuth_, target_azimuth) *
                           azimuth_alpha));
    }
    // A free camera is driven directly by the user; do not lag behind it.
    if (mode_ == Mode::Free)
        zoom_ = free_zoom_;

    const int centre_x = viewport_.left + viewport_.width / 2;
    const int centre_y = viewport_.top + viewport_.height / 2;
    if (follow) {
        pose.latitude = vehicle.latitude;
        pose.longitude = vehicle.longitude;
        pose.anchor_x = centre_x;
        pose.anchor_y = mode_ == Mode::HeadingUp
            ? viewport_.top + static_cast<int>(viewport_.height *
                                               settings_.heading_up_anchor)
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
    pose.azimuth_degrees = azimuth_;
    pose.elevation_degrees = tilted_ && follow && mode_ == Mode::HeadingUp
        ? settings_.tilted_elevation : 90.0f;
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
