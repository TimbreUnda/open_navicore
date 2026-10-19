// Contraction hierarchy over the road graph written by build_graph.py.
//
//   ch_tool build       <in.graph> <out.ch> [settle limit]
//   ch_tool export-meta <in.graph> <out.meta>         edge -> road table for the radio
//   ch_tool verify      <in.graph> <in.ch> <pairs>    random pairs vs Dijkstra
//   ch_tool bench       <in.graph> <in.ch> <pairs>    batch query time (benchfull: + unpack)
//   ch_tool route       <in.graph> <in.ch> lat lon lat lon
//   ch_tool leanroute   <coords>   <in.ch> lat lon lat lon [repeats]   (radio: no edge arrays)
//   ch_tool leanbench   <coords>   <in.ch> <pairs>
//   ch_tool pack        <in.graph> <in.ch> <out.sidecar> <map.obf>...   the file the radio maps
//   ch_tool sidecar-route <sidecar> lat lon lat lon [repeats]           route through the sidecar
//
// Ordering: edge difference + deleted neighbours + level, lazy updates;
// witness searches are bounded by settled nodes.  The query is
// backends/osmand/ch/ch_query.h, the same code the radio runs.
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

#include <sys/stat.h>

#include "../../backends/osmand/ch/ch_query.h"
#include "../../backends/osmand/ch/ch_sidecar.h"
#include "ch_contract.h"
#include "ch_graph.h"
#include "ch_pack.h"

using namespace onc_ch;
using i64 = int64_t;
using u64 = uint64_t;
using onc_ch::Graph;

static double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
[[noreturn]] static void die(const char* message) {
    std::fprintf(stderr, "error: %s\n", message);
    std::exit(1);
}


template <class T> static void read_vec(std::FILE* f, std::vector<T>& v, size_t count) {
    if (!detail::read_vector(f, v, count)) die("short read");
}

// With coords_only just the header and node coordinates are read.
static Graph load_graph(const char* path, bool coords_only = false) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) die("cannot open graph");
    char magic[4];
    Graph g;
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "CHG1", 4)) die("not a CHG1 graph");
    if (std::fread(&g.n, 4, 1, f) != 1 || std::fread(&g.m, 4, 1, f) != 1) die("short header");
    read_vec(f, g.x, g.n); read_vec(f, g.y, g.n);
    if (!coords_only) {
        read_vec(f, g.from, g.m); read_vec(f, g.to, g.m);
        read_vec(f, g.cost, g.m); read_vec(f, g.time, g.m);
        read_vec(f, g.len, g.m); read_vec(f, g.road, g.m);
        read_vec(f, g.pf, g.m); read_vec(f, g.pt, g.m);
        read_vec(f, g.rev, g.m);
    }
    std::fclose(f);
    return g;
}

static Hierarchy load_or_die(const char* path) {
    Hierarchy h;
    std::string error;
    if (!load_hierarchy(path, h, error)) die(error.c_str());
    return h;
}

// Edge table for the radio: which road and which stretch of it each original
// edge is, and where to find the road (the edge's start node).
// "CHM1", u32 edges, then from[e] u32, road_id[e] i64, point_from[e] u32,
// point_to[e] u32, reversed[e] u8.
static void export_meta(const Graph& g, const char* path) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) die("cannot write meta");
    std::fwrite("CHM1", 1, 4, f);
    std::fwrite(&g.m, 4, 1, f);
    std::fwrite(g.from.data(), 4, g.m, f);
    std::fwrite(g.road.data(), 8, g.m, f);
    std::fwrite(g.pf.data(), 4, g.m, f);
    std::fwrite(g.pt.data(), 4, g.m, f);
    std::fwrite(g.rev.data(), 1, g.m, f);
    std::fclose(f);
}

// ------------------------------------------------------------------- helpers

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
static int lean_main(int argc, char** argv) {
    std::string cmd = argv[1];
    double t0 = now();
    Graph g = load_graph(argv[2], true);
    Hierarchy h = load_or_die(argv[3]);
    std::printf("loaded %u nodes, %zu upward arcs in %.2f s\n", g.n,
                h.out_arcs.size() + h.in_arcs.size(), now() - t0);
    Query q(h.view());
    std::vector<u32> edges;
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
        double b0 = now();
        for (u32 i = 0; i < repeats; ++i) { cost = q.run(s, t, meet); q.path_edges(s, t, meet, edges); }
        double full_ms = (now() - b0) * 1e3 / repeats;
        std::printf("cost %.1fs, %zu base edges, %zu nodes settled; query %.3f ms, query+unpack %.3f ms"
                    " (avg of %u); nearest-node snap (brute force, not part of CH) %.1f ms\n",
                    cost / 1000.0, edges.size(), q.settled, query_ms, full_ms, repeats, snap_ms);
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
        q.path_edges(pr.first, pr.second, meet, edges);
    }
    double full_s = now() - b0;
    std::printf("leanbench: %u pairs, %u reachable; query %.4f ms, query+unpack %.4f ms per pair "
                "(%zu nodes settled on average)\n", pairs, found, query_s * 1e3 / pairs,
                full_s * 1e3 / pairs, settled / pairs);
    return 0;
}

// Route through a sidecar file with the same Router the radio runs.
static int sidecar_main(int argc, char** argv) {
    if (argc < 7) die("sidecar-route <sidecar> lat lon lat lon [repeats]");
    std::string error;
    double t0 = now();
    auto sidecar = Sidecar::open(argv[2], error);
    if (!sidecar) die(error.c_str());
    std::printf("opened %u nodes, %u real edges, %u roads, %zu maps in %.1f ms\n", sidecar->nodes(),
                sidecar->real_edges(), sidecar->header().roads, sidecar->map_count(), (now() - t0) * 1e3);
    auto to_tile = [](double lat, double lon, int& x, int& y) {
        x = (int)((lon + 180.0) / 360.0 * 2147483648.0);
        const double s = std::sin(lat * M_PI / 180.0);
        y = (int)((0.5 - std::log((1 + s) / (1 - s)) / (4 * M_PI)) * 2147483648.0);
    };
    int sx, sy, ex, ey;
    to_tile(std::atof(argv[3]), std::atof(argv[4]), sx, sy);
    to_tile(std::atof(argv[5]), std::atof(argv[6]), ex, ey);
    Router router(*sidecar);
    std::vector<RoadRun> runs;
    u32 cost = 0;
    const u32 repeats = argc > 7 ? std::atoi(argv[7]) : 200;
    Router::Status status = Router::Status::NoRoute;
    double r0 = now();
    for (u32 i = 0; i < repeats; ++i) status = router.route(sx, sy, ex, ey, runs, cost);
    const double ms = (now() - r0) * 1e3 / repeats;
    if (status != Router::Status::Ok) { std::printf("no route (%d)\n", (int)status); return 1; }
    std::printf("cost %.1fs, %zu road stretches from %zu original edges, %zu nodes settled; snap + query + unpack "
                "+ merge %.3f ms (avg of %u)\n", cost / 1000.0, runs.size(), router.edges().size(),
                router.settled(), ms, repeats);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 4) die("usage: ch_tool build|export-meta|pack|sidecar-route|verify|bench|benchfull|route|leanroute|leanbench ...");
    std::string cmd = argv[1];
    if (cmd == "leanroute" || cmd == "leanbench") return lean_main(argc, argv);
    if (cmd == "sidecar-route") return sidecar_main(argc, argv);
    Graph g = load_graph(argv[2]);
    std::fprintf(stderr, "graph: %u nodes, %u edges\n", g.n, g.m);
    if (cmd == "pack") {
        if (argc < 5) die("pack <in.graph> <in.ch> <out.sidecar> <map.obf>...");
        Hierarchy h = load_or_die(argv[3]);
        std::vector<SidecarMap> maps;
        for (int i = 5; i < argc; ++i) {
            struct stat info {};
            if (::stat(argv[i], &info) != 0) die("cannot stat a map file");
            SidecarMap entry {};
            const char* slash = std::strrchr(argv[i], '/');
            std::strncpy(entry.name, slash ? slash + 1 : argv[i], sizeof(entry.name) - 1);
            entry.size = (uint64_t)info.st_size;
            maps.push_back(entry);
        }
        std::string error;
        if (!pack_sidecar(g, h, maps, 15, argv[4], error)) die(error.c_str());
        struct stat out {};
        ::stat(argv[4], &out);
        std::printf("sidecar: %u nodes, %zu+%zu arcs, %zu maps, %.1f MB\n", g.n, h.out_arcs.size(),
                    h.in_arcs.size(), maps.size(), out.st_size / 1e6);
        return 0;
    }
    if (cmd == "export-meta") {
        export_meta(g, argv[3]);
        std::printf("meta: %u edges, %.1f MB\n", g.m, (4.0 + g.m * 21.0) / 1e6);
        return 0;
    }
    if (cmd == "build") {
        double t0 = now();
        Contractor c(g, argc > 4 ? std::atoi(argv[4]) : 200);
        c.run();
        Hierarchy h = c.finish();
        double t1 = now();
        if (!save_hierarchy(h, argv[3])) die("cannot write hierarchy");
        size_t arcs = h.out_arcs.size() + h.in_arcs.size();
        std::printf("built in %.1fs: %zu upward arcs (%.2f per base edge), %.1f MB (graph %.1f MB)\n",
                    t1 - t0, arcs, (double)arcs / g.m,
                    (8.0 * h.n + 4.0 * h.n + arcs * sizeof(QArc)) / 1e6, (g.n * 8.0 + g.m * 12.0) / 1e6);
        return 0;
    }
    Hierarchy h = load_or_die(argv[3]);
    Query q(h.view());
    Plain plain(g);
    std::vector<u32> edges;
    if (cmd == "route") {
        if (argc < 8) die("route needs lat lon lat lon");
        u32 s = nearest(g, std::atof(argv[4]), std::atof(argv[5]));
        u32 t = nearest(g, std::atof(argv[6]), std::atof(argv[7]));
        u32 meet;
        double t0 = now();
        u32 cost = q.run(s, t, meet);
        double t1 = now();
        if (cost == INF) { std::printf("no route\n"); return 0; }
        if (!q.path_edges(s, t, meet, edges)) die("unpack failed");
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
    if (cmd == "routeedges") {
        // Plain text dump of a route's original edges, for checks in Python:
        // edge road_id point_from point_to reversed from_x from_y to_x to_y
        if (argc < 8) die("routeedges needs lat lon lat lon");
        u32 s = nearest(g, std::atof(argv[4]), std::atof(argv[5]));
        u32 t = nearest(g, std::atof(argv[6]), std::atof(argv[7]));
        u32 meet;
        if (q.run(s, t, meet) == INF || !q.path_edges(s, t, meet, edges)) die("no route");
        for (u32 e : edges)
            std::printf("%u %lld %u %u %u %d %d %d %d\n", e, (long long)g.road[e], g.pf[e], g.pt[e],
                        (unsigned)g.rev[e], g.x[g.from[e]], g.y[g.from[e]], g.x[g.to[e]], g.y[g.to[e]]);
        return 0;
    }
    if (cmd == "pairedges") {
        // routeedges for <pairs> random node pairs, separated by "ROUTE" lines
        u32 count = argc > 4 ? std::atoi(argv[4]) : 100;
        std::mt19937 gen(777);
        for (u32 i = 0; i < count; ++i) {
            u32 s = gen() % g.n, t = gen() % g.n, meet;
            if (q.run(s, t, meet) == INF || !q.path_edges(s, t, meet, edges)) continue;
            std::printf("ROUTE %u %u\n", s, t);
            for (u32 e : edges)
                std::printf("%u %lld %u %u %u %d %d %d %d\n", e, (long long)g.road[e], g.pf[e], g.pt[e],
                            (unsigned)g.rev[e], g.x[g.from[e]], g.y[g.from[e]], g.x[g.to[e]], g.y[g.to[e]]);
        }
        return 0;
    }
    u32 pairs = argc > 4 ? std::atoi(argv[4]) : 1000;
    std::mt19937 rng(12345);
    // The clock of some targets ticks in whole milliseconds, so per-query
    // timings are rounded; the batch wall time below is the reliable figure.
    std::vector<std::pair<u32, u32>> queries;
    for (u32 i = 0; i < pairs; ++i) { u32 s = rng() % g.n; queries.push_back({s, (u32)(rng() % g.n)}); }
    if (cmd == "bench" || cmd == "benchfull") {
        u32 meet, found = 0;
        double t0 = now();
        for (const auto& pr : queries) {
            u32 cost = q.run(pr.first, pr.second, meet);
            if (cost == INF) continue;
            ++found;
            if (cmd == "benchfull") q.path_edges(pr.first, pr.second, meet, edges);
        }
        double wall = now() - t0;
        std::printf("%s: %u pairs, %u reachable; batch %.1f ms, %.4f ms per query%s\n", cmd.c_str(), pairs, found,
                    wall * 1e3, wall * 1e3 / pairs, cmd == "benchfull" ? " (query + unpack to base edges)" : "");
        double d0 = now();
        size_t ds;
        u32 plain_pairs = std::min<u32>(pairs, 200);
        for (u32 i = 0; i < plain_pairs; ++i) plain.run(queries[i].first, queries[i].second, ds);
        std::printf("dijkstra: %u pairs, %.1f ms per query\n", plain_pairs, (now() - d0) * 1e3 / plain_pairs);
        return 0;
    }
    // verify: every cost against Dijkstra and every unpacked path as an edge chain
    u32 bad = 0, reachable = 0;
    double total = 0, total_unpack = 0, total_plain = 0; size_t settled_ch = 0, settled_plain = 0;
    for (const auto& pr : queries) {
        u32 s = pr.first, t = pr.second, meet;
        double t0 = now();
        u32 cost = q.run(s, t, meet);
        total += now() - t0; settled_ch += q.settled;
        size_t ps = 0;
        double p0 = now();
        u32 ref = plain.run(s, t, ps);
        total_plain += now() - p0; settled_plain += ps;
        if (ref != cost) { ++bad; if (bad < 5) std::printf("MISMATCH %u->%u ch=%u dijkstra=%u\n", s, t, cost, ref); }
        if (cost == INF) continue;
        ++reachable;
        double u0 = now();
        bool ok = q.path_edges(s, t, meet, edges);
        total_unpack += now() - u0;
        u64 sum = 0; u32 at = s; bool chain = ok;
        for (u32 e : edges) { if (g.from[e] != at) chain = false; at = g.to[e]; sum += g.cost[e]; }
        if (!chain || at != t || sum != cost) { ++bad; if (bad < 5) std::printf("BAD PATH %u->%u\n", s, t); }
    }
    std::printf("verify: %u pairs, %u reachable, %u bad; ch avg %.3f ms (%zu settled avg); "
                "dijkstra avg %.2f ms (%zu settled avg); unpack avg %.3f ms\n",
                pairs, reachable, bad, total / pairs * 1e3, settled_ch / pairs, total_plain / pairs * 1e3,
                settled_plain / pairs, total_unpack / std::max(1u, reachable) * 1e3);
    return bad ? 2 : 0;
}
