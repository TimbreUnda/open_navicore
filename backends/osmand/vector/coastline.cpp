// Sea and island polygons from coastline polylines. A port of OsmAnd core's
// MapPrimitiviser_P::getCoastlines (the non-V1 path the renderer itself
// uses) and of the Utilities clipping helpers it calls, on plain std types so
// it builds and tests on the host without Qt. Only the case the tile builder
// needs is kept: the coastline area equals the tile (no surface-type
// probing).
#include "coastline.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <utility>

namespace navicore_vector {
namespace {

struct PointI {
    std::int32_t x = 0, y = 0;
    PointI() = default;
    PointI(std::int32_t px, std::int32_t py) : x(px), y(py) {}
    bool operator==(const PointI& o) const { return x == o.x && y == o.y; }
    bool operator!=(const PointI& o) const { return !(*this == o); }
};
using Points = std::vector<PointI>;

struct Piece {
    Points points;
    std::int64_t winding;
    int proximity;
};

std::int64_t cross(std::int64_t ax, std::int64_t ay, std::int64_t bx, std::int64_t by)
{
    return ax * by - bx * ay;
}
std::int64_t cross(const PointI& a, const PointI& b)
{
    return cross(a.x, a.y, b.x, b.y);
}

int border_code(const PointI& p, const PointI& tl, const PointI& br)
{
    if (p.x == tl.x)
        return p.y == tl.y ? 0 : 3;
    if (p.x == br.x)
        return p.y == br.y ? 2 : 1;
    if (p.y == tl.y)
        return 0;
    if (p.y == br.y)
        return 2;
    return 4;
}

int offset_on_border(const PointI& point, const PointI& start, int code)
{
    const int offset = (code & 1) > 0 ? point.y - start.y : point.x - start.x;
    return code > 1 ? -offset : offset;
}

int axis_x(const PointI& p0, const PointI& p1, int limit)
{
    const std::int64_t dx = static_cast<std::int64_t>(p1.x) - p0.x;
    const auto offset = dx / 2;
    const auto num = static_cast<std::int64_t>(limit - p0.x) * (p1.y - p0.y);
    return p0.y + static_cast<int>((num + ((num ^ dx) < 0 ? -offset : offset)) / dx);
}

int axis_y(const PointI& p0, const PointI& p1, int limit)
{
    const std::int64_t dy = static_cast<std::int64_t>(p1.y) - p0.y;
    const auto offset = dy / 2;
    const auto num = static_cast<std::int64_t>(limit - p0.y) * (p1.x - p0.x);
    return p0.x + static_cast<int>((num + ((num ^ dy) < 0 ? -offset : offset)) / dy);
}

PointI intersection(int code, const PointI& p0, const PointI& p1, const PointI& tl,
                    const PointI& br)
{
    if ((code & 1) > 0)
        return PointI(tl.x, axis_x(p0, p1, tl.x));
    if ((code & 2) > 0)
        return PointI(br.x, axis_x(p0, p1, br.x));
    if ((code & 4) > 0)
        return PointI(axis_y(p0, p1, tl.y), tl.y);
    return PointI(axis_y(p0, p1, br.y), br.y);
}

int out_code(const PointI& p, const PointI& tl, const PointI& br)
{
    return (p.x < tl.x) | ((p.x > br.x) << 1) | ((p.y < tl.y) << 2) | ((p.y > br.y) << 3);
}

// Utilities::clipCoastlineForTile<false>: the parts of one polyline inside
// the tile, grouped by the border side they start on; `finish` collects
// where they end.
void clip_for_tile(const Points& coastline, const PointI& tl, const PointI& br,
                   std::vector<Piece>* result, Points* finish)
{
    const auto size = coastline.size();
    Points segment;
    segment.reserve(size);
    PointI prev;
    PointI sm(INT32_MIN, INT32_MIN);
    int prev_code = 0;
    bool next = false;
    std::int64_t signed_area = 0;
    for (const auto& point : coastline) {
        const int next_code = out_code(point, tl, br);
        if (next) {
            if (point == prev)
                continue;
            PointI p0 = prev, p1 = point;
            int code0 = prev_code, code1 = next_code;
            bool accept = false;
            for (;;) {
                if ((code0 | code1) == 0) {
                    accept = true;
                    break;
                } else if ((code0 & code1) > 0) {
                    break;
                } else if (code0 > 0) {
                    p0 = intersection(code0, p0, p1, tl, br);
                    code0 = out_code(p0, tl, br);
                } else {
                    p1 = intersection(code1, p0, p1, tl, br);
                    code1 = out_code(p1, tl, br);
                }
            }
            const bool is_empty = segment.empty();
            if (accept && p0 != p1) {
                if (is_empty) {
                    sm.x = INT32_MIN;
                    sm.y = INT32_MIN;
                }
                if (is_empty || segment.back() != p0) {
                    if (!is_empty) {
                        const int code = border_code(segment.front(), tl, br);
                        const int last_code = border_code(segment.back(), tl, br);
                        finish[last_code].push_back(segment.back());
                        result[code].push_back({std::move(segment), signed_area, INT32_MAX});
                        segment.clear();
                        signed_area = 0;
                        segment.reserve(size);
                    }
                    if (p0 != sm) {
                        segment.push_back(p0);
                        sm = p0;
                    }
                }
                if (p1 != sm) {
                    signed_area += cross(p0, p1);
                    segment.push_back(p1);
                    sm = p1;
                }
            } else if (!is_empty) {
                if (segment.size() > 1) {
                    const int code = border_code(segment.front(), tl, br);
                    const int last_code = border_code(segment.back(), tl, br);
                    finish[last_code].push_back(segment.back());
                    result[code].push_back({std::move(segment), signed_area, INT32_MAX});
                }
                segment.clear();
                signed_area = 0;
                segment.reserve(size);
            }
        } else {
            next = true;
        }
        prev = point;
        prev_code = next_code;
    }
    if (segment.size() > 1) {
        const int code = border_code(segment.front(), tl, br);
        const int last_code = border_code(segment.back(), tl, br);
        finish[last_code].push_back(segment.back());
        result[code].push_back({std::move(segment), signed_area, INT32_MAX});
    }
}

void swap_to_last(std::vector<Piece>& v, int index)
{
    const int last = static_cast<int>(v.size()) - 1;
    if (index < last)
        std::swap(v[static_cast<std::size_t>(index)], v[static_cast<std::size_t>(last)]);
}

Piece take_last(std::vector<Piece>& v)
{
    Piece piece = std::move(v.back());
    v.pop_back();
    return piece;
}

} // namespace

bool polygonize_coastlines(
    const std::vector<std::vector<std::pair<std::int32_t, std::int32_t>>>& lines,
    std::int32_t left, std::int32_t right, std::int32_t bottom, std::int32_t top, int,
    std::vector<CoastlineRing>& out)
{
    out.clear();
    const std::uint32_t mask = static_cast<std::uint32_t>(-1) << 5;
    const auto enlarge = [](std::int32_t v) {
        return static_cast<std::int32_t>(std::min<std::int64_t>(std::int64_t(v) + 31, INT32_MAX));
    };
    const PointI tl(static_cast<std::int32_t>(static_cast<std::uint32_t>(left) & mask),
                    static_cast<std::int32_t>(static_cast<std::uint32_t>(top) & mask));
    const PointI br(static_cast<std::int32_t>(static_cast<std::uint32_t>(enlarge(right)) & mask),
                    static_cast<std::int32_t>(static_cast<std::uint32_t>(enlarge(bottom)) & mask));

    bool with_coastlines = false;
    std::vector<Points> polylines;
    for (const auto& line : lines) {
        if (line.size() < 2)
            continue;
        Points points;
        points.reserve(line.size());
        std::int32_t min_x = INT32_MAX, min_y = INT32_MAX, max_x = INT32_MIN, max_y = INT32_MIN;
        for (const auto& p : line) {
            points.emplace_back(p.first, p.second);
            min_x = std::min(min_x, p.first);
            max_x = std::max(max_x, p.first);
            min_y = std::min(min_y, p.second);
            max_y = std::max(max_y, p.second);
        }
        if (points.front() != points.back()) {
            polylines.push_back(std::move(points));
            continue;
        }
        if (points.size() < 4)
            continue;
        // Big closed coastlines (a lake inside the continent) go through the
        // clipping below.
        if (min_x < tl.x || max_x > br.x || min_y < tl.y || max_y > br.y) {
            polylines.push_back(std::move(points));
            continue;
        }
        // A whole island or lake inside the tile.
        CoastlineRing ring;
        std::int64_t signed_area = 0;
        PointI sp = points.back();
        for (const auto& p : points) {
            ring.points.emplace_back(p.x, p.y);
            if (p == sp)
                continue;
            signed_area += cross(sp, p);
            sp = p;
        }
        ring.water = signed_area >= 0;
        if (ring.water)
            with_coastlines = true;
        out.push_back(std::move(ring));
    }

    if (!polylines.empty()) {
        std::vector<Piece> groups[5];
        Points finish[5];
        for (const auto& polyline : polylines)
            clip_for_tile(polyline, tl, br, groups, finish);

        // Find a crossed coastline to start a polygon from.
        int min_proximity = INT32_MAX;
        int from_side = 0;
        int from_index = -1;
        for (int side = 0; side < 4; side++) {
            int i = 0;
            for (auto& piece : groups[side]) {
                const auto& start = piece.points.front();
                for (const auto& end : finish[side]) {
                    const int proximity = std::abs(offset_on_border(end, start, side));
                    if (proximity < piece.proximity)
                        piece.proximity = proximity;
                }
                if (piece.proximity > 0 && piece.proximity < min_proximity) {
                    min_proximity = piece.proximity;
                    from_side = side;
                    from_index = i;
                }
                i++;
            }
        }
        if (from_index >= 0)
            swap_to_last(groups[from_side], from_index);

        // Combine polylines into polygons.
        const int till_side = from_side + 4;
        for (int border = from_side; border <= till_side; border++) {
            const bool from_inside = border == till_side;
            const int begin_side = from_inside ? 4 : border % 4;
            auto& pieces = groups[begin_side];
            while (!pieces.empty()) {
                Piece piece = take_last(pieces);
                Points polygon = std::move(piece.points);
                std::int64_t winding = piece.winding;
                std::int64_t winding_full = winding;
                PointI start_point = polygon.front();
                int start_side = begin_side;
                PointI start_point_full = start_point;
                int start_side_full = begin_side;
                PointI last_point = polygon.back();
                int end_side = border_code(last_point, tl, br);
                if (from_inside && end_side != 4)
                    continue;
                PointI finish_point = last_point;
                int finish_side = end_side;
                int prev_side = 0;
                int i = -2;
                int last_side = 0;
                bool complete = false;
                bool extra_cycle = false;
                while (++i <= last_side) {
                    if (end_side != 4 && i < 0) {
                        i = end_side;
                        last_side = begin_side + (begin_side <= end_side ? 4 : 0);
                    }
                    while (end_side == 4) {
                        auto& next_pieces = groups[end_side];
                        int seg;
                        const int count = static_cast<int>(next_pieces.size());
                        for (seg = 0; seg < count; seg++) {
                            if (next_pieces[static_cast<std::size_t>(seg)].points.front() ==
                                last_point) {
                                swap_to_last(next_pieces, seg);
                                Piece next_piece = take_last(next_pieces);
                                winding += next_piece.winding;
                                winding_full += next_piece.winding;
                                polygon.pop_back();
                                polygon.insert(polygon.end(), next_piece.points.begin(),
                                               next_piece.points.end());
                                break;
                            }
                        }
                        if (seg == count)
                            break;
                        last_point = polygon.back();
                        const int next_side = border_code(last_point, tl, br);
                        if (next_side < 4) {
                            if (i < 0) {
                                i = next_side;
                                last_side = begin_side + (begin_side <= next_side ? 4 : 0);
                            } else {
                                i += (next_side < prev_side ? 4 : 0) + next_side - prev_side;
                            }
                        }
                        end_side = next_side;
                        finish_point = last_point;
                        finish_side = end_side;
                    }
                    if (from_inside) {
                        complete = end_side == 4 && polygon.size() > 3 &&
                                   polygon.front() == polygon.back();
                        break;
                    }
                    if (end_side == 4)
                        break;
                    const int side = i % 4;
                    PointI test_point = last_point;
                    if (side == finish_side && start_side == finish_side) {
                        start_side = -1;
                        const bool is_water = winding + cross(finish_point, start_point) >= 0;
                        const int offset = offset_on_border(finish_point, start_point, side);
                        if (is_water && offset > 0) {
                            test_point = start_point;
                        } else if (!is_water && offset <= 0) {
                            test_point = start_point;
                            if ((side & 1) > 0)
                                test_point.y += side > 1 ? -1 : 1;
                            else
                                test_point.x += side > 1 ? -1 : 1;
                        }
                    } else if (side == end_side && start_side_full == end_side) {
                        start_side_full = -1;
                        const bool is_water = winding_full + cross(last_point, start_point_full) >= 0;
                        const int offset = offset_on_border(last_point, start_point_full, side);
                        if (is_water && offset > 0) {
                            test_point = start_point_full;
                        } else if (!is_water && offset <= 0) {
                            test_point = start_point_full;
                            if ((side & 1) > 0)
                                test_point.y += side > 1 ? -1 : 1;
                            else
                                test_point.x += side > 1 ? -1 : 1;
                        }
                    }
                    auto& next_pieces = groups[side];
                    const int count = static_cast<int>(next_pieces.size());
                    int min_distance = INT32_MAX;
                    int min_index = -1;
                    const int total = side == begin_side ? count + 1 : count;
                    for (int seg = 0; seg < total; seg++) {
                        const PointI& p = seg < count ? next_pieces[static_cast<std::size_t>(seg)].points.front()
                                                      : polygon.front();
                        const int offset = offset_on_border(p, test_point, side);
                        if (!extra_cycle && offset == 0 && test_point == last_point) {
                            last_side += 4;
                            extra_cycle = true;
                        }
                        if (offset >= 0 && offset < min_distance) {
                            min_distance = offset;
                            min_index = seg;
                        }
                    }
                    if (min_index >= 0) {
                        if (min_index < count) {
                            swap_to_last(next_pieces, min_index);
                            Piece next_piece = take_last(next_pieces);
                            winding = next_piece.winding;
                            start_point = next_piece.points.front();
                            winding_full += cross(last_point, start_point) + winding;
                            start_side = border_code(start_point, tl, br);
                            if (next_piece.points.front() == last_point)
                                polygon.pop_back();
                            polygon.insert(polygon.end(), next_piece.points.begin(),
                                           next_piece.points.end());
                            last_point = polygon.back();
                            const int next_side = border_code(last_point, tl, br);
                            if (next_side < 4) {
                                i += (next_side < end_side ? 4 : 0) + next_side - end_side - 1;
                            } else {
                                prev_side = end_side;
                                i -= 1;
                            }
                            end_side = next_side;
                            finish_point = last_point;
                            finish_side = end_side;
                            continue;
                        }
                        complete = true;
                        break;
                    }
                    if (i < last_side) {
                        const PointI previous = last_point;
                        last_point.x = side > 1 ? tl.x : br.x;
                        last_point.y = side > 0 && side < 3 ? br.y : tl.y;
                        polygon.push_back(last_point);
                        end_side = (end_side + 1) % 4;
                        winding_full += cross(previous, last_point);
                    }
                }
                if (!complete)
                    continue;
                if (from_inside) {
                    CoastlineRing ring;
                    ring.water = winding >= 0;
                    for (const auto& p : polygon)
                        ring.points.emplace_back(p.x, p.y);
                    if (ring.water)
                        with_coastlines = true;
                    out.push_back(std::move(ring));
                    continue;
                }
                if (polygon.size() < 3)
                    continue;
                if (polygon.front() != polygon.back())
                    polygon.push_back(polygon.front());
                if (polygon.size() < 4)
                    continue;
                with_coastlines = true;
                CoastlineRing ring;
                ring.water = true;
                for (const auto& p : polygon)
                    ring.points.emplace_back(p.x, p.y);
                out.push_back(std::move(ring));
            }
        }
    }

    if (out.empty())
        return false;
    if (!with_coastlines) {
        // Only islands: the rest of the tile is sea.
        CoastlineRing ring;
        ring.water = true;
        ring.points = {{tl.x, tl.y}, {br.x, tl.y}, {br.x, br.y}, {tl.x, br.y}, {tl.x, tl.y}};
        out.push_back(std::move(ring));
    }
    return true;
}

} // namespace navicore_vector
