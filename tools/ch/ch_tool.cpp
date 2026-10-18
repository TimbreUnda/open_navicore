// Contraction hierarchy over the road graph written by build_graph.py.
//
//   ch_tool build  <in.graph> <out.ch>
//   ch_tool verify <in.graph> <in.ch> <pairs>          random pairs vs Dijkstra
//   ch_tool bench  <in.graph> <in.ch> <pairs>          batch query time (benchfull: + unpack)
//   ch_tool route  <in.graph> <in.ch> lat lon lat lon  one route, nearest nodes
//
// Ordering: edge difference + deleted neighbours + level, lazy updates;
// witness searches are bounded by settled nodes.  Query: bidirectional
// Dijkstra over upward arcs with stall-on-demand; shortcuts are unpacked
// through their middle node down to original edges.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <random>
#include <string>
#include <vector>

using u32 = uint32_t;
using i64 = int64_t;
using u64 = uint64_t;
static const u32 INF = 0xFFFFFFFFu;
static const u32 BASE = 0x80000000u;   // via flag: original edge, low bits = edge id

static double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
[[noreturn]] static void die(const char* message) {
    std::fprintf(stderr, "error: %s\n", message);
    std::exit(1);
}

struct Graph {
    u32 n = 0, m = 0;
    std::vector<int32_t> x, y;
    std::vector<u32> from, to, cost, time, pf, pt;
    std::vector<float> len;
    std::vector<i64> road;
    std::vector<uint8_t> rev;
};

template <class T> static void read_vec(FILE* f, std::vector<T>& v, size_t count) {
    v.resize(count);
    if (count && std::fread(v.data(), sizeof(T), count, f) != count) die("short read");
}

static Graph load_graph(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) die("cannot open graph");
    char magic[4];
    Graph g;
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "CHG1", 4)) die("not a CHG1 graph");
    if (std::fread(&g.n, 4, 1, f) != 1 || std::fread(&g.m, 4, 1, f) != 1) die("short header");
    read_vec(f, g.x, g.n); read_vec(f, g.y, g.n);
    read_vec(f, g.from, g.m); read_vec(f, g.to, g.m);
    read_vec(f, g.cost, g.m); read_vec(f, g.time, g.m);
    read_vec(f, g.len, g.m); read_vec(f, g.road, g.m);
    read_vec(f, g.pf, g.m); read_vec(f, g.pt, g.m);
    read_vec(f, g.rev, g.m);
    std::fclose(f);
    return g;
}

// ---------------------------------------------------------------- hierarchy

struct QArc { u32 to, w, via; };   // 12 bytes

struct Hierarchy {
    u32 n = 0;
    std::vector<u32> rank;
    std::vector<u32> out_first, in_first;   // CSR over upward arcs
    std::vector<QArc> out_arcs, in_arcs;    // out: x->to, in: to->x (to ranks higher)
};

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
        double started = now();
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
                std::fprintf(stderr, "  contracted %u / %u (%.0fs)\n", next_rank, g.n, now() - started);
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

static void save(const Hierarchy& h, const char* path) {
    FILE* f = std::fopen(path, "wb");
    if (!f) die("cannot write hierarchy");
    u32 counts[3] = {h.n, (u32)h.out_arcs.size(), (u32)h.in_arcs.size()};
    std::fwrite("CHH1", 1, 4, f);
    std::fwrite(counts, 4, 3, f);
    std::fwrite(h.rank.data(), 4, h.n, f);
    std::fwrite(h.out_first.data(), 4, h.n + 1, f);
    std::fwrite(h.in_first.data(), 4, h.n + 1, f);
    std::fwrite(h.out_arcs.data(), sizeof(QArc), h.out_arcs.size(), f);
    std::fwrite(h.in_arcs.data(), sizeof(QArc), h.in_arcs.size(), f);
    std::fclose(f);
}

static Hierarchy load_hierarchy(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) die("cannot open hierarchy");
    char magic[4]; u32 counts[3];
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "CHH1", 4)) die("not a CHH1 file");
    if (std::fread(counts, 4, 3, f) != 3) die("short header");
    Hierarchy h; h.n = counts[0];
    read_vec(f, h.rank, h.n);
    read_vec(f, h.out_first, h.n + 1); read_vec(f, h.in_first, h.n + 1);
    read_vec(f, h.out_arcs, counts[1]); read_vec(f, h.in_arcs, counts[2]);
    std::fclose(f);
    return h;
}

// -------------------------------------------------------------------- query

struct Query {
    const Hierarchy& h;
    std::vector<u32> df, db, sf, sb;   // distances and stamps, forward/backward
    std::vector<u32> pf, pb;           // parent node in the up graphs
    u32 clock = 0;
    size_t settled = 0;

    explicit Query(const Hierarchy& hier) : h(hier), df(h.n), db(h.n), sf(h.n, 0), sb(h.n, 0), pf(h.n), pb(h.n) {}

    u32 get(const std::vector<u32>& d, const std::vector<u32>& s, u32 v) const { return s[v] == clock ? d[v] : INF; }

    // Returns the cost, INF when unreachable; meeting node in meet.
    u32 run(u32 source, u32 target, u32& meet) {
        ++clock; settled = 0;
        using Item = std::pair<u32, u32>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> qf, qb;
        df[source] = 0; sf[source] = clock; pf[source] = source; qf.push({0, source});
        db[target] = 0; sb[target] = clock; pb[target] = target; qb.push({0, target});
        u32 best = INF; meet = INF;
        while (!qf.empty() || !qb.empty()) {
            u32 top = std::min(qf.empty() ? INF : qf.top().first, qb.empty() ? INF : qb.top().first);
            if (top >= best) break;
            bool forward = !qf.empty() && (qb.empty() || qf.top().first <= qb.top().first);
            auto& q = forward ? qf : qb;
            auto& dist = forward ? df : db; auto& stamp = forward ? sf : sb; auto& parent = forward ? pf : pb;
            auto& other_dist = forward ? db : df; auto& other_stamp = forward ? sb : sf;
            auto [d, u] = q.top(); q.pop();
            if (stamp[u] != clock || d != dist[u]) continue;
            ++settled;
            if (other_stamp[u] == clock && d + other_dist[u] < best) { best = d + other_dist[u]; meet = u; }
            // stall on demand: a higher node reaches u cheaper than d
            const auto& stall_first = forward ? h.in_first : h.out_first;
            const auto& stall_arcs = forward ? h.in_arcs : h.out_arcs;
            bool stalled = false;
            for (u32 i = stall_first[u]; i < stall_first[u + 1]; ++i) {
                const QArc& a = stall_arcs[i];
                if (stamp[a.to] == clock && dist[a.to] + a.w < d) { stalled = true; break; }
            }
            if (stalled) continue;
            const auto& first = forward ? h.out_first : h.in_first;
            const auto& arcs = forward ? h.out_arcs : h.in_arcs;
            for (u32 i = first[u]; i < first[u + 1]; ++i) {
                const QArc& a = arcs[i];
                u32 nd = d + a.w;
                if (stamp[a.to] != clock || nd < dist[a.to]) {
                    stamp[a.to] = clock; dist[a.to] = nd; parent[a.to] = u; q.push({nd, a.to});
                }
            }
        }
        return best;
    }

    const QArc* find(const std::vector<u32>& first, const std::vector<QArc>& arcs, u32 at, u32 to) const {
        const QArc* hit = nullptr;
        for (u32 i = first[at]; i < first[at + 1]; ++i)
            if (arcs[i].to == to && (!hit || arcs[i].w < hit->w)) hit = &arcs[i];
        return hit;
    }

    // Original edge ids of the arc a->b (an upward arc of whichever end is lower).
    void unpack(u32 a, u32 b, u32 via, std::vector<u32>& edges) const {
        if (via & BASE) { edges.push_back(via & ~BASE); return; }
        const QArc* left = find(h.in_first, h.in_arcs, via, a);     // a->via, via lower
        const QArc* right = find(h.out_first, h.out_arcs, via, b);  // via->b
        if (!left || !right) die("shortcut unpack: missing arc");
        unpack(a, via, left->via, edges);
        unpack(via, b, right->via, edges);
    }

    std::vector<u32> path_edges(u32 source, u32 target, u32 meet) const {
        std::vector<u32> up;                      // source ... meet
        for (u32 v = meet; ; v = pf[v]) { up.push_back(v); if (v == source) break; }
        std::reverse(up.begin(), up.end());
        std::vector<u32> down;                    // meet ... target
        for (u32 v = meet; ; v = pb[v]) { down.push_back(v); if (v == target) break; }
        std::vector<u32> edges;
        for (size_t i = 0; i + 1 < up.size(); ++i) {
            const QArc* a = find(h.out_first, h.out_arcs, up[i], up[i + 1]);
            unpack(up[i], up[i + 1], a->via, edges);
        }
        for (size_t i = 0; i + 1 < down.size(); ++i) {
            const QArc* a = find(h.in_first, h.in_arcs, down[i + 1], down[i]);   // down[i]->down[i+1]
            unpack(down[i], down[i + 1], a->via, edges);
        }
        return edges;
    }
};

// Plain Dijkstra on the original graph, for verification.
struct Plain {
    const Graph& g;
    std::vector<u32> first, edge;
    std::vector<u32> dist;
    explicit Plain(const Graph& graph) : g(graph) {
        first.assign(g.n + 1, 0);
        for (u32 e = 0; e < g.m; ++e) ++first[g.from[e] + 1];
        for (u32 i = 0; i < g.n; ++i) first[i + 1] += first[i];
        edge.resize(g.m);
        std::vector<u32> fill(first.begin(), first.end() - 1);
        for (u32 e = 0; e < g.m; ++e) edge[fill[g.from[e]]++] = e;
        dist.assign(g.n, INF);
    }
    u32 run(u32 s, u32 t, size_t& settled) {
        std::fill(dist.begin(), dist.end(), INF);
        using Item = std::pair<u32, u32>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> q;
        dist[s] = 0; q.push({0, s}); settled = 0;
        while (!q.empty()) {
            auto [d, u] = q.top(); q.pop();
            if (d != dist[u]) continue;
            ++settled;
            if (u == t) return d;
            for (u32 i = first[u]; i < first[u + 1]; ++i) {
                u32 e = edge[i], nd = d + g.cost[e];
                if (nd < dist[g.to[e]]) { dist[g.to[e]] = nd; q.push({nd, g.to[e]}); }
            }
        }
        return INF;
    }
};

static u32 nearest(const Graph& g, double lat, double lon) {
    double px = (lon + 180.0) / 360.0 * 2147483648.0;
    double s = std::sin(lat * M_PI / 180.0);
    double py = (0.5 - std::log((1 + s) / (1 - s)) / (4 * M_PI)) * 2147483648.0;
    u32 best = 0; double best_d = 1e300;
    for (u32 i = 0; i < g.n; ++i) {
        double dx = g.x[i] - px, dy = g.y[i] - py, d = dx * dx + dy * dy;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

// Low-memory commands for the radio: only the hierarchy and the node
// coordinates are loaded (no edge arrays, no Dijkstra), so a whole-country
// hierarchy fits in RAM.  Edge lengths are unavailable; results are cost,
// number of base edges and timings.
//   leanroute <graph> <ch> lat lon lat lon [repeats]
//   leanbench <graph> <ch> <pairs>
static int lean_main(int argc, char** argv) {
    std::string cmd = argv[1];
    double t0 = now();
    FILE* f = std::fopen(argv[2], "rb");
    if (!f) die("cannot open graph");
    char magic[4];
    Graph g;
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "CHG1", 4)) die("not a CHG1 graph");
    if (std::fread(&g.n, 4, 1, f) != 1 || std::fread(&g.m, 4, 1, f) != 1) die("short header");
    read_vec(f, g.x, g.n); read_vec(f, g.y, g.n);
    std::fclose(f);
    Hierarchy h = load_hierarchy(argv[3]);
    std::printf("loaded %u nodes, %zu upward arcs in %.2f s\n", g.n,
                h.out_arcs.size() + h.in_arcs.size(), now() - t0);
    Query q(h);
    if (cmd == "leanroute") {
        if (argc < 8) die("leanroute needs lat lon lat lon");
        double s0 = now();
        u32 s = nearest(g, std::atof(argv[4]), std::atof(argv[5]));
        u32 t = nearest(g, std::atof(argv[6]), std::atof(argv[7]));
        double snap_ms = (now() - s0) * 1e3;
        u32 repeats = argc > 8 ? std::atoi(argv[8]) : 200, meet = 0, cost = INF;
        double a0 = now();
        for (u32 i = 0; i < repeats; ++i) cost = q.run(s, t, meet);
        double query_ms = (now() - a0) * 1e3 / repeats;
        if (cost == INF) { std::printf("no route\n"); return 0; }
        size_t edges = 0;
        double b0 = now();
        for (u32 i = 0; i < repeats; ++i) { cost = q.run(s, t, meet); edges = q.path_edges(s, t, meet).size(); }
        double full_ms = (now() - b0) * 1e3 / repeats;
        std::printf("cost %.1fs, %zu base edges, %zu nodes settled; query %.3f ms, query+unpack %.3f ms"
                    " (avg of %u); nearest-node snap (brute force, not part of CH) %.1f ms\n",
                    cost / 1000.0, edges, q.settled, query_ms, full_ms, repeats, snap_ms);
        return 0;
    }
    u32 pairs = argc > 4 ? std::atoi(argv[4]) : 1000;
    std::mt19937 rng(12345);
    std::vector<std::pair<u32, u32>> queries;
    for (u32 i = 0; i < pairs; ++i) { u32 s = rng() % g.n; queries.push_back({s, (u32)(rng() % g.n)}); }
    u32 meet, found = 0; size_t settled = 0;
    double a0 = now();
    for (const auto& pr : queries) {
        if (q.run(pr.first, pr.second, meet) != INF) ++found;
        settled += q.settled;
    }
    double query_s = now() - a0;
    found = 0;
    double b0 = now();
    for (const auto& pr : queries) {
        u32 cost = q.run(pr.first, pr.second, meet);
        if (cost == INF) continue;
        ++found;
        q.path_edges(pr.first, pr.second, meet);
    }
    double full_s = now() - b0;
    std::printf("leanbench: %u pairs, %u reachable; query %.4f ms, query+unpack %.4f ms per pair "
                "(%zu nodes settled on average)\n", pairs, found, query_s * 1e3 / pairs,
                full_s * 1e3 / pairs, settled / pairs);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 4) die("usage: ch_tool build|verify|bench|route ...");
    std::string cmd = argv[1];
    if (cmd == "leanroute" || cmd == "leanbench") return lean_main(argc, argv);
    Graph g = load_graph(argv[2]);
    std::fprintf(stderr, "graph: %u nodes, %u edges\n", g.n, g.m);
    if (cmd == "build") {
        double t0 = now();
        Contractor c(g, argc > 4 ? std::atoi(argv[4]) : 200);
        c.run();
        Hierarchy h = c.finish();
        double t1 = now();
        save(h, argv[3]);
        size_t arcs = h.out_arcs.size() + h.in_arcs.size();
        std::printf("built in %.1fs: %zu upward arcs (%.2f per base edge), %.1f MB (graph %.1f MB)\n",
                    t1 - t0, arcs, (double)arcs / g.m,
                    (8.0 * h.n + 4.0 * h.n + arcs * sizeof(QArc)) / 1e6, (g.n * 8.0 + g.m * 12.0) / 1e6);
        return 0;
    }
    Hierarchy h = load_hierarchy(argv[3]);
    Query q(h);
    Plain plain(g);
    if (cmd == "route") {
        if (argc < 8) die("route needs lat lon lat lon");
        u32 s = nearest(g, std::atof(argv[4]), std::atof(argv[5]));
        u32 t = nearest(g, std::atof(argv[6]), std::atof(argv[7]));
        u32 meet;
        double t0 = now();
        u32 cost = q.run(s, t, meet);
        double t1 = now();
        if (cost == INF) { std::printf("no route\n"); return 0; }
        std::vector<u32> edges = q.path_edges(s, t, meet);
        double t2 = now();
        double metres = 0, ms = 0;
        for (u32 e : edges) { metres += g.len[e]; ms += g.time[e]; }
        size_t settled;
        double t3 = now();
        u32 ref = plain.run(s, t, settled);
        double t4 = now();
        std::printf("cost %.1fs (dijkstra %.1fs), %.2f km, %.1f min, %zu edges; ch %.3f ms + unpack %.3f ms,"
                    " %zu settled; dijkstra %.1f ms, %zu settled\n",
                    cost / 1000.0, ref / 1000.0, metres / 1000, ms / 60000, edges.size(),
                    (t1 - t0) * 1e3, (t2 - t1) * 1e3, q.settled, (t4 - t3) * 1e3, settled);
        return 0;
    }
    u32 pairs = argc > 4 ? std::atoi(argv[4]) : 1000;
    std::mt19937 rng(12345);
    u32 bad = 0, reachable = 0;
    double total = 0, total_unpack = 0, total_plain = 0; size_t settled_ch = 0, settled_plain = 0;
    // The clock of some targets ticks in whole milliseconds, so per-query
    // timings are rounded; the batch wall time below is the reliable figure.
    std::vector<std::pair<u32, u32>> queries;
    for (u32 i = 0; i < pairs; ++i) { u32 s = rng() % g.n; queries.push_back({s, (u32)(rng() % g.n)}); }
    if (cmd == "bench" || cmd == "benchfull") {
        u32 meet, found = 0; size_t edges_total = 0;
        double t0 = now();
        for (const auto& pr : queries) {
            u32 cost = q.run(pr.first, pr.second, meet);
            if (cost == INF) continue;
            ++found;
            if (cmd == "benchfull") edges_total += q.path_edges(pr.first, pr.second, meet).size();
        }
        double wall = now() - t0;
        std::printf("%s: %u pairs, %u reachable; batch %.1f ms, %.4f ms per query%s\n", cmd.c_str(), pairs, found,
                    wall * 1e3, wall * 1e3 / pairs, cmd == "benchfull" ? " (query + unpack to base edges)" : "");
        (void)edges_total;
        double d0 = now();
        size_t ds; u32 dn = 0;
        u32 plain_pairs = std::min<u32>(pairs, 200);
        for (u32 i = 0; i < plain_pairs; ++i) { if (plain.run(queries[i].first, queries[i].second, ds) != INF) ++dn; }
        double dwall = now() - d0;
        std::printf("dijkstra: %u pairs, %.1f ms per query\n", plain_pairs, dwall * 1e3 / plain_pairs);
        return 0;
    }
    for (u32 i = 0; i < pairs; ++i) {
        u32 s = rng() % g.n, t = rng() % g.n, meet;
        double t0 = now();
        u32 cost = q.run(s, t, meet);
        double t1 = now();
        total += t1 - t0; settled_ch += q.settled;
        u32 ref = INF; size_t ps = 0;
        if (cmd == "verify") {
            double p0 = now();
            ref = plain.run(s, t, ps);
            total_plain += now() - p0; settled_plain += ps;
            if (ref != cost) { ++bad; if (bad < 5) std::printf("MISMATCH %u->%u ch=%u dijkstra=%u\n", s, t, cost, ref); }
        }
        if (cost != INF) {
            ++reachable;
            if (cmd == "verify") {
                double u0 = now();
                std::vector<u32> edges = q.path_edges(s, t, meet);
                total_unpack += now() - u0;
                u64 sum = 0; u32 at = s; bool chain = true;
                for (u32 e : edges) { if (g.from[e] != at) chain = false; at = g.to[e]; sum += g.cost[e]; }
                if (!chain || at != t || sum != cost) { ++bad; if (bad < 5) std::printf("BAD PATH %u->%u\n", s, t); }
            }
        }
    }
    std::printf("%s: %u pairs, %u reachable, %u bad; ch avg %.3f ms (%zu settled avg)",
                cmd.c_str(), pairs, reachable, bad, total / pairs * 1e3, settled_ch / pairs);
    if (cmd == "verify")
        std::printf("; dijkstra avg %.2f ms (%zu settled avg); unpack avg %.3f ms",
                    total_plain / pairs * 1e3, settled_plain / pairs, total_unpack / std::max(1u, reachable) * 1e3);
    std::printf("\n");
    return bad ? 2 : 0;
}
