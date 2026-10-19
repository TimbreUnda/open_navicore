// The routing sidecar: everything the radio needs to route with a contraction
// hierarchy, in one memory-mapped file built offline from the OBF maps
// (tools/ch, `ch_tool pack`).  Header only, standard library and POSIX mmap.
//
// File layout (little-endian, every section 8-byte aligned, offsets from the
// start of the file):
//   SidecarHeader
//   SidecarPoint nodes[nodes]          coordinates (31-bit tile numbers)
//   u32 out_first[nodes + 1], in_first[nodes + 1]     hierarchy CSR
//   QArc out_arcs[], in_arcs[]
//   SidecarEdge edges[real_edges]      original edge -> stretch of a road
//   SidecarRoad roads[roads]           road id and a point to load it by
//   u32 grid_first[grid_w * grid_h + 1], grid_nodes[nodes]     snap grid
//   SidecarMap maps[maps]              the OBF files this was built from
//
// Original edge ids >= real_edges are junction transitions (turn costs and
// restrictions): they carry cost but no road.
#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ch_query.h"

namespace onc_ch {

struct SidecarPoint { int32_t x, y; };
struct SidecarEdge { u32 road, start, end; };            // start/end: point indexes in travel order
struct SidecarRoad { int64_t id; int32_t x, y; };        // x, y: a vertex of the road
struct SidecarMap { char name[64]; uint64_t size; };     // basename and byte size of an OBF

constexpr char kSidecarMagic[8] = {'O', 'N', 'C', 'H', 'S', 'D', 'C', '1'};
constexpr u32 kSidecarVersion = 1;

struct SidecarHeader {
    char magic[8];
    u32 version, header_bytes;
    u32 nodes, real_edges, roads, maps;
    u32 out_arcs, in_arcs;
    int32_t grid_min_x, grid_min_y;
    u32 grid_shift, grid_w, grid_h, reserved;
    uint64_t off_nodes, off_out_first, off_in_first, off_out_arcs, off_in_arcs;
    uint64_t off_edges, off_roads, off_grid_first, off_grid_nodes, off_maps;
    uint64_t file_bytes;
};

// One stretch of one road, in travel direction, ready for the core to load.
struct RoadRun {
    int64_t road;
    int start, end;
    int anchor_x, anchor_y;   // a vertex of the road: loading the tile there finds it
};

// Metres per 31-bit unit at the latitude of tile row y31 (a unit is a Mercator unit).
inline double metres_per_unit(double y31)
{
    const double lat = std::atan(std::sinh(3.14159265358979323846 * (1.0 - 2.0 * y31 / 2147483648.0)));
    return 40075016.686 / 2147483648.0 * std::cos(lat);
}

class Sidecar {
public:
    struct Candidate { u32 node; double metres; };

    // Maps the file and checks it is a sidecar of this version and size.
    static std::unique_ptr<Sidecar> open(const char* path, std::string& error)
    {
        const int fd = ::open(path, O_RDONLY);
        if (fd < 0) { error = std::string("cannot open ") + path; return nullptr; }
        struct stat info {};
        if (::fstat(fd, &info) != 0 || (size_t)info.st_size < sizeof(SidecarHeader)) {
            ::close(fd); error = "too small to be a sidecar"; return nullptr;
        }
        void* map = ::mmap(nullptr, (size_t)info.st_size, PROT_READ, MAP_SHARED, fd, 0);
        ::close(fd);
        if (map == MAP_FAILED) { error = "mmap failed"; return nullptr; }
        std::unique_ptr<Sidecar> sidecar(new Sidecar((const uint8_t*)map, (size_t)info.st_size));
        if (!sidecar->validate(error)) return nullptr;
        return sidecar;
    }

    ~Sidecar() { if (base_) ::munmap((void*)base_, bytes_); }
    Sidecar(const Sidecar&) = delete;
    Sidecar& operator=(const Sidecar&) = delete;

    const SidecarHeader& header() const { return *header_; }
    u32 nodes() const { return header_->nodes; }
    u32 real_edges() const { return header_->real_edges; }
    HierarchyView hierarchy() const { return view_; }
    const SidecarPoint& point(u32 node) const { return points_[node]; }
    const SidecarEdge& edge(u32 id) const { return edges_[id]; }
    const SidecarRoad& road(u32 index) const { return roads_[index]; }
    size_t map_count() const { return header_->maps; }
    const SidecarMap& map(size_t i) const { return maps_[i]; }

    // The k nodes nearest to a point, nearest first, with their distance in metres.
    void nearest_nodes(int x31, int y31, size_t k, std::vector<Candidate>& out) const
    {
        out.clear();
        const int64_t cx = ((int64_t)x31 - header_->grid_min_x) >> header_->grid_shift;
        const int64_t cy = ((int64_t)y31 - header_->grid_min_y) >> header_->grid_shift;
        const int64_t cell = (int64_t)1 << header_->grid_shift;
        std::vector<std::pair<int64_t, u32>> best;   // max-heap on squared distance, <= k entries
        auto scan = [&](int64_t gx, int64_t gy) {
            if (gx < 0 || gy < 0 || gx >= (int64_t)header_->grid_w || gy >= (int64_t)header_->grid_h) return;
            const u32 c = (u32)(gy * header_->grid_w + gx);
            for (u32 i = grid_first_[c]; i < grid_first_[c + 1]; ++i) {
                const u32 node = grid_nodes_[i];
                const int64_t dx = (int64_t)points_[node].x - x31, dy = (int64_t)points_[node].y - y31;
                const int64_t d = dx * dx + dy * dy;
                if (best.size() == k && d >= best.front().first) continue;
                best.push_back({d, node});
                std::push_heap(best.begin(), best.end());
                if (best.size() > k) { std::pop_heap(best.begin(), best.end()); best.pop_back(); }
            }
        };
        const int max_ring = 128;   // about 50 km at a 400 m cell
        for (int r = 0; r <= max_ring; ++r) {
            if (r == 0) {
                scan(cx, cy);
            } else {
                for (int64_t gx = cx - r; gx <= cx + r; ++gx) { scan(gx, cy - r); scan(gx, cy + r); }
                for (int64_t gy = cy - r + 1; gy <= cy + r - 1; ++gy) { scan(cx - r, gy); scan(cx + r, gy); }
            }
            // anything not scanned yet is at least r cells away
            if (best.size() == k && std::sqrt((double)best.front().first) <= (double)r * (double)cell) break;
        }
        std::sort(best.begin(), best.end());
        const double scale = metres_per_unit((double)y31);
        for (const auto& b : best) out.push_back({b.second, std::sqrt((double)b.first) * scale});
    }

private:
    Sidecar(const uint8_t* base, size_t bytes) : base_(base), bytes_(bytes) {}

    template <class T> const T* at(uint64_t offset) const { return (const T*)(base_ + offset); }

    bool validate(std::string& error)
    {
        header_ = at<SidecarHeader>(0);
        const SidecarHeader& h = *header_;
        if (std::memcmp(h.magic, kSidecarMagic, 8) != 0) { error = "not a routing sidecar"; return false; }
        if (h.version != kSidecarVersion || h.header_bytes != sizeof(SidecarHeader)) {
            error = "unsupported sidecar version"; return false;
        }
        if (h.file_bytes != bytes_) { error = "sidecar size does not match its header (truncated copy?)"; return false; }
        auto fits = [&](uint64_t offset, uint64_t count, uint64_t each) {
            return offset % 8 == 0 && offset <= bytes_ && count <= (bytes_ - offset) / (each ? each : 1);
        };
        const uint64_t cells = (uint64_t)h.grid_w * h.grid_h;
        if (!fits(h.off_nodes, h.nodes, sizeof(SidecarPoint)) ||
            !fits(h.off_out_first, (uint64_t)h.nodes + 1, 4) || !fits(h.off_in_first, (uint64_t)h.nodes + 1, 4) ||
            !fits(h.off_out_arcs, h.out_arcs, sizeof(QArc)) || !fits(h.off_in_arcs, h.in_arcs, sizeof(QArc)) ||
            !fits(h.off_edges, h.real_edges, sizeof(SidecarEdge)) || !fits(h.off_roads, h.roads, sizeof(SidecarRoad)) ||
            !fits(h.off_grid_first, cells + 1, 4) || !fits(h.off_grid_nodes, h.nodes, 4) ||
            !fits(h.off_maps, h.maps, sizeof(SidecarMap))) {
            error = "sidecar section outside the file"; return false;
        }
        points_ = at<SidecarPoint>(h.off_nodes);
        view_ = {h.nodes, at<u32>(h.off_out_first), at<u32>(h.off_in_first),
                 at<QArc>(h.off_out_arcs), at<QArc>(h.off_in_arcs)};
        edges_ = at<SidecarEdge>(h.off_edges);
        roads_ = at<SidecarRoad>(h.off_roads);
        grid_first_ = at<u32>(h.off_grid_first);
        grid_nodes_ = at<u32>(h.off_grid_nodes);
        maps_ = at<SidecarMap>(h.off_maps);
        if (view_.out_first[h.nodes] != h.out_arcs || view_.in_first[h.nodes] != h.in_arcs) {
            error = "sidecar hierarchy offsets are inconsistent"; return false;
        }
        return true;
    }

    const uint8_t* base_;
    size_t bytes_;
    const SidecarHeader* header_ = nullptr;
    const SidecarPoint* points_ = nullptr;
    HierarchyView view_;
    const SidecarEdge* edges_ = nullptr;
    const SidecarRoad* roads_ = nullptr;
    const u32* grid_first_ = nullptr;
    const u32* grid_nodes_ = nullptr;
    const SidecarMap* maps_ = nullptr;
};

// Routes between two points over a sidecar.  One Router per thread (it owns the
// query state); the Sidecar itself is shared and read-only.
class Router {
public:
    enum class Status { Ok, NoRoute, Inconsistent };

    explicit Router(const Sidecar& sidecar) : sidecar_(sidecar), query_(sidecar.hierarchy()) {}

    // Start and end are snapped to the nearest few nodes within kMaxSnapMetres (a point
    // farther from every road has no route); the snap distance counts as a slow walk
    // (120 ms per metre) against the route cost, so a far node does not win on route
    // cost alone.  On success runs are the road stretches in travel order (empty when
    // both ends snap to the same node: too short to route here) and cost is the routing
    // cost in milliseconds.
    Status route(int sx, int sy, int ex, int ey, std::vector<RoadRun>& runs, u32& cost)
    {
        runs.clear();
        sidecar_.nearest_nodes(sx, sy, kSnapNodes, from_);
        sidecar_.nearest_nodes(ex, ey, kSnapNodes, to_);
        const auto too_far = [](const Sidecar::Candidate& c) { return c.metres > kMaxSnapMetres; };
        from_.erase(std::remove_if(from_.begin(), from_.end(), too_far), from_.end());
        to_.erase(std::remove_if(to_.begin(), to_.end(), too_far), to_.end());
        double best = 1e18;
        u32 best_from = 0, best_to = 0, best_cost = INF;
        for (const auto& s : from_) {
            for (const auto& t : to_) {
                u32 meet = 0;
                const u32 c = query_.run(s.node, t.node, meet);
                if (c == INF) continue;
                const double total = c + 120.0 * (s.metres + t.metres);
                if (total < best) { best = total; best_from = s.node; best_to = t.node; best_cost = c; }
            }
        }
        if (best_cost == INF) return Status::NoRoute;
        u32 meet = 0;
        query_.run(best_from, best_to, meet);   // path_edges reads the last run
        if (!query_.path_edges(best_from, best_to, meet, edges_)) return Status::Inconsistent;
        cost = best_cost;
        merge(runs);
        return Status::Ok;
    }

    size_t settled() const { return query_.settled; }
    const std::vector<u32>& edges() const { return edges_; }   // original edges of the last route

    static constexpr double kMaxSnapMetres = 1000.0;

private:
    static constexpr size_t kSnapNodes = 4;

    // Consecutive original edges of one road become one stretch; junction
    // transitions (ids beyond the real edges) have no road and are skipped.
    void merge(std::vector<RoadRun>& runs) const
    {
        for (u32 id : edges_) {
            if (id >= sidecar_.real_edges()) continue;
            const SidecarEdge& e = sidecar_.edge(id);
            const SidecarRoad& road = sidecar_.road(e.road);
            const int start = (int)e.start, end = (int)e.end;
            if (!runs.empty() && runs.back().road == road.id && runs.back().end == start &&
                ((runs.back().end > runs.back().start) == (end > start))) {
                runs.back().end = end;
            } else {
                runs.push_back({road.id, start, end, road.x, road.y});
            }
        }
    }

    const Sidecar& sidecar_;
    Query query_;
    std::vector<Sidecar::Candidate> from_, to_;
    std::vector<u32> edges_;
};

}  // namespace onc_ch
