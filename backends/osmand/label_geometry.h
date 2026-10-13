#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
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
// Same, with the caller's scratch for the cumulative lengths (it keeps its
// capacity, so repeated calls stop allocating).
bool place_along_path(const std::vector<ScreenPoint>& path, int pin_segment,
                      float pin_t, float width, float height,
                      OrientedBox& box, float max_turn,
                      std::vector<float>& cumulative);

// Separating-axis overlap test for two rotated boxes, with `padding` pixels
// of clearance added around each.
bool boxes_overlap(const OrientedBox& a, const OrientedBox& b,
                   float padding = 2.0f);

// Corners in order: top-left, top-right, bottom-right, bottom-left of the
// text (top = text top after rotation).
void box_corners(const OrientedBox& box, ScreenPoint corners[4]);

// Open-addressing map from 64-bit label ids to V (linear probing,
// power-of-two slot count, at most half full). clear() keeps the memory and
// iteration follows insertion order. It grows only past its high-water
// mark, so a table refilled every frame stops allocating after warm-up.
template <typename V>
class LabelIdMap {
public:
    explicit LabelIdMap(std::size_t capacity = 1024) { reserve(capacity); }

    // Room for n entries without growing.
    void reserve(std::size_t n)
    {
        std::size_t slots = 16;
        while (slots < n * 2)
            slots *= 2;
        if (slots > slots_.size())
            rehash(slots);
    }
    std::size_t size() const { return order_.size(); }
    bool empty() const { return order_.empty(); }
    std::size_t capacity() const { return slots_.size() / 2; }

    V* find(std::uint64_t id)
    {
        Slot& slot = slots_[probe(stored(id))];
        return slot.key ? &slot.value : nullptr;
    }
    const V* find(std::uint64_t id) const
    {
        const Slot& slot = slots_[probe(stored(id))];
        return slot.key ? &slot.value : nullptr;
    }
    bool contains(std::uint64_t id) const { return find(id) != nullptr; }

    // The entry for id; second is true when it was just added (value V{}).
    std::pair<V*, bool> emplace(std::uint64_t id)
    {
        const std::uint64_t key = stored(id);
        std::size_t index = probe(key);
        if (slots_[index].key)
            return {&slots_[index].value, false};
        if ((order_.size() + 1) * 2 > slots_.size()) {
            rehash(slots_.size() * 2);
            index = probe(key);
        }
        slots_[index].key = key;
        slots_[index].value = V{};
        order_.push_back(static_cast<unsigned>(index));
        return {&slots_[index].value, true};
    }
    V& operator[](std::uint64_t id) { return *emplace(id).first; }

    void clear()
    {
        for (const auto index : order_)
            slots_[index].key = 0;
        order_.clear();
    }
    void swap(LabelIdMap& other)
    {
        slots_.swap(other.slots_);
        order_.swap(other.order_);
        std::swap(shift_, other.shift_);
    }

    // i-th entry in insertion order. Id 0 is stored as kZeroId.
    std::uint64_t key_at(std::size_t i) const { return slots_[order_[i]].key; }
    const V& value_at(std::size_t i) const { return slots_[order_[i]].value; }
    V& value_at(std::size_t i) { return slots_[order_[i]].value; }

    static constexpr std::uint64_t kZeroId = 0x9E3779B97F4A7C15ull;

private:
    struct Slot {
        std::uint64_t key = 0;   // 0: empty
        V value{};
    };
    static std::uint64_t stored(std::uint64_t id) { return id ? id : kZeroId; }
    std::size_t probe(std::uint64_t key) const
    {
        const std::size_t mask = slots_.size() - 1;
        std::size_t index = static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ull) >> shift_);
        while (slots_[index].key && slots_[index].key != key)
            index = (index + 1) & mask;
        return index;
    }
    void rehash(std::size_t count)
    {
        std::vector<Slot> old(count);
        old.swap(slots_);
        shift_ = 64;
        for (std::size_t n = count; n > 1; n >>= 1)
            --shift_;
        std::vector<unsigned> old_order;
        old_order.swap(order_);
        order_.reserve(count / 2);
        for (const auto index : old_order) {
            const std::size_t at = probe(old[index].key);
            slots_[at] = old[index];
            order_.push_back(static_cast<unsigned>(at));
        }
    }

    std::vector<Slot> slots_;
    std::vector<unsigned> order_;  // occupied slots, insertion order
    int shift_ = 64;
};

} // namespace navicore_osmand
