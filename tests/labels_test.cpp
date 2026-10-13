#include "label_geometry.h"

#include <cassert>
#include <cmath>

using namespace navicore_osmand;

namespace {
bool near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
constexpr float pi = 3.14159265358979323846f;

} // namespace

int main()
{
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
    // The scratch variant places identically and reuses its buffer.
    std::vector<float> scratch;
    scratch.reserve(16);
    const float* scratch_data = scratch.data();
    for (const auto* path : {&diagonal, &reversed, &vertical, &straight}) {
        OrientedBox plain, reused;
        const bool ok_plain = place_along_path(*path, 0, 0.3f, 60.0f, 14.0f, plain);
        const bool ok_reused =
            place_along_path(*path, 0, 0.3f, 60.0f, 14.0f, reused, 0.6f, scratch);
        assert(ok_plain == ok_reused);
        assert(plain.cx == reused.cx && plain.cy == reused.cy && plain.angle == reused.angle);
    }
    assert(!place_along_path(elbow, 0, 1.0f, 80.0f, 16.0f, box, 0.6f, scratch));
    assert(scratch.data() == scratch_data);

    // Label id table: insert/find, insertion order, clear keeps memory,
    // swap, growth past the reservation, id 0.
    {
        LabelIdMap<int> table(8);
        const std::size_t capacity = table.capacity();
        assert(capacity >= 8 && table.empty());
        for (std::uint64_t id = 1; id <= 8; ++id) {
            const auto added = table.emplace(id * 0x1000);
            assert(added.second && *added.first == 0);
            *added.first = static_cast<int>(id);
        }
        assert(!table.emplace(0x3000).second && *table.find(0x3000) == 3);
        assert(table.size() == 8 && !table.contains(0x9000));
        for (std::size_t i = 0; i < table.size(); ++i)
            assert(table.key_at(i) == (i + 1) * 0x1000 && table.value_at(i) == int(i) + 1);
        table.clear();
        assert(table.empty() && !table.contains(0x1000) && table.capacity() == capacity);
        table[0] = 7;
        assert(table.contains(0) && *table.find(0) == 7);
        LabelIdMap<int> other(8);
        other[42] = 1;
        table.swap(other);
        assert(table.contains(42) && !table.contains(0));
        assert(other.contains(0) && !other.contains(42));
        for (std::uint64_t id = 100; id < 1100; ++id)
            table[id] = static_cast<int>(id);
        assert(table.size() == 1001 && table.capacity() >= 1001);
        for (std::uint64_t id = 100; id < 1100; ++id)
            assert(*table.find(id) == static_cast<int>(id));
        assert(*table.find(42) == 1 && table.key_at(0) == 42);
        // Refilled after clear(): no growth.
        const std::size_t grown = table.capacity();
        table.clear();
        for (std::uint64_t id = 5000; id < 6000; ++id)
            table[id] = 1;
        assert(table.capacity() == grown);
    }

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
