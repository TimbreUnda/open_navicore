#pragma once

#include <vector>

namespace navicore_osmand {

// Screen-space geometry for labels. Pure math, host-testable.
struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// A label quad: centre, half extents and rotation (radians, clockwise in
// screen space, 0 = text running left to right).
struct OrientedBox {
    float cx = 0.0f;
    float cy = 0.0f;
    float half_width = 0.0f;
    float half_height = 0.0f;
    float angle = 0.0f;
};

// Places a street label of `width` x `height` pixels along a projected road
// path, centred on the pin (segment `pin_segment` starts at path[pin_segment],
// `pin_t` in [0,1] along it). The text follows the road's direction over the
// label's length and is flipped to stay upright. Returns false when the
// visible path is shorter than the text or bends more than `max_turn`
// radians under it (the label would cut across the road).
bool place_along_path(const std::vector<ScreenPoint>& path, int pin_segment,
                      float pin_t, float width, float height,
                      OrientedBox& box, float max_turn = 0.6f);

// Separating-axis overlap test for two rotated boxes, with `padding` pixels
// of clearance added around each.
bool boxes_overlap(const OrientedBox& a, const OrientedBox& b,
                   float padding = 2.0f);

// Corners in order: top-left, top-right, bottom-right, bottom-left of the
// text (top = text top after rotation).
void box_corners(const OrientedBox& box, ScreenPoint corners[4]);

} // namespace navicore_osmand
