#include "label_geometry.h"
#include "label_policy.h"

#include <cassert>
#include <cmath>

using namespace navicore_osmand;

namespace {
bool near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
constexpr float pi = 3.14159265358979323846f;

LabelTags road(const char* highway, bool on_path)
{
    LabelTags tags;
    tags.highway = highway;
    tags.on_path = on_path;
    return tags;
}
} // namespace

int main()
{
    // Policy: streets ranked by class; non-car roads, churches, libraries,
    // shops and POI captions are dropped; driver POIs keep icons only.
    assert(classify_label(road("primary", true)).label_class == LabelClass::Street);
    assert(classify_label(road("motorway", true)).rank <
           classify_label(road("residential", true)).rank);
    assert(classify_label(road("footway", true)).label_class == LabelClass::Drop);
    assert(classify_label(road("service", true)).label_class == LabelClass::Drop);
    assert(classify_label(road("trunk", false)).label_class == LabelClass::Shield);
    LabelTags signals = road("primary", false);
    signals.icon = true;
    assert(classify_label(signals).label_class == LabelClass::Drop);
    LabelTags church;
    church.amenity = "place_of_worship";
    church.icon = true;
    assert(classify_label(church).label_class == LabelClass::Drop);
    LabelTags library;
    library.amenity = "library";
    assert(classify_label(library).label_class == LabelClass::Drop);
    LabelTags fuel;
    fuel.amenity = "fuel";
    assert(classify_label(fuel).label_class == LabelClass::Drop);   // caption
    fuel.icon = true;
    assert(classify_label(fuel).label_class == LabelClass::DriverPoi);
    LabelTags city;
    city.place = "city";
    assert(classify_label(city).label_class == LabelClass::Place);
    city.place = "hamlet";
    assert(classify_label(city).label_class == LabelClass::Drop);
    assert(classify_label(LabelTags{}).label_class == LabelClass::Drop);

    // Budgets: best ranks win within a class; classes ordered shields,
    // streets, places, POIs; drops never selected.
    std::vector<LabelCandidate> candidates;
    for (int i = 0; i < 20; ++i)
        candidates.push_back({{LabelClass::Street, 11 - (i % 3)}, i});
    candidates.push_back({{LabelClass::Shield, 2}, 0});
    candidates.push_back({{LabelClass::Drop, 0}, 0});
    candidates.push_back({{LabelClass::Place, 0}, 0});
    LabelBudget budget;
    budget.streets = 5;
    const auto chosen = select_labels(candidates, budget);
    assert(chosen.size() == 7);
    assert(candidates[chosen[0]].decision.label_class == LabelClass::Shield);
    for (int i = 1; i <= 5; ++i) {
        assert(candidates[chosen[i]].decision.label_class == LabelClass::Street);
        assert(candidates[chosen[i]].decision.rank == 9);   // best street rank
    }
    assert(candidates[chosen[6]].decision.label_class == LabelClass::Place);

    // Geometry: a label on a straight diagonal road follows it.
    OrientedBox box;
    std::vector<ScreenPoint> diagonal = {{0, 0}, {100, 100}, {200, 200}};
    assert(place_along_path(diagonal, 0, 0.5f, 80.0f, 16.0f, box));
    assert(near(box.angle, pi / 4.0f, 1e-3f));
    assert(near(box.cx, 50.0f, 0.5f) && near(box.cy, 50.0f, 0.5f));
    // Drawn right-to-left it is flipped to stay upright, same placement.
    std::vector<ScreenPoint> reversed = {{200, 200}, {100, 100}, {0, 0}};
    assert(place_along_path(reversed, 1, 0.5f, 80.0f, 16.0f, box));
    assert(near(box.angle, pi / 4.0f, 1e-3f));
    // A vertical road reads bottom-to-top or top-to-bottom but never upside
    // down: the angle stays within (-90, 90] degrees.
    std::vector<ScreenPoint> vertical = {{10, 300}, {10, 0}};
    assert(place_along_path(vertical, 0, 0.5f, 60.0f, 14.0f, box));
    assert(box.angle > -pi / 2.0f - 1e-4f && box.angle <= pi / 2.0f + 1e-4f);
    // Too short for the text, or a sharp bend under it: rejected.
    std::vector<ScreenPoint> stub = {{0, 0}, {30, 0}};
    assert(!place_along_path(stub, 0, 0.5f, 80.0f, 16.0f, box));
    std::vector<ScreenPoint> elbow = {{0, 0}, {60, 0}, {60, 60}};
    assert(!place_along_path(elbow, 0, 1.0f, 80.0f, 16.0f, box));
    // The pin near an end slides inward so the whole name stays on the road.
    std::vector<ScreenPoint> straight = {{0, 0}, {300, 0}};
    assert(place_along_path(straight, 0, 0.0f, 100.0f, 16.0f, box));
    assert(near(box.cx, 50.0f, 0.5f) && near(box.angle, 0.0f, 1e-4f));

    // Rotated collision: crossing diagonals overlap, parallel ones apart do
    // not, and axis-aligned boxes behave like rectangles.
    const OrientedBox a{100, 100, 40, 8, pi / 4.0f};
    const OrientedBox b{100, 100, 40, 8, -pi / 4.0f};
    const OrientedBox c{160, 40, 40, 8, pi / 4.0f};
    assert(boxes_overlap(a, b));
    assert(!boxes_overlap(a, c));
    // Rotated boxes whose axis-aligned bounds overlap but shapes do not.
    const OrientedBox d{100, 100, 50, 4, pi / 4.0f};
    const OrientedBox e{130, 70, 4, 4, 0.0f};
    assert(!boxes_overlap(d, e, 0.0f));
    assert(boxes_overlap({0, 0, 10, 10, 0}, {15, 0, 10, 10, 0}));
    assert(!boxes_overlap({0, 0, 10, 10, 0}, {30, 0, 10, 10, 0}));
    ScreenPoint corners[4];
    box_corners({0, 0, 10, 5, 0}, corners);
    assert(near(corners[0].x, -10, 1e-4f) && near(corners[0].y, -5, 1e-4f));
    assert(near(corners[2].x, 10, 1e-4f) && near(corners[2].y, 5, 1e-4f));
    return 0;
}
