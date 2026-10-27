// Contraction-hierarchy query, shared by the navigation worker (on the
// radio) and the offline tools/ch tool.  Header only, standard library only,
// no exceptions: errors are reported through bool results.
//
// The hierarchy is the result of contracting the directed road graph in
// importance order.  Every node keeps only its *upward* arcs (to nodes
// contracted later): out arcs for the forward search, in arcs (arcs that point
// at the node from higher nodes) for the backward search.  A shortcut arc
// stores its middle node in `via`; an original edge stores BASE | edge id.
//
// The query reads arcs through a Store: HierarchyView (plain arrays, used by
// the tools and the tests) or the sidecar's paged file store (the radio, where
// the hierarchy is far larger than a 32-bit process can map).  Per-query state
// is a small hash table (a query settles a few hundred nodes, however large the
// graph is).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace onc_ch {

using u32 = uint32_t;

constexpr u32 INF = 0xFFFFFFFFu;
constexpr u32 BASE = 0x80000000u;   // `via` flag: original edge, low bits = edge id

struct QArc {
    u32 to;    // the other end (always the higher-ranked node)
    u32 w;     // cost in milliseconds
    u32 via;   // middle node of a shortcut, or BASE | original edge id
};

// A hierarchy held in plain arrays: n nodes, CSR offsets (n + 1 entries each) and arcs.
// Also the simplest Store.
struct HierarchyView {
    u32 n = 0;
    const u32* out_first = nullptr;
    const u32* in_first = nullptr;
    const QArc* out_arcs = nullptr;
    const QArc* in_arcs = nullptr;

    // The upward arcs of `node` for the forward (out) or backward (in) search.
    const QArc* arcs(bool forward, u32 node, u32& count, std::vector<QArc>& /*scratch*/) const
    {
        const u32* first = forward ? out_first : in_first;
        count = first[node + 1] - first[node];
        return (forward ? out_arcs : in_arcs) + first[node];
    }
};

struct Hierarchy {
    u32 n = 0;
    std::vector<u32> rank;                  // contraction order; empty when loaded for queries only
    std::vector<u32> out_first, in_first;   // CSR offsets, n + 1 entries
    std::vector<QArc> out_arcs, in_arcs;

    HierarchyView view() const
    {
        return {n, out_first.data(), in_first.data(), out_arcs.data(), in_arcs.data()};
    }
};

namespace detail {
template <class T>
bool read_vector(std::FILE* f, std::vector<T>& v, size_t count)
{
    v.resize(count);
    return count == 0 || std::fread(v.data(), sizeof(T), count, f) == count;
}
}  // namespace detail

// File layout (little-endian): "CHH1", u32 nodes, u32 out arcs, u32 in arcs,
// u32 rank[n], u32 out_first[n+1], u32 in_first[n+1], QArc out[], QArc in[].
// With load_rank false the rank array is skipped (queries do not need it).
inline bool load_hierarchy(const char* path, Hierarchy& h, std::string& error, bool load_rank = false)
{
    std::FILE* f = std::fopen(path, "rb");
    if (!f) { error = std::string("cannot open ") + path; return false; }
    char magic[4];
    u32 counts[3];
    bool ok = std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, "CHH1", 4) == 0 &&
              std::fread(counts, 4, 3, f) == 3;
    if (!ok) { std::fclose(f); error = "not a CHH1 hierarchy"; return false; }
    h.n = counts[0];
    if (load_rank) ok = detail::read_vector(f, h.rank, h.n);
    else { h.rank.clear(); ok = std::fseek(f, (long)h.n * 4, SEEK_CUR) == 0; }
    ok = ok && detail::read_vector(f, h.out_first, (size_t)h.n + 1) &&
         detail::read_vector(f, h.in_first, (size_t)h.n + 1) &&
         detail::read_vector(f, h.out_arcs, counts[1]) && detail::read_vector(f, h.in_arcs, counts[2]);
    std::fclose(f);
    if (!ok) error = "short read";
    return ok;
}

// Store must provide: const QArc* arcs(bool forward, u32 node, u32& count, std::vector<QArc>& scratch) const.
// The returned pointer is valid until the next call with the same scratch.
template <class Store>
class BasicQuery {
public:
    explicit BasicQuery(Store store) : store_(std::move(store)) {}

    // A start or end node and what reaching it costs already (snapping onto it).
    struct Seed { u32 node, cost; };

    // Cheapest cost from source to target, INF when unreachable.  `meet` is
    // the node where the two upward searches met.
    u32 run(u32 source, u32 target, u32& meet)
    {
        const Seed s {source, 0}, t {target, 0};
        return run(&s, 1, &t, 1, meet);
    }

    // Cheapest path from any source to any target, seeds' costs included: one
    // search instead of one per pair.  INF when unreachable or when `cancelled`
    // (asked every 1024 settled nodes) says stop.  source_of()/target_of()
    // name the seeds the path starts and ends at.
    u32 run(const Seed* sources, size_t source_count, const Seed* targets, size_t target_count,
            u32& meet, const std::function<bool()>& cancelled = {})
    {
        settled = 0;
        f_.reset(); b_.reset();
        using Item = std::pair<u32, u32>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> qf, qb;
        const auto seed = [](Table& table, auto& queue, const Seed& s) {
            const Entry* known = table.find(s.node);
            if (known && known->dist <= s.cost) return;
            table.set(s.node, s.cost, s.node);
            queue.push({s.cost, s.node});
        };
        for (size_t i = 0; i < source_count; ++i) seed(f_, qf, sources[i]);
        for (size_t i = 0; i < target_count; ++i) seed(b_, qb, targets[i]);
        u32 best = INF; meet = INF;
        while (!qf.empty() || !qb.empty()) {
            if ((settled & 1023u) == 1023u && cancelled && cancelled()) { meet = INF; return INF; }
            const u32 top = std::min(qf.empty() ? INF : qf.top().first, qb.empty() ? INF : qb.top().first);
            if (top >= best) break;
            const bool forward = !qf.empty() && (qb.empty() || qf.top().first <= qb.top().first);
            auto& q = forward ? qf : qb;
            Table& mine = forward ? f_ : b_;
            Table& other = forward ? b_ : f_;
            const auto [d, u] = q.top(); q.pop();
            const Entry* here = mine.find(u);
            if (!here || here->dist != d) continue;
            ++settled;
            if (const Entry* opposite = other.find(u); opposite && d + opposite->dist < best) {
                best = d + opposite->dist; meet = u;
            }
            // stall on demand: a higher node already reaches u more cheaply
            u32 count = 0;
            const QArc* stall = store_.arcs(!forward, u, count, scratch_);
            bool stalled = false;
            for (u32 i = 0; i < count; ++i) {
                if (const Entry* reached = mine.find(stall[i].to); reached && reached->dist + stall[i].w < d) {
                    stalled = true;
                    break;
                }
            }
            if (stalled) continue;
            const QArc* arcs = store_.arcs(forward, u, count, scratch_);
            for (u32 i = 0; i < count; ++i) {
                const u32 nd = d + arcs[i].w;
                const Entry* known = mine.find(arcs[i].to);
                if (!known || nd < known->dist) { mine.set(arcs[i].to, nd, u); q.push({nd, arcs[i].to}); }
            }
        }
        return best;
    }

    // Original edge ids of the cheapest path found by the last run().
    // False when the hierarchy is inconsistent (a shortcut without its halves).
    bool path_edges(u32 source, u32 target, u32 meet, std::vector<u32>& edges)
    {
        edges.clear();
        std::vector<u32> up;                       // source ... meet
        for (u32 v = meet;; v = f_.find(v)->parent) { up.push_back(v); if (v == source) break; }
        std::reverse(up.begin(), up.end());
        std::vector<u32> down;                     // meet ... target
        for (u32 v = meet;; v = b_.find(v)->parent) { down.push_back(v); if (v == target) break; }
        QArc arc {0, 0, 0};
        for (size_t i = 0; i + 1 < up.size(); ++i) {
            if (!find(true, up[i], up[i + 1], arc) || !unpack(up[i], up[i + 1], arc.via, edges)) return false;
        }
        for (size_t i = 0; i + 1 < down.size(); ++i) {
            // down[i] -> down[i + 1] is stored at the lower end, down[i + 1], as an in arc
            if (!find(false, down[i + 1], down[i], arc) || !unpack(down[i], down[i + 1], arc.via, edges)) return false;
        }
        return true;
    }

    // The seed the last run()'s path starts at (seeds are their own parents).
    u32 source_of(u32 meet) const { return root(f_, meet); }
    u32 target_of(u32 meet) const { return root(b_, meet); }

    size_t settled = 0;   // nodes settled by the last run()

private:
    struct Entry { u32 node, dist, parent, stamp; };

    // Open-addressing hash table of the nodes one search has reached; reset()
    // is O(1) (a generation counter), so a Query can be reused for many routes.
    class Table {
    public:
        Table() : slots_(1u << 12) {}
        void reset() { ++stamp_; used_ = 0; }
        const Entry* find(u32 node) const
        {
            for (u32 i = hash(node) & mask();; i = (i + 1) & mask()) {
                const Entry& e = slots_[i];
                if (e.stamp != stamp_) return nullptr;
                if (e.node == node) return &e;
            }
        }
        void set(u32 node, u32 dist, u32 parent)
        {
            if ((used_ + 1) * 2 > slots_.size()) grow();
            for (u32 i = hash(node) & mask();; i = (i + 1) & mask()) {
                Entry& e = slots_[i];
                if (e.stamp != stamp_) { e = {node, dist, parent, stamp_}; ++used_; return; }
                if (e.node == node) { e.dist = dist; e.parent = parent; return; }
            }
        }
    private:
        static u32 hash(u32 node) { return node * 2654435761u >> 7; }
        u32 mask() const { return (u32)slots_.size() - 1; }
        void grow()
        {
            std::vector<Entry> old(slots_.size() * 2);
            old.swap(slots_);
            const u32 live = stamp_;
            used_ = 0;
            for (const Entry& e : old) {
                if (e.stamp != live) continue;
                for (u32 i = hash(e.node) & mask();; i = (i + 1) & mask()) {
                    if (slots_[i].stamp != live) { slots_[i] = e; ++used_; break; }
                }
            }
        }
        std::vector<Entry> slots_;
        u32 stamp_ = 1;
        u32 used_ = 0;
    };

    static u32 root(const Table& table, u32 v)
    {
        for (const Entry* e = table.find(v); e && e->parent != v; e = table.find(v)) v = e->parent;
        return v;
    }

    // The cheapest arc at -> to in the upward out (forward) or in arcs of `at`.
    bool find(bool forward, u32 at, u32 to, QArc& result)
    {
        u32 count = 0;
        const QArc* arcs = store_.arcs(forward, at, count, find_scratch_);
        bool hit = false;
        for (u32 i = 0; i < count; ++i)
            if (arcs[i].to == to && (!hit || arcs[i].w < result.w)) { result = arcs[i]; hit = true; }
        return hit;
    }

    // a -> b is stored at its lower end; a shortcut is its two halves through `via`.
    bool unpack(u32 a, u32 b, u32 via, std::vector<u32>& edges)
    {
        if (via & BASE) { edges.push_back(via & ~BASE); return true; }
        QArc left {0, 0, 0}, right {0, 0, 0};
        if (!find(false, via, a, left) ||     // a -> via, via lower: an in arc of via
            !find(true, via, b, right))       // via -> b: an out arc of via
            return false;
        const u32 left_via = left.via, right_via = right.via;
        return unpack(a, via, left_via, edges) && unpack(via, b, right_via, edges);
    }

    Store store_;
    Table f_, b_;                                // forward and backward search state
    std::vector<QArc> scratch_, find_scratch_;   // arcs fetched from a file-backed store
};

using Query = BasicQuery<HierarchyView>;

}  // namespace onc_ch
