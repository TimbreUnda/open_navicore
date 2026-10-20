// Offline contraction of a road graph into a hierarchy (see ch_query.h for the
// query).  Ordering: edge difference + deleted neighbours + level with lazy
// updates; witness searches are bounded by settled nodes, so the hierarchy may
// carry a few shortcuts a perfect witness search would drop (never a wrong one).
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <queue>
#include <utility>
#include <vector>

#include "../../backends/osmand/ch/ch_query.h"
#include "ch_graph.h"

namespace onc_ch {

inline double seconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Arc { u32 to, w; u32 via; };        // via: middle node, or BASE|edge

struct Contractor {
    const Graph& g;
    std::vector<std::vector<Arc>> out, in;
    std::vector<char> done;
    std::vector<u32> deleted, level, order_rank;
    // witness search state
    std::vector<u32> dist, stamp;
    u32 clock = 0;
    int settle_limit;

    explicit Contractor(const Graph& graph, int limit) : g(graph), settle_limit(limit) {
        out.resize(g.n); in.resize(g.n); done.assign(g.n, 0);
        deleted.assign(g.n, 0); level.assign(g.n, 0); order_rank.assign(g.n, 0);
        dist.assign(g.n, INF); stamp.assign(g.n, 0);
        for (u32 e = 0; e < g.m; ++e) add_arc(g.from[e], g.to[e], g.cost[e], BASE | e);
    }

    // One arc per ordered pair: the cheaper wins.
    void add_arc(u32 u, u32 w, u32 weight, u32 via) {
        for (Arc& a : out[u]) {
            if (a.to != w) continue;
            if (a.w <= weight) return;
            a.w = weight; a.via = via;
            for (Arc& b : in[w]) if (b.to == u) { b.w = weight; b.via = via; break; }
            return;
        }
        out[u].push_back({w, weight, via});
        in[w].push_back({u, weight, via});
    }

    void witness(u32 src, u32 skip, u32 max_dist) {
        ++clock;
        using Item = std::pair<u32, u32>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
        dist[src] = 0; stamp[src] = clock; queue.push({0, src});
        int settled = 0;
        while (!queue.empty()) {
            auto [d, u] = queue.top(); queue.pop();
            if (d != dist[u] || stamp[u] != clock) continue;
            if (d > max_dist || ++settled > settle_limit) break;
            for (const Arc& a : out[u]) {
                if (done[a.to] || a.to == skip) continue;
                u32 nd = d + a.w;
                if (stamp[a.to] != clock || nd < dist[a.to]) {
                    stamp[a.to] = clock; dist[a.to] = nd; queue.push({nd, a.to});
                }
            }
        }
    }

    // Shortcuts needed to contract v; adds them when apply is set.
    // removed gets the number of live arcs at v.
    int contract(u32 v, bool apply, int& removed) {
        std::vector<const Arc*> ins, outs;
        for (const Arc& a : in[v]) if (!done[a.to]) ins.push_back(&a);
        for (const Arc& a : out[v]) if (!done[a.to]) outs.push_back(&a);
        removed = (int)(ins.size() + outs.size());
        int shortcuts = 0;
        std::vector<std::pair<std::pair<u32, u32>, std::pair<u32, u32>>> pending;
        for (const Arc* in_arc : ins) {
            u32 u = in_arc->to, max_dist = 0;
            bool any = false;
            for (const Arc* o : outs) if (o->to != u) { max_dist = std::max(max_dist, in_arc->w + o->w); any = true; }
            if (!any) continue;
            witness(u, v, max_dist);
            for (const Arc* o : outs) {
                if (o->to == u) continue;
                u32 through = in_arc->w + o->w;
                bool found = stamp[o->to] == clock && dist[o->to] <= through;
                if (found) continue;
                ++shortcuts;
                if (apply) pending.push_back({{u, o->to}, {through, v}});
            }
        }
        for (auto& p : pending) add_arc(p.first.first, p.first.second, p.second.first, p.second.second);
        return shortcuts;
    }

    int priority(u32 v) {
        int removed;
        int shortcuts = contract(v, false, removed);
        return 2 * (shortcuts - removed) + (int)deleted[v] + (int)level[v];
    }

    void run() {
        using Item = std::pair<int, u32>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
        std::vector<int> current(g.n);
        for (u32 v = 0; v < g.n; ++v) { current[v] = priority(v); queue.push({current[v], v}); }
        u32 next_rank = 0;
        double started = seconds();
        while (!queue.empty()) {
            auto [p, v] = queue.top(); queue.pop();
            if (done[v] || p != current[v]) continue;
            int fresh = priority(v);
            if (fresh > p && !queue.empty() && fresh > queue.top().first) {   // lazy update
                current[v] = fresh; queue.push({fresh, v}); continue;
            }
            int removed;
            contract(v, true, removed);
            done[v] = 1;
            order_rank[v] = next_rank++;
            std::vector<u32> neighbours;
            for (const Arc& a : in[v]) if (!done[a.to]) neighbours.push_back(a.to);
            for (const Arc& a : out[v]) if (!done[a.to]) neighbours.push_back(a.to);
            std::sort(neighbours.begin(), neighbours.end());
            neighbours.erase(std::unique(neighbours.begin(), neighbours.end()), neighbours.end());
            for (u32 x : neighbours) {
                ++deleted[x];
                level[x] = std::max(level[x], level[v] + 1);
                current[x] = priority(x);
                queue.push({current[x], x});
            }
            if (next_rank % 100000 == 0)
                std::fprintf(stderr, "  contracted %u / %u (%.0fs)\n", next_rank, g.n, seconds() - started);
        }
    }

    Hierarchy finish() const {
        Hierarchy h;
        h.n = g.n; h.rank = order_rank;
        std::vector<std::vector<QArc>> uo(g.n), ui(g.n);
        for (u32 u = 0; u < g.n; ++u) {
            for (const Arc& a : out[u]) if (order_rank[a.to] > order_rank[u]) uo[u].push_back({a.to, a.w, a.via});
            for (const Arc& a : in[u]) if (order_rank[a.to] > order_rank[u]) ui[u].push_back({a.to, a.w, a.via});
        }
        h.out_first.assign(g.n + 1, 0); h.in_first.assign(g.n + 1, 0);
        for (u32 u = 0; u < g.n; ++u) {
            h.out_first[u + 1] = h.out_first[u] + (u32)uo[u].size();
            h.in_first[u + 1] = h.in_first[u] + (u32)ui[u].size();
            h.out_arcs.insert(h.out_arcs.end(), uo[u].begin(), uo[u].end());
            h.in_arcs.insert(h.in_arcs.end(), ui[u].begin(), ui[u].end());
        }
        return h;
    }
};

inline bool save_hierarchy(const Hierarchy& h, const char* path) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    u32 counts[3] = {h.n, (u32)h.out_arcs.size(), (u32)h.in_arcs.size()};
    std::fwrite("CHH1", 1, 4, f);
    std::fwrite(counts, 4, 3, f);
    std::fwrite(h.rank.data(), 4, h.n, f);
    std::fwrite(h.out_first.data(), 4, h.n + 1, f);
    std::fwrite(h.in_first.data(), 4, h.n + 1, f);
    std::fwrite(h.out_arcs.data(), sizeof(QArc), h.out_arcs.size(), f);
    std::fwrite(h.in_arcs.data(), sizeof(QArc), h.in_arcs.size(), f);
    return std::fclose(f) == 0;
}

}  // namespace onc_ch
