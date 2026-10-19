// Contraction-hierarchy query, shared by the navigation worker (on the
// radio) and the offline tools/ch tool.  Header only, standard library only,
// no exceptions: errors are reported through bool results.
//
// The hierarchy is the result of contracting the directed road graph in
// importance order.  Every node keeps only its *upward* arcs (to nodes
// contracted later): out_arcs for the forward search, in_arcs (arcs that point
// at the node from higher nodes) for the backward search.  A shortcut arc
// stores its middle node in `via`; an original edge stores BASE | edge id.
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

struct Hierarchy {
    u32 n = 0;
    std::vector<u32> rank;                  // contraction order; empty when loaded for queries only
    std::vector<u32> out_first, in_first;   // CSR offsets, n + 1 entries
    std::vector<QArc> out_arcs, in_arcs;
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
    explicit Query(const Hierarchy& hierarchy)
        : h_(hierarchy), df_(h_.n), db_(h_.n), sf_(h_.n, 0), sb_(h_.n, 0), pf_(h_.n), pb_(h_.n) {}

    // Cheapest cost from source to target, INF when unreachable.  `meet` is
    // the node where the two upward searches met.
    u32 run(u32 source, u32 target, u32& meet)
    {
        ++clock_; settled = 0;
        using Item = std::pair<u32, u32>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> qf, qb;
        df_[source] = 0; sf_[source] = clock_; pf_[source] = source; qf.push({0, source});
        db_[target] = 0; sb_[target] = clock_; pb_[target] = target; qb.push({0, target});
        u32 best = INF; meet = INF;
        while (!qf.empty() || !qb.empty()) {
            const u32 top = std::min(qf.empty() ? INF : qf.top().first, qb.empty() ? INF : qb.top().first);
            if (top >= best) break;
            const bool forward = !qf.empty() && (qb.empty() || qf.top().first <= qb.top().first);
            auto& q = forward ? qf : qb;
            auto& dist = forward ? df_ : db_; auto& stamp = forward ? sf_ : sb_; auto& parent = forward ? pf_ : pb_;
            auto& other_dist = forward ? db_ : df_; auto& other_stamp = forward ? sb_ : sf_;
            const auto [d, u] = q.top(); q.pop();
            if (stamp[u] != clock_ || d != dist[u]) continue;
            ++settled;
            if (other_stamp[u] == clock_ && d + other_dist[u] < best) { best = d + other_dist[u]; meet = u; }
            // stall on demand: a higher node already reaches u more cheaply
            const auto& stall_first = forward ? h_.in_first : h_.out_first;
            const auto& stall_arcs = forward ? h_.in_arcs : h_.out_arcs;
            bool stalled = false;
            for (u32 i = stall_first[u]; i < stall_first[u + 1]; ++i) {
                const QArc& a = stall_arcs[i];
                if (stamp[a.to] == clock_ && dist[a.to] + a.w < d) { stalled = true; break; }
            }
            if (stalled) continue;
            const auto& first = forward ? h_.out_first : h_.in_first;
            const auto& arcs = forward ? h_.out_arcs : h_.in_arcs;
            for (u32 i = first[u]; i < first[u + 1]; ++i) {
                const QArc& a = arcs[i];
                const u32 nd = d + a.w;
                if (stamp[a.to] != clock_ || nd < dist[a.to]) {
                    stamp[a.to] = clock_; dist[a.to] = nd; parent[a.to] = u; q.push({nd, a.to});
                }
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
        for (u32 v = meet;; v = pf_[v]) { up.push_back(v); if (v == source) break; }
        std::reverse(up.begin(), up.end());
        std::vector<u32> down;                     // meet ... target
        for (u32 v = meet;; v = pb_[v]) { down.push_back(v); if (v == target) break; }
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
    static const QArc* find(const std::vector<u32>& first, const std::vector<QArc>& arcs, u32 at, u32 to)
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

    const Hierarchy& h_;
    std::vector<u32> df_, db_, sf_, sb_, pf_, pb_;   // forward/backward distance, stamp, parent
    u32 clock_ = 0;
};

}  // namespace onc_ch
