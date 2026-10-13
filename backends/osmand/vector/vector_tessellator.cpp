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

void TileTessellator::add_line(const Ring& points, const LineStyle& style)
{
    auto& bucket = lines_[static_cast<int>(style.layer)];
    for (const auto& run : clip_polyline(points, 0.0f, kTileExtent))
        extrude(run, style, bucket);
}

void TileTessellator::add_line_unclipped(const Ring& points, const LineStyle& style)
{
    extrude(points, style, lines_[static_cast<int>(style.layer)]);
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
    for (auto& bucket : lines_)
        bucket = {};
    return mesh;
}

void TileTessellator::finish_into(TileMesh& mesh)
{
    mesh.area_vertices.clear();
    mesh.area_indices.clear();
    mesh.line_vertices.clear();
    mesh.line_indices.clear();
    for (auto& bucket : areas_) {
        const auto base = static_cast<std::uint32_t>(mesh.area_vertices.size());
        mesh.area_vertices.insert(mesh.area_vertices.end(),
                                  bucket.vertices.begin(), bucket.vertices.end());
        for (const auto index : bucket.indices)
            mesh.area_indices.push_back(base + index);
        bucket.vertices.clear();
        bucket.indices.clear();
    }
    for (auto& bucket : lines_) {
        const auto base = static_cast<std::uint32_t>(mesh.line_vertices.size());
        mesh.line_vertices.insert(mesh.line_vertices.end(),
                                  bucket.vertices.begin(), bucket.vertices.end());
        for (const auto index : bucket.indices)
            mesh.line_indices.push_back(base + index);
        bucket.vertices.clear();
        bucket.indices.clear();
    }
}

void TileTessellator::clear()
{
    for (auto& bucket : areas_) {
        bucket.vertices.clear();
        bucket.indices.clear();
    }
    for (auto& bucket : lines_) {
        bucket.vertices.clear();
        bucket.indices.clear();
    }
    clipped_away_ = 0;
}

} // namespace navicore_vector
