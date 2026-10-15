#include "vector_tessellator.h"

#include <mapbox/earcut.hpp>

#include <algorithm>
#include <cmath>

namespace navicore_vector {
namespace {

// Sutherland-Hodgman against one axis-aligned edge. axis 0 = x, 1 = y;
// keep_greater selects which side of `limit` survives.
Ring clip_edge(const Ring& input, int axis, float limit, bool keep_greater)
{
    Ring output;
    if (input.empty())
        return output;
    output.reserve(input.size() + 4);
    const auto inside = [&](const Point& p) {
        return keep_greater ? p[axis] >= limit : p[axis] <= limit;
    };
    const auto intersect = [&](const Point& a, const Point& b) {
        const float t = (limit - a[axis]) / (b[axis] - a[axis]);
        Point p;
        p[axis] = limit;
        p[1 - axis] = a[1 - axis] + t * (b[1 - axis] - a[1 - axis]);
        return p;
    };
    Point previous = input.back();
    bool previous_inside = inside(previous);
    for (const auto& current : input) {
        const bool current_inside = inside(current);
        if (current_inside) {
            if (!previous_inside)
                output.push_back(intersect(previous, current));
            output.push_back(current);
        } else if (previous_inside) {
            output.push_back(intersect(previous, current));
        }
        previous = current;
        previous_inside = current_inside;
    }
    return output;
}

void put(std::uint8_t out[4], Rgba c)
{
    out[0] = c.r;
    out[1] = c.g;
    out[2] = c.b;
    out[3] = c.a;
}

} // namespace

Ring clip_ring(const Ring& ring, float min, float max)
{
    // A closed input (last == first) is treated as the same open ring.
    Ring open = ring;
    if (open.size() > 1 && open.front() == open.back())
        open.pop_back();
    bool all_inside = true;
    for (const auto& p : open)
        if (p[0] < min || p[0] > max || p[1] < min || p[1] > max) {
            all_inside = false;
            break;
        }
    if (all_inside)
        return open;
    Ring out = clip_edge(open, 0, min, true);
    out = clip_edge(out, 0, max, false);
    out = clip_edge(out, 1, min, true);
    out = clip_edge(out, 1, max, false);
    return out;
}

std::vector<Ring> clip_polyline(const Ring& line, float min, float max)
{
    std::vector<Ring> runs;
    Ring current;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const Point a = line[i];
        const Point b = line[i + 1];
        // Liang-Barsky.
        float t0 = 0.0f, t1 = 1.0f;
        const float dx = b[0] - a[0], dy = b[1] - a[1];
        const float p[4] = {-dx, dx, -dy, dy};
        const float q[4] = {a[0] - min, max - a[0], a[1] - min, max - a[1]};
        bool visible = true;
        for (int k = 0; k < 4 && visible; ++k) {
            if (p[k] == 0.0f) {
                if (q[k] < 0.0f)
                    visible = false;
                continue;
            }
            const float r = q[k] / p[k];
            if (p[k] < 0.0f) {
                if (r > t1) visible = false;
                else if (r > t0) t0 = r;
            } else {
                if (r < t0) visible = false;
                else if (r < t1) t1 = r;
            }
        }
        if (!visible) {
            if (current.size() >= 2)
                runs.push_back(std::move(current));
            current.clear();
            continue;
        }
        const Point start{a[0] + t0 * dx, a[1] + t0 * dy};
        const Point end{a[0] + t1 * dx, a[1] + t1 * dy};
        if (t0 > 0.0f || current.empty()) {
            if (current.size() >= 2)
                runs.push_back(std::move(current));
            current.clear();
            current.push_back(start);
        }
        current.push_back(end);
        if (t1 < 1.0f) {
            runs.push_back(std::move(current));
            current.clear();
        }
    }
    if (current.size() >= 2)
        runs.push_back(std::move(current));
    return runs;
}

void TileTessellator::add_area(const Ring& outer, const std::vector<Ring>& holes,
                               const AreaStyle& style)
{
    std::vector<Ring> polygon;
    polygon.push_back(clip_ring(outer, 0.0f, kTileExtent));
    if (polygon.front().size() < 3) {
        ++clipped_away_;
        return;
    }
    for (const auto& hole : holes) {
        Ring clipped = clip_ring(hole, 0.0f, kTileExtent);
        if (clipped.size() >= 3)
            polygon.push_back(std::move(clipped));
    }
    const auto indices = mapbox::earcut<std::uint32_t>(polygon);
    if (indices.empty())
        return;
    auto& bucket = areas_[static_cast<int>(style.layer)];
    const auto base = static_cast<std::uint32_t>(bucket.vertices.size());
    for (const auto& ring : polygon)
        for (const auto& p : ring) {
            AreaVertex v{p[0], p[1], {}};
            put(v.rgba, style.fill);
            bucket.vertices.push_back(v);
        }
    for (const auto index : indices)
        bucket.indices.push_back(base + index);
}

TileTessellator::LineBucket& TileTessellator::line_bucket(const LineStyle& style)
{
    const int level = std::min<int>(style.level, kLineLevels - 1);
    return lines_[level][static_cast<int>(style.layer)];
}

void TileTessellator::add_line(const Ring& points, const LineStyle& style)
{
    auto& bucket = line_bucket(style);
    for (const auto& run : clip_polyline(points, 0.0f, kTileExtent))
        extrude(run, style, bucket);
}

void TileTessellator::add_line_unclipped(const Ring& points, const LineStyle& style)
{
    extrude(points, style, line_bucket(style));
}

void TileTessellator::sprite_quad(std::uint8_t level, const Point& at, const Point& dir,
                                  float u0, float v0, float u1, float v1, Rgba colour,
                                  float min_scale)
{
    auto& bucket = sprites_[std::min<int>(level, kLineLevels - 1)];
    const auto base = static_cast<std::uint32_t>(bucket.vertices.size());
    const float corners[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    for (const auto& c : corners) {
        SpriteVertex v{at[0], at[1], dir[0], dir[1], c[0], c[1], {}, min_scale};
        put(v.rgba, colour);
        bucket.vertices.push_back(v);
    }
    bucket.indices.insert(bucket.indices.end(),
                          {base, base + 1, base + 2, base, base + 2, base + 3});
}

void TileTessellator::sprite_triangle(std::uint8_t level, const Point& at, const Point& dir,
                                      const float (&uv)[3][2], Rgba colour, float min_scale)
{
    auto& bucket = sprites_[std::min<int>(level, kLineLevels - 1)];
    const auto base = static_cast<std::uint32_t>(bucket.vertices.size());
    for (const auto& c : uv) {
        SpriteVertex v{at[0], at[1], dir[0], dir[1], c[0], c[1], {}, min_scale};
        put(v.rgba, colour);
        bucket.vertices.push_back(v);
    }
    bucket.indices.insert(bucket.indices.end(), {base, base + 1, base + 2});
}

void TileTessellator::add_oneway_arrows(const Ring& path, const LineStyle& road,
                                        int direction, Rgba colour, float spacing)
{
    if (path.size() < 2 || direction == 0 || spacing <= 0.0f)
        return;
    // Shape in px at zoom 16, from the road's fill width: a shaft and a
    // head, inside the road.
    const float w = road.width;
    const float length = std::max(w * 1.8f, 7.0f);
    const float head = length * 0.45f;
    const float shaft = std::max(w * 0.16f, 0.9f) / 2.0f;
    const float half_head = std::max(w * 0.34f, 2.0f);
    const float tail = -length / 2.0f, neck = length / 2.0f - head, tip = length / 2.0f;
    const float head_uv[3][2] = {{neck, -half_head}, {tip, 0.0f}, {neck, half_head}};
    double travelled = 0.0;
    double next = spacing * 0.5;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const float dx = path[i + 1][0] - path[i][0];
        const float dy = path[i + 1][1] - path[i][1];
        const double segment = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);
        while (segment > 0.0 && next <= travelled + segment) {
            const float t = static_cast<float>((next - travelled) / segment);
            const Point at{path[i][0] + t * dx, path[i][1] + t * dy};
            next += spacing;
            if (at[0] < 0.0f || at[0] >= kTileExtent || at[1] < 0.0f || at[1] >= kTileExtent)
                continue;
            const Point dir{dx * direction, dy * direction};
            // Hidden below zoom 16 (scale 1), like the detail they belong to.
            sprite_quad(road.level, at, dir, tail, -shaft, neck, shaft, colour, 0.99f);
            sprite_triangle(road.level, at, dir, head_uv, colour, 0.99f);
        }
        travelled += segment;
    }
}

void TileTessellator::add_zebra(const Point& at, const Point& along, float road_width,
                                std::uint8_t level, Rgba colour, float min_scale)
{
    // Stripes run along the road and repeat across it, inside the fill.
    const float width = road_width * 0.86f;
    const int stripes = std::clamp(static_cast<int>(road_width / 1.6f), 3, 6);
    const float stripe = width / (2 * stripes - 1);
    const float half_length = std::max(road_width * 0.22f, 1.0f);
    for (int i = 0; i < stripes; ++i) {
        const float v0 = -width / 2.0f + 2 * i * stripe;
        sprite_quad(level, at, along, -half_length, v0, half_length, v0 + stripe, colour,
                    min_scale);
    }
}

void TileTessellator::extrude(const Ring& raw, const LineStyle& style,
                              LineBucket& out)
{
    Ring& run = run_;
    run.clear();
    run.reserve(raw.size());
    for (const auto& p : raw)
        if (run.empty() || std::fabs(p[0] - run.back()[0]) +
                                   std::fabs(p[1] - run.back()[1]) > 0.01f)
            run.push_back(p);
    if (run.size() < 2)
        return;
    const std::size_t n = run.size();
    const auto normal_of = [&](std::size_t i) {
        const float dx = run[i + 1][0] - run[i][0];
        const float dy = run[i + 1][1] - run[i][1];
        const float length = std::sqrt(dx * dx + dy * dy);
        return Point{-dy / length, dx / length};
    };
    const auto base = static_cast<std::uint32_t>(out.vertices.size());
    for (std::size_t i = 0; i < n; ++i) {
        Point normal;
        if (i == 0) {
            normal = normal_of(0);
        } else if (i == n - 1) {
            normal = normal_of(n - 2);
        } else {
            const Point a = normal_of(i - 1);
            const Point b = normal_of(i);
            Point m{a[0] + b[0], a[1] + b[1]};
            const float length = std::sqrt(m[0] * m[0] + m[1] * m[1]);
            if (length < 1e-3f) {
                normal = a;  // U-turn: no sensible miter
            } else {
                m = {m[0] / length, m[1] / length};
                const float cosine = m[0] * a[0] + m[1] * a[1];
                const float scale = std::min(1.0f / std::max(cosine, 1e-3f), 2.5f);
                normal = {m[0] * scale, m[1] * scale};
            }
        }
        for (const float side : {1.0f, -1.0f}) {
            LineVertex v{};
            v.x = run[i][0];
            v.y = run[i][1];
            v.nx = normal[0] * side;
            v.ny = normal[1] * side;
            v.side = side;
            v.width = style.width;
            v.casing_width = style.casing_width;
            put(v.fill, style.fill);
            put(v.casing, style.casing);
            out.vertices.push_back(v);
        }
    }
    for (std::uint32_t i = 0; i + 1 < n; ++i) {
        const std::uint32_t a = base + 2 * i, b = a + 1, c = a + 2, d = a + 3;
        out.indices.insert(out.indices.end(), {a, b, c, b, d, c});
    }
}

TileMesh TileTessellator::finish()
{
    TileMesh mesh;
    finish_into(mesh);
    // Not reused for another tile: release the buckets now.
    for (auto& bucket : areas_)
        bucket = {};
    for (auto& level : lines_)
        for (auto& bucket : level)
            bucket = {};
    for (auto& bucket : sprites_)
        bucket = {};
    return mesh;
}

void TileTessellator::finish_into(TileMesh& mesh)
{
    mesh.area_vertices.clear();
    mesh.area_indices.clear();
    mesh.line_vertices.clear();
    mesh.line_indices.clear();
    mesh.sprite_vertices.clear();
    mesh.sprite_indices.clear();
    for (auto& bucket : areas_) {
        const auto base = static_cast<std::uint32_t>(mesh.area_vertices.size());
        mesh.area_vertices.insert(mesh.area_vertices.end(),
                                  bucket.vertices.begin(), bucket.vertices.end());
        for (const auto index : bucket.indices)
            mesh.area_indices.push_back(base + index);
        bucket.vertices.clear();
        bucket.indices.clear();
    }
    for (int level = 0; level < kLineLevels; ++level) {
        mesh.line_level_start[level] = static_cast<std::uint32_t>(mesh.line_indices.size());
        for (auto& bucket : lines_[level]) {
            const auto base = static_cast<std::uint32_t>(mesh.line_vertices.size());
            mesh.line_vertices.insert(mesh.line_vertices.end(),
                                      bucket.vertices.begin(), bucket.vertices.end());
            for (const auto index : bucket.indices)
                mesh.line_indices.push_back(base + index);
            bucket.vertices.clear();
            bucket.indices.clear();
        }
        mesh.sprite_level_start[level] = static_cast<std::uint32_t>(mesh.sprite_indices.size());
        auto& bucket = sprites_[level];
        const auto base = static_cast<std::uint32_t>(mesh.sprite_vertices.size());
        mesh.sprite_vertices.insert(mesh.sprite_vertices.end(), bucket.vertices.begin(),
                                    bucket.vertices.end());
        for (const auto index : bucket.indices)
            mesh.sprite_indices.push_back(base + index);
        bucket.vertices.clear();
        bucket.indices.clear();
    }
    mesh.line_level_start[kLineLevels] = static_cast<std::uint32_t>(mesh.line_indices.size());
    mesh.sprite_level_start[kLineLevels] = static_cast<std::uint32_t>(mesh.sprite_indices.size());
}

void TileTessellator::clear()
{
    for (auto& bucket : areas_) {
        bucket.vertices.clear();
        bucket.indices.clear();
    }
    for (auto& level : lines_)
        for (auto& bucket : level) {
            bucket.vertices.clear();
            bucket.indices.clear();
        }
    for (auto& bucket : sprites_) {
        bucket.vertices.clear();
        bucket.indices.clear();
    }
    clipped_away_ = 0;
}

} // namespace navicore_vector
