// Contraction-hierarchy query, shared by the navigation worker (on the
// radio) and the offline tools/ch tool.  Header only, standard library only,
// no exceptions: errors are reported through bool results.
//
// The hierarchy is the result of contracting the directed road graph in
// importance order.  Every node keeps only its *upward* arcs (to nodes
// contracted later): out_arcs for the forward search, in_arcs (arcs that point
// at the node from higher nodes) for the backward search.  A shortcut arc
// stores its middle node in `via`; an original edge stores BASE | edge id.
//
// Queries read the hierarchy through a HierarchyView, so it can live in a
// memory-mapped file; per-query state is a small hash table (a query settles a
// few hundred nodes, however large the graph is).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

// Read-only view of a hierarchy: n nodes, CSR offsets (n + 1 entries each) and arcs.
struct HierarchyView {
    u32 n = 0;
    const u32* out_first = nullptr;
    const u32* in_first = nullptr;
    const QArc* out_arcs = nullptr;
    const QArc* in_arcs = nullptr;
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

class Query {
public:
    explicit Query(const HierarchyView& hierarchy) : h_(hierarchy) {}

    // Cheapest cost from source to target, INF when unreachable.  `meet` is
    // the node where the two upward searches met.
    u32 run(u32 source, u32 target, u32& meet)
    {
        settled = 0;
        f_.reset(); b_.reset();
        using Item = std::pair<u32, u32>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> qf, qb;
        f_.set(source, 0, source); qf.push({0, source});
        b_.set(target, 0, target); qb.push({0, target});
        u32 best = INF; meet = INF;
        while (!qf.empty() || !qb.empty()) {
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
            const u32* stall_first = forward ? h_.in_first : h_.out_first;
            const QArc* stall_arcs = forward ? h_.in_arcs : h_.out_arcs;
            bool stalled = false;
            for (u32 i = stall_first[u]; i < stall_first[u + 1]; ++i) {
                const QArc& a = stall_arcs[i];
                if (const Entry* reached = mine.find(a.to); reached && reached->dist + a.w < d) { stalled = true; break; }
            }
            if (stalled) continue;
            const u32* first = forward ? h_.out_first : h_.in_first;
            const QArc* arcs = forward ? h_.out_arcs : h_.in_arcs;
            for (u32 i = first[u]; i < first[u + 1]; ++i) {
                const QArc& a = arcs[i];
                const u32 nd = d + a.w;
                const Entry* known = mine.find(a.to);
                if (!known || nd < known->dist) { mine.set(a.to, nd, u); q.push({nd, a.to}); }
            }
        }
        return best;
    }

    // Original edge ids of the cheapest path found by the last run().
    // False when the hierarchy is inconsistent (a shortcut without its halves).
    bool path_edges(u32 source, u32 target, u32 meet, std::vector<u32>& edges) const
    {
        edges.clear();
        std::vector<u32> up;                       // source ... meet
        for (u32 v = meet;; v = f_.find(v)->parent) { up.push_back(v); if (v == source) break; }
        std::reverse(up.begin(), up.end());
        std::vector<u32> down;                     // meet ... target
        for (u32 v = meet;; v = b_.find(v)->parent) { down.push_back(v); if (v == target) break; }
        for (size_t i = 0; i + 1 < up.size(); ++i) {
            const QArc* a = find(h_.out_first, h_.out_arcs, up[i], up[i + 1]);
            if (!a || !unpack(up[i], up[i + 1], a->via, edges)) return false;
        }
        for (size_t i = 0; i + 1 < down.size(); ++i) {
            const QArc* a = find(h_.in_first, h_.in_arcs, down[i + 1], down[i]);   // down[i] -> down[i + 1]
            if (!a || !unpack(down[i], down[i + 1], a->via, edges)) return false;
        }
        return true;
    }

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

    static const QArc* find(const u32* first, const QArc* arcs, u32 at, u32 to)
    {
        const QArc* hit = nullptr;
        for (u32 i = first[at]; i < first[at + 1]; ++i)
            if (arcs[i].to == to && (!hit || arcs[i].w < hit->w)) hit = &arcs[i];
        return hit;
    }

    // a -> b is stored at its lower end; a shortcut is its two halves through `via`.
    bool unpack(u32 a, u32 b, u32 via, std::vector<u32>& edges) const
    {
        if (via & BASE) { edges.push_back(via & ~BASE); return true; }
        const QArc* left = find(h_.in_first, h_.in_arcs, via, a);     // a -> via, via lower
        const QArc* right = find(h_.out_first, h_.out_arcs, via, b);  // via -> b
        return left && right && unpack(a, via, left->via, edges) && unpack(via, b, right->via, edges);
    }

    HierarchyView h_;
    Table f_, b_;   // forward and backward search state
};

}  // namespace onc_ch
