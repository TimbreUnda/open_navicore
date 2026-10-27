// The routing sidecar: everything the radio needs to route with a contraction
// hierarchy, in one file built offline from the OBF maps (tools/ch,
// `ch_tool pack`).  Header only, standard library and POSIX.
//
// The file is read through a small page cache (PageFile: 4 KB pages, 64 MB), not
// mapped: the radio's 32-bit processes cannot map a country-sized file, and a route
// only touches a few thousand pages of it.  Nodes are numbered along a Hilbert curve
// (see ch_pack.h), so the nodes of a route are close together in the file.
//
// File layout (little-endian, every section 8-byte aligned, offsets from the
// start of the file):
//   SidecarHeader
//   u32 out_first[nodes + 1], in_first[nodes + 1]     hierarchy CSR
//   QArc out_arcs[], in_arcs[]
//   SidecarEdge edges[real_edges]      original edge -> stretch of a road
//   SidecarRoad roads[roads]           road id and a point to load it by
//   u32 grid_first[grid_w * grid_h + 1]                snap grid: first entry of each cell
//   SidecarGridEntry grid[nodes]       every node with its coordinates, in cell order
//   SidecarMap maps[maps]              the OBF files this was built from
//
// Original edge ids >= real_edges are junction transitions (turn costs and
// restrictions): they carry cost but no road.
#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ch_query.h"

namespace onc_ch {

struct SidecarEdge { u32 road, start, end; };            // start/end: point indexes in travel order
struct SidecarRoad { int64_t id; int32_t x, y; };        // x, y: a vertex of the road
struct SidecarGridEntry { u32 node; int32_t x, y; };     // a node and its coordinates (31-bit tile numbers)
struct SidecarMap { char name[64]; uint64_t size; };     // basename and byte size of an OBF

constexpr char kSidecarMagic[8] = {'O', 'N', 'C', 'H', 'S', 'D', 'C', '2'};
constexpr u32 kSidecarVersion = 2;

struct SidecarHeader {
    char magic[8];
    u32 version, header_bytes;
    u32 nodes, real_edges, roads, maps;
    u32 out_arcs, in_arcs;
    int32_t grid_min_x, grid_min_y;
    u32 grid_shift, grid_w, grid_h, reserved;
    uint64_t off_out_first, off_in_first, off_out_arcs, off_in_arcs;
    uint64_t off_edges, off_roads, off_grid_first, off_grid, off_maps;
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

// A read-only file behind a page cache with clock eviction.  Not thread-safe: one
// owner thread (the navigation worker).  Pages are read with pread, so the file can
// be any size.
class PageFile {
public:
    static std::unique_ptr<PageFile> open(const char* path, std::string& error,
                                          size_t page_bytes = 4096, size_t cache_pages = 16384)
    {
        const int fd = ::open(path, O_RDONLY);
        if (fd < 0) { error = std::string("cannot open ") + path; return nullptr; }
        struct stat info {};
        if (::fstat(fd, &info) != 0) { ::close(fd); error = std::string("cannot stat ") + path; return nullptr; }
        return std::unique_ptr<PageFile>(new PageFile(fd, (uint64_t)info.st_size, page_bytes, cache_pages));
    }

    ~PageFile() { ::close(fd_); }
    PageFile(const PageFile&) = delete;
    PageFile& operator=(const PageFile&) = delete;

    uint64_t size() const { return size_; }
    uint64_t page_reads() const { return page_reads_; }   // pages fetched from the file
    uint64_t page_hits() const { return page_hits_; }     // lookups answered from the cache

    // Copies `bytes` bytes at `offset` to dst; false when the range is outside the file or a
    // read fails.
    bool read(uint64_t offset, void* dst, size_t bytes)
    {
        if (offset > size_ || bytes > size_ - offset) return false;
        uint8_t* out = static_cast<uint8_t*>(dst);
        while (bytes) {
            const uint64_t page = offset / page_bytes_;
            const size_t inside = (size_t)(offset % page_bytes_);
            const size_t take = std::min(bytes, page_bytes_ - inside);
            const uint8_t* data = load(page);
            if (!data) return false;
            std::memcpy(out, data + inside, take);
            out += take; offset += take; bytes -= take;
        }
        return true;
    }

private:
    struct Slot { uint64_t page = 0; bool used = false; bool referenced = false; };

    PageFile(int fd, uint64_t size, size_t page_bytes, size_t cache_pages)
        : fd_(fd), size_(size), page_bytes_(page_bytes), slots_(std::max<size_t>(cache_pages, 2)),
          data_(new uint8_t[slots_.size() * page_bytes]) {}

    const uint8_t* load(uint64_t page)
    {
        const auto hit = where_.find(page);
        if (hit != where_.end()) {
            slots_[hit->second].referenced = true;
            ++page_hits_;
            return data_.get() + (size_t)hit->second * page_bytes_;
        }
        // clock: take an unused slot, or the first unreferenced one, clearing reference bits
        for (;;) {
            Slot& slot = slots_[hand_];
            const size_t index = hand_;
            hand_ = (hand_ + 1) % slots_.size();
            if (slot.used && slot.referenced) { slot.referenced = false; continue; }
            if (slot.used) where_.erase(slot.page);
            uint8_t* target = data_.get() + index * page_bytes_;
            const uint64_t start = page * page_bytes_;
            const size_t want = (size_t)std::min<uint64_t>(page_bytes_, size_ - start);
            size_t got = 0;
            while (got < want) {
                const ssize_t n = ::pread(fd_, target + got, want - got, (off_t)(start + got));
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) { slot.used = false; return nullptr; }
                got += (size_t)n;
            }
            slot = {page, true, true};
            where_[page] = (u32)index;
            ++page_reads_;
            return target;
        }
    }

    int fd_;
    uint64_t size_;
    size_t page_bytes_;
    std::vector<Slot> slots_;
    std::unique_ptr<uint8_t[]> data_;
    std::unordered_map<uint64_t, u32> where_;
    size_t hand_ = 0;
    uint64_t page_reads_ = 0, page_hits_ = 0;
};

class Sidecar;

// The hierarchy of a sidecar as a Store for BasicQuery.
class SidecarStore {
public:
    explicit SidecarStore(Sidecar& sidecar) : sidecar_(&sidecar) {}
    const QArc* arcs(bool forward, u32 node, u32& count, std::vector<QArc>& scratch) const;
private:
    Sidecar* sidecar_;
};

class Sidecar {
public:
    struct Candidate { u32 node; double metres; int32_t x, y; };

    // Opens the file and checks it is a sidecar of this version and size.
    static std::unique_ptr<Sidecar> open(const char* path, std::string& error,
                                         size_t page_bytes = 4096, size_t cache_pages = 16384)
    {
        auto file = PageFile::open(path, error, page_bytes, cache_pages);
        if (!file) return nullptr;
        std::unique_ptr<Sidecar> sidecar(new Sidecar(std::move(file)));
        if (!sidecar->validate(error)) return nullptr;
        return sidecar;
    }

    const SidecarHeader& header() const { return header_; }
    u32 nodes() const { return header_.nodes; }
    u32 real_edges() const { return header_.real_edges; }
    PageFile& file() { return *file_; }
    SidecarStore store() { return SidecarStore(*this); }

    bool edge(u32 id, SidecarEdge& out) { return file_->read(header_.off_edges + (uint64_t)id * sizeof(SidecarEdge), &out, sizeof out); }
    bool road(u32 index, SidecarRoad& out) { return file_->read(header_.off_roads + (uint64_t)index * sizeof(SidecarRoad), &out, sizeof out); }
    size_t map_count() const { return header_.maps; }
    bool map(size_t i, SidecarMap& out) { return file_->read(header_.off_maps + i * sizeof(SidecarMap), &out, sizeof out); }

    // The k nodes nearest to a point, nearest first, with their distance in metres.
    void nearest_nodes(int x31, int y31, size_t k, std::vector<Candidate>& out)
    {
        out.clear();
        const int64_t cx = ((int64_t)x31 - header_.grid_min_x) >> header_.grid_shift;
        const int64_t cy = ((int64_t)y31 - header_.grid_min_y) >> header_.grid_shift;
        const int64_t cell = (int64_t)1 << header_.grid_shift;
        std::vector<std::pair<int64_t, SidecarGridEntry>> best;   // max-heap on squared distance, <= k entries
        const auto by_distance = [](const std::pair<int64_t, SidecarGridEntry>& a,
                                    const std::pair<int64_t, SidecarGridEntry>& b) {
            return a.first != b.first ? a.first < b.first : a.second.node < b.second.node;
        };
        auto scan = [&](int64_t gx, int64_t gy) {
            if (gx < 0 || gy < 0 || gx >= (int64_t)header_.grid_w || gy >= (int64_t)header_.grid_h) return;
            const u32 c = (u32)(gy * header_.grid_w + gx);
            u32 bounds[2];
            if (!file_->read(header_.off_grid_first + (uint64_t)c * 4, bounds, sizeof bounds) || bounds[1] <= bounds[0]) return;
            entries_.resize(bounds[1] - bounds[0]);
            if (!file_->read(header_.off_grid + (uint64_t)bounds[0] * sizeof(SidecarGridEntry), entries_.data(),
                             entries_.size() * sizeof(SidecarGridEntry))) return;
            for (const SidecarGridEntry& e : entries_) {
                const int64_t dx = (int64_t)e.x - x31, dy = (int64_t)e.y - y31;
                const int64_t d = dx * dx + dy * dy;
                if (best.size() == k && d >= best.front().first) continue;
                best.push_back({d, e});
                std::push_heap(best.begin(), best.end(), by_distance);
                if (best.size() > k) { std::pop_heap(best.begin(), best.end(), by_distance); best.pop_back(); }
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
        std::sort(best.begin(), best.end(), by_distance);
        const double scale = metres_per_unit((double)y31);
        for (const auto& b : best)
            out.push_back({b.second.node, std::sqrt((double)b.first) * scale, b.second.x, b.second.y});
    }

private:
    explicit Sidecar(std::unique_ptr<PageFile> file) : file_(std::move(file)) {}

    bool validate(std::string& error)
    {
        if (file_->size() < sizeof(SidecarHeader) || !file_->read(0, &header_, sizeof header_)) {
            error = "too small to be a sidecar"; return false;
        }
        const SidecarHeader& h = header_;
        if (std::memcmp(h.magic, kSidecarMagic, 8) != 0) { error = "not a routing sidecar"; return false; }
        if (h.version != kSidecarVersion || h.header_bytes != sizeof(SidecarHeader)) {
            error = "unsupported sidecar version"; return false;
        }
        const uint64_t bytes = file_->size();
        if (h.file_bytes != bytes) { error = "sidecar size does not match its header (truncated copy?)"; return false; }
        auto fits = [&](uint64_t offset, uint64_t count, uint64_t each) {
            return offset % 8 == 0 && offset <= bytes && count <= (bytes - offset) / (each ? each : 1);
        };
        const uint64_t cells = (uint64_t)h.grid_w * h.grid_h;
        if (!fits(h.off_out_first, (uint64_t)h.nodes + 1, 4) || !fits(h.off_in_first, (uint64_t)h.nodes + 1, 4) ||
            !fits(h.off_out_arcs, h.out_arcs, sizeof(QArc)) || !fits(h.off_in_arcs, h.in_arcs, sizeof(QArc)) ||
            !fits(h.off_edges, h.real_edges, sizeof(SidecarEdge)) || !fits(h.off_roads, h.roads, sizeof(SidecarRoad)) ||
            !fits(h.off_grid_first, cells + 1, 4) || !fits(h.off_grid, h.nodes, sizeof(SidecarGridEntry)) ||
            !fits(h.off_maps, h.maps, sizeof(SidecarMap))) {
            error = "sidecar section outside the file"; return false;
        }
        u32 out_end = 0, in_end = 0;
        if (!file_->read(h.off_out_first + (uint64_t)h.nodes * 4, &out_end, 4) ||
            !file_->read(h.off_in_first + (uint64_t)h.nodes * 4, &in_end, 4) ||
            out_end != h.out_arcs || in_end != h.in_arcs) {
            error = "sidecar hierarchy offsets are inconsistent"; return false;
        }
        return true;
    }

    friend class SidecarStore;
    std::unique_ptr<PageFile> file_;
    SidecarHeader header_ {};
    std::vector<SidecarGridEntry> entries_;   // scratch for one grid cell
};

// The arcs of `node`: two offsets and the arcs between them, through the page cache.
inline const QArc* SidecarStore::arcs(bool forward, u32 node, u32& count, std::vector<QArc>& scratch) const
{
    const SidecarHeader& h = sidecar_->header_;
    u32 bounds[2];
    count = 0;
    if (!sidecar_->file_->read((forward ? h.off_out_first : h.off_in_first) + (uint64_t)node * 4, bounds, sizeof bounds) ||
        bounds[1] < bounds[0])
        return nullptr;
    const u32 n = bounds[1] - bounds[0];
    scratch.resize(n);
    if (n && !sidecar_->file_->read((forward ? h.off_out_arcs : h.off_in_arcs) + (uint64_t)bounds[0] * sizeof(QArc),
                                    scratch.data(), (size_t)n * sizeof(QArc)))
        return nullptr;
    count = n;
    return scratch.data();
}

// Routes between two points over a sidecar.  One Router per sidecar and thread (the
// sidecar's page cache and the query state are not shared).
class Router {
public:
    enum class Status { Ok, NoRoute, Inconsistent, Cancelled };

    explicit Router(Sidecar& sidecar) : sidecar_(sidecar), query_(sidecar.store()) {}

    // Start and end are snapped to the nearest few nodes within kMaxSnapMetres (a point
    // farther from every road has no route); the snap distance counts as a slow walk
    // (120 ms per metre) against the route cost, so a far node does not win on route
    // cost alone.  With a start heading (degrees clockwise from north) a start node more
    // than 120 degrees off it, i.e. behind the car, costs kReversePenaltyMs more, as
    // OsmAnd's own routers charge for starting against the initial direction.  All
    // candidates seed one search with those costs (the best of every pair, in one run).
    // On success runs are the road stretches in travel order (empty when both ends snap
    // to the same node: too short to route here) and cost is the routing cost in
    // milliseconds.  `cancelled`, when set, is asked during the search.
    Status route(int sx, int sy, int ex, int ey, std::vector<RoadRun>& runs, u32& cost,
                 double start_heading_degrees = kNoHeading,
                 const std::function<bool()>& cancelled = {})
    {
        runs.clear();
        sidecar_.nearest_nodes(sx, sy, kSnapNodes, from_);
        sidecar_.nearest_nodes(ex, ey, kSnapNodes, to_);
        const auto too_far = [](const Sidecar::Candidate& c) { return c.metres > kMaxSnapMetres; };
        from_.erase(std::remove_if(from_.begin(), from_.end(), too_far), from_.end());
        to_.erase(std::remove_if(to_.begin(), to_.end(), too_far), to_.end());
        sources_.clear();
        targets_.clear();
        for (const auto& s : from_)
            sources_.push_back({s.node, (u32)std::lround(120.0 * s.metres +
                                                         reverse_penalty(sx, sy, s, start_heading_degrees))});
        for (const auto& t : to_) targets_.push_back({t.node, (u32)std::lround(120.0 * t.metres)});
        u32 meet = 0;
        const u32 total = query_.run(sources_.data(), sources_.size(), targets_.data(), targets_.size(),
                                     meet, cancelled);
        if (total == INF) return cancelled && cancelled() ? Status::Cancelled : Status::NoRoute;
        const u32 from = query_.source_of(meet), to = query_.target_of(meet);
        u32 offsets = 0;
        for (const auto& s : sources_) if (s.node == from) offsets += s.cost;
        for (const auto& t : targets_) if (t.node == to) offsets += t.cost;
        if (!query_.path_edges(from, to, meet, edges_)) return Status::Inconsistent;
        cost = total - std::min(total, offsets);
        return merge(runs) ? Status::Ok : Status::Inconsistent;
    }

    size_t settled() const { return query_.settled; }
    const std::vector<u32>& edges() const { return edges_; }   // original edges of the last route

    static constexpr double kMaxSnapMetres = 1000.0;
    static constexpr double kNoHeading = -1.0;
    static constexpr double kReversePenaltyMs = 60000.0;   // OsmAnd's default 60 s

private:
    static constexpr size_t kSnapNodes = 4;
    static constexpr double kHeadingNearMetres = 10.0;   // a node this close has no direction

    static double reverse_penalty(int sx, int sy, const Sidecar::Candidate& node,
                                  double heading_degrees)
    {
        if (heading_degrees < 0.0 || node.metres < kHeadingNearMetres) return 0.0;
        // 31-bit tile y grows southwards
        const double bearing = std::atan2((double)node.x - sx, (double)sy - node.y) *
                               180.0 / 3.14159265358979323846;
        const double off = std::fabs(std::remainder(bearing - heading_degrees, 360.0));
        return off > 120.0 ? kReversePenaltyMs : 0.0;
    }

    // Consecutive original edges of one road become one stretch; junction
    // transitions (ids beyond the real edges) have no road and are skipped.
    bool merge(std::vector<RoadRun>& runs)
    {
        for (u32 id : edges_) {
            if (id >= sidecar_.real_edges()) continue;
            SidecarEdge e;
            SidecarRoad road;
            if (!sidecar_.edge(id, e) || !sidecar_.road(e.road, road)) return false;
            const int start = (int)e.start, end = (int)e.end;
            if (!runs.empty() && runs.back().road == road.id && runs.back().end == start &&
                ((runs.back().end > runs.back().start) == (end > start))) {
                runs.back().end = end;
            } else {
                runs.push_back({road.id, start, end, road.x, road.y});
            }
        }
        return true;
    }

    Sidecar& sidecar_;
    BasicQuery<SidecarStore> query_;
    std::vector<Sidecar::Candidate> from_, to_;
    std::vector<BasicQuery<SidecarStore>::Seed> sources_, targets_;
    std::vector<u32> edges_;
};

}  // namespace onc_ch
