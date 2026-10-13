#include "label_geometry.h"

#include <algorithm>
#include <cmath>

namespace navicore_osmand {
namespace {
constexpr float pi = 3.14159265358979323846f;

float segment_length(const ScreenPoint& a, const ScreenPoint& b)
{
    return std::hypot(b.x - a.x, b.y - a.y);
}

ScreenPoint point_at(const std::vector<ScreenPoint>& path,
                     const std::vector<float>& cumulative, float distance)
{
    if (distance <= 0.0f) return path.front();
    for (std::size_t i = 1; i < path.size(); ++i) {
        if (distance <= cumulative[i]) {
            const float length = cumulative[i] - cumulative[i - 1];
            const float t = length > 0.0f
                ? (distance - cumulative[i - 1]) / length : 0.0f;
            return {path[i - 1].x + (path[i].x - path[i - 1].x) * t,
                    path[i - 1].y + (path[i].y - path[i - 1].y) * t};
        }
    }
    return path.back();
}

float angle_difference(float a, float b)
{
    float delta = std::fmod(a - b, 2.0f * pi);
    if (delta > pi) delta -= 2.0f * pi;
    if (delta < -pi) delta += 2.0f * pi;
    return std::fabs(delta);
}

void project(const ScreenPoint corners[4], float ax, float ay,
             float& minimum, float& maximum)
{
    minimum = maximum = corners[0].x * ax + corners[0].y * ay;
    for (int i = 1; i < 4; ++i) {
        const float value = corners[i].x * ax + corners[i].y * ay;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
}
} // namespace

bool place_along_path(const std::vector<ScreenPoint>& path, int pin_segment,
                      float pin_t, float width, float height,
                      OrientedBox& box, float max_turn)
{
    std::vector<float> cumulative;
    return place_along_path(path, pin_segment, pin_t, width, height, box,
                            max_turn, cumulative);
}

bool place_along_path(const std::vector<ScreenPoint>& path, int pin_segment,
                      float pin_t, float width, float height,
                      OrientedBox& box, float max_turn,
                      std::vector<float>& cumulative)
{
    if (path.size() < 2 || width <= 0.0f || height <= 0.0f)
        return false;
    cumulative.assign(path.size(), 0.0f);
    for (std::size_t i = 1; i < path.size(); ++i)
        cumulative[i] = cumulative[i - 1] + segment_length(path[i - 1], path[i]);
    const float total = cumulative.back();
    if (total < width)
        return false;   // the road on screen is shorter than its name
    pin_segment = std::clamp(pin_segment, 0,
                             static_cast<int>(path.size()) - 2);
    pin_t = std::clamp(pin_t, 0.0f, 1.0f);
    float centre = cumulative[pin_segment] +
        (cumulative[pin_segment + 1] - cumulative[pin_segment]) * pin_t;
    // Slide along the road so the whole name stays on it.
    centre = std::clamp(centre, width * 0.5f, total - width * 0.5f);
    const float start = centre - width * 0.5f;
    const float end = centre + width * 0.5f;
    const ScreenPoint a = point_at(path, cumulative, start);
    const ScreenPoint b = point_at(path, cumulative, end);
    float angle = std::atan2(b.y - a.y, b.x - a.x);
    // Reject labels over sharp bends: every segment under the text must run
    // roughly along the chord.
    for (std::size_t i = 1; i < path.size(); ++i) {
        if (cumulative[i] <= start || cumulative[i - 1] >= end)
            continue;
        if (cumulative[i] - cumulative[i - 1] < 0.5f)
            continue;
        const float segment = std::atan2(path[i].y - path[i - 1].y,
                                         path[i].x - path[i - 1].x);
        if (angle_difference(segment, angle) > max_turn)
            return false;
    }
    // Keep text upright: never read it upside down.
    if (angle > pi * 0.5f) angle -= pi;
    else if (angle <= -pi * 0.5f) angle += pi;
    const ScreenPoint middle = point_at(path, cumulative, centre);
    box = {middle.x, middle.y, width * 0.5f, height * 0.5f, angle};
    return true;
}

void box_corners(const OrientedBox& box, ScreenPoint corners[4])
{
    const float c = std::cos(box.angle);
    const float s = std::sin(box.angle);
    const float local[4][2] = {{-box.half_width, -box.half_height},
                               {box.half_width, -box.half_height},
                               {box.half_width, box.half_height},
                               {-box.half_width, box.half_height}};
    for (int i = 0; i < 4; ++i) {
        corners[i].x = box.cx + local[i][0] * c - local[i][1] * s;
        corners[i].y = box.cy + local[i][0] * s + local[i][1] * c;
    }
}

bool boxes_overlap(const OrientedBox& a, const OrientedBox& b, float padding)
{
    OrientedBox pa = a;
    OrientedBox pb = b;
    pa.half_width += padding; pa.half_height += padding;
    pb.half_width += padding; pb.half_height += padding;
    ScreenPoint ca[4];
    ScreenPoint cb[4];
    box_corners(pa, ca);
    box_corners(pb, cb);
    const float axes[4][2] = {
        {std::cos(pa.angle), std::sin(pa.angle)},
        {-std::sin(pa.angle), std::cos(pa.angle)},
        {std::cos(pb.angle), std::sin(pb.angle)},
        {-std::sin(pb.angle), std::cos(pb.angle)}};
    for (const auto& axis : axes) {
        float a_min, a_max, b_min, b_max;
        project(ca, axis[0], axis[1], a_min, a_max);
        project(cb, axis[0], axis[1], b_min, b_max);
        if (a_max < b_min || b_max < a_min)
            return false;   // separating axis found
    }
    return true;
}

} // namespace navicore_osmand
