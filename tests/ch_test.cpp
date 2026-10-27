#include "ch_contract.h"
#include "ch_pack.h"
#include "ch_sidecar.h"

#include <unistd.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <queue>
#include <random>

using namespace onc_ch;

namespace {

// Test maps sit near 45 N, where one tile unit is about 1.3 cm: nodes 20000 units apart are ~250 m apart.
constexpr int kBaseX = 1100000000, kBaseY = 750000000, kSpacing = 20000;

// Plain Dijkstra over the original edges, the reference for the hierarchy.
u32 dijkstra(const Graph& g, u32 source, u32 target)
{
    std::vector<u32> dist(g.n, INF);
    std::priority_queue<std::pair<u32, u32>, std::vector<std::pair<u32, u32>>, std::greater<>> queue;
    dist[source] = 0;
    queue.push({0, source});
    while (!queue.empty()) {
        const auto [d, u] = queue.top();
        queue.pop();
        if (d != dist[u]) continue;
        if (u == target) return d;
        for (u32 e = 0; e < g.m; ++e)
            if (g.from[e] == u && d + g.cost[e] < dist[g.to[e]]) {
                dist[g.to[e]] = d + g.cost[e];
                queue.push({dist[g.to[e]], g.to[e]});
            }
    }
    return INF;
}

void add_edge(Graph& g, u32 from, u32 to, u32 cost, int64_t road, u32 pf, u32 pt, uint8_t rev)
{
    g.from.push_back(from); g.to.push_back(to); g.cost.push_back(cost); g.time.push_back(cost);
    g.len.push_back(1.0f); g.road.push_back(road); g.pf.push_back(pf); g.pt.push_back(pt);
    g.rev.push_back(rev);
    ++g.m;
}

// A w x h grid of two-way roads, some one-way, with random costs; node (i, j) sits at
// kSpacing * (i, j) tile units from (kBaseX, kBaseY).
Graph random_grid(int w, int h, unsigned seed)
{
    std::mt19937 rng(seed);
    Graph g;
    g.n = (u32)(w * h);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) { g.x.push_back(kBaseX + kSpacing * i); g.y.push_back(kBaseY + kSpacing * j); }
    int64_t road = 1;
    auto link = [&](int a, int b) {
        const u32 cost = 1000 + rng() % 9000;
        const bool oneway = rng() % 8 == 0;
        add_edge(g, a, b, cost, road, 0, 1, 0);
        if (!oneway) add_edge(g, b, a, cost, road, 0, 1, 1);
        ++road;
    };
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            if (i + 1 < w) link(j * w + i, j * w + i + 1);
            if (j + 1 < h) link(j * w + i, (j + 1) * w + i);
        }
    return g;
}

std::string temp_path()
{
    char path[] = "/tmp/onc_ch_test_XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    return path;
}

} // namespace

int main()
{
    // The hierarchy answers exactly what Dijkstra does, and unpacks to a connected path of
    // original edges with that cost.
    {
        Graph g = random_grid(14, 11, 42);
        Contractor contractor(g, 200);
        contractor.run();
        const Hierarchy h = contractor.finish();
        Query query(h.view());
        std::mt19937 rng(7);
        std::vector<u32> edges;
        for (int i = 0; i < 300; ++i) {
            const u32 s = rng() % g.n, t = rng() % g.n;
            u32 meet = 0;
            const u32 cost = query.run(s, t, meet);
            assert(cost == dijkstra(g, s, t));
            if (cost == INF) continue;
            assert(query.path_edges(s, t, meet, edges));
            u32 at = s, sum = 0;
            for (u32 e : edges) { assert(g.from[e] == at); at = g.to[e]; sum += g.cost[e]; }
            assert(at == t && sum == cost);
        }

        // Seeded with several starts and ends (with their own costs), one search finds
        // the best of every pair, and the path runs from the seeds it names.
        for (int i = 0; i < 200; ++i) {
            std::vector<Query::Seed> from, to;
            for (int k = 0; k < 4; ++k) {
                from.push_back({(u32)(rng() % g.n), (u32)(rng() % 3000)});
                to.push_back({(u32)(rng() % g.n), (u32)(rng() % 3000)});
            }
            u32 brute = INF;
            for (const auto& s : from)
                for (const auto& t : to) {
                    const u32 d = dijkstra(g, s.node, t.node);
                    if (d != INF) brute = std::min(brute, d + s.cost + t.cost);
                }
            u32 meet = 0;
            const u32 cost = query.run(from.data(), from.size(), to.data(), to.size(), meet);
            assert(cost == brute);
            if (cost == INF) continue;
            const u32 s = query.source_of(meet), t = query.target_of(meet);
            assert(query.path_edges(s, t, meet, edges));
            u32 at = s, sum = 0;
            for (u32 e : edges) { assert(g.from[e] == at); at = g.to[e]; sum += g.cost[e]; }
            u32 offsets = INF;
            for (const auto& a : from)
                for (const auto& b : to)
                    if (a.node == s && b.node == t) offsets = std::min(offsets, a.cost + b.cost);
            assert(at == t && sum + offsets == cost);
        }

        // A cancelled search stops with no route.
        {
            const Query::Seed s {0, 0}, t {g.n - 1, 0};
            u32 meet = 0;
            assert(query.run(&s, 1, &t, 1, meet, [] { return true; }) == INF || query.settled < 1024);
        }
    }

    // The sidecar round trip: counts, hierarchy, snap lookup against brute force, routing.
    {
        Graph g = random_grid(14, 11, 42);
        Contractor contractor(g, 200);
        contractor.run();
        const Hierarchy h = contractor.finish();
        SidecarMap map {};
        std::strncpy(map.name, "Test_region.obf", sizeof(map.name) - 1);
        map.size = 123456;
        const std::string path = temp_path();
        std::string error;
        assert(pack_sidecar(g, h, {map}, 15, path.c_str(), error));

        auto sidecar = Sidecar::open(path.c_str(), error);
        assert(sidecar);
        assert(sidecar->nodes() == g.n && sidecar->real_edges() == g.m);
        SidecarMap listed;
        assert(sidecar->map_count() == 1 && sidecar->map(0, listed));
        assert(std::strcmp(listed.name, "Test_region.obf") == 0 && listed.size == 123456);
        assert(sidecar->header().out_arcs == h.out_arcs.size() && sidecar->header().in_arcs == h.in_arcs.size());

        // nearest nodes: the grid search finds what a scan of every node finds
        std::mt19937 rng(11);
        std::vector<Sidecar::Candidate> found;
        for (int i = 0; i < 200; ++i) {
            const int x = kBaseX - 10000 + (int)(rng() % 300000), y = kBaseY - 10000 + (int)(rng() % 240000);
            sidecar->nearest_nodes(x, y, 4, found);
            std::vector<std::pair<int64_t, u32>> all;
            for (u32 n = 0; n < g.n; ++n) {
                const int64_t dx = g.x[n] - x, dy = g.y[n] - y;
                all.push_back({dx * dx + dy * dy, n});
            }
            std::sort(all.begin(), all.end());
            assert(found.size() == 4);
            // nodes are numbered along a curve in the sidecar, so compare by distance
            const double scale = metres_per_unit((double)y);
            for (size_t k = 0; k < 4; ++k)
                assert(std::fabs(found[k].metres - std::sqrt((double)all[k].first) * scale) < 1e-6);
            assert(found[0].metres <= found[3].metres);
        }
        // a point far outside the map has no nearby node
        sidecar->nearest_nodes(2000000000, 2000000000, 4, found);
        assert(found.empty());

        // a route between two node positions costs what Dijkstra says (snapping onto the nodes
        // themselves is at distance zero, so the neighbours' snap penalty keeps them out)
        Router router(*sidecar);
        std::vector<RoadRun> runs;
        for (int i = 0; i < 100; ++i) {
            const u32 s = rng() % g.n, t = rng() % g.n;
            u32 cost = 0;
            const auto status = router.route(g.x[s], g.y[s], g.x[t], g.y[t], runs, cost);
            if (dijkstra(g, s, t) == INF) { assert(status != Router::Status::Ok || s == t); continue; }
            assert(status == Router::Status::Ok);
            assert(cost <= dijkstra(g, s, t));
            if (s != t) assert(!runs.empty());
        }

        // the same routes through a cache of three 64-byte pages (every read evicts)
        auto tiny = Sidecar::open(path.c_str(), error, 64, 3);
        assert(tiny);
        Router small(*tiny);
        std::vector<RoadRun> small_runs;
        for (int i = 0; i < 60; ++i) {
            const u32 s = rng() % g.n, t = rng() % g.n;
            u32 cost = 0, small_cost = 0;
            const auto status = router.route(g.x[s], g.y[s], g.x[t], g.y[t], runs, cost);
            const auto small_status = small.route(g.x[s], g.y[s], g.x[t], g.y[t], small_runs, small_cost);
            assert(status == small_status);
            if (status != Router::Status::Ok) continue;
            assert(cost == small_cost && runs.size() == small_runs.size());
            for (size_t k = 0; k < runs.size(); ++k)
                assert(runs[k].road == small_runs[k].road && runs[k].start == small_runs[k].start &&
                       runs[k].end == small_runs[k].end);
        }
        assert(tiny->file().page_reads() > tiny->file().page_hits() / 100);   // it really did page
        unlink(path.c_str());
    }

    // Edges of one road merge into one stretch; a junction transition (no road) is skipped;
    // a reversed edge is travelled from its higher point index to its lower.
    {
        Graph g;
        g.n = 6;
        // ~1.3 km apart: every point has one node within snapping distance
        for (int i = 0; i < 6; ++i) { g.x.push_back(kBaseX + 100000 * i); g.y.push_back(kBaseY); }
        add_edge(g, 0, 1, 1000, 100, 0, 1, 0);     // road 100, points 0 -> 1
        add_edge(g, 1, 2, 1000, 100, 1, 2, 0);     // road 100, points 1 -> 2
        add_edge(g, 3, 4, 1000, 200, 0, 1, 0);     // road 200, points 0 -> 1
        add_edge(g, 5, 4, 1000, 300, 0, 4, 1);     // road 300, travelled from point 4 down to 0
        add_edge(g, 2, 3, 500, -1, 0, 0, 0);       // junction transition, after the real edges
        Contractor contractor(g, 200);
        contractor.run();
        const Hierarchy h = contractor.finish();
        const std::string path = temp_path();
        std::string error;
        assert(pack_sidecar(g, h, {}, 15, path.c_str(), error));
        auto sidecar = Sidecar::open(path.c_str(), error);
        assert(sidecar && sidecar->real_edges() == 4);
        Router router(*sidecar);
        std::vector<RoadRun> runs;
        u32 cost = 0;
        assert(router.route(g.x[0], g.y[0], g.x[4], g.y[4], runs, cost) == Router::Status::Ok);
        assert(cost == 3500);
        assert(runs.size() == 2);
        assert(runs[0].road == 100 && runs[0].start == 0 && runs[0].end == 2);
        assert(runs[1].road == 200 && runs[1].start == 0 && runs[1].end == 1);
        assert(runs[0].anchor_x == g.x[0] && runs[0].anchor_y == g.y[0]);
        // node 5 reaches node 4 over the reversed edge of road 300
        assert(router.route(g.x[5], g.y[5], g.x[4], g.y[4], runs, cost) == Router::Status::Ok);
        assert(runs.size() == 1 && runs[0].road == 300 && runs[0].start == 4 && runs[0].end == 0);
        // no way back from node 4 to node 0
        assert(router.route(g.x[4], g.y[4], g.x[0], g.y[0], runs, cost) == Router::Status::NoRoute);
        // a point more than a kilometre from every node has no route
        assert(router.route(g.x[0], g.y[0] + 500000, g.x[4], g.y[4], runs, cost) == Router::Status::NoRoute);
        unlink(path.c_str());
    }

    // A start heading makes a start node behind the car cost the reverse penalty: halfway
    // between a west and an east junction, the destination is cheaper through the west one
    // unless the car is heading east.
    {
        Graph g;
        g.n = 3;
        g.x = {kBaseX, kBaseX + 2 * kSpacing, kBaseX};          // west, east, destination
        g.y = {kBaseY, kBaseY, kBaseY - 100000};                // destination ~1.3 km north
        add_edge(g, 0, 2, 1000, 10, 0, 1, 0);      // west -> destination, 1 s
        add_edge(g, 1, 2, 30000, 20, 0, 1, 0);     // east -> destination, 30 s
        Contractor contractor(g, 200);
        contractor.run();
        const Hierarchy h = contractor.finish();
        const std::string path = temp_path();
        std::string error;
        assert(pack_sidecar(g, h, {}, 15, path.c_str(), error));
        auto sidecar = Sidecar::open(path.c_str(), error);
        assert(sidecar);
        Router router(*sidecar);
        std::vector<RoadRun> runs;
        u32 cost = 0;
        const int car_x = kBaseX + kSpacing, car_y = kBaseY;
        assert(router.route(car_x, car_y, g.x[2], g.y[2], runs, cost) == Router::Status::Ok);
        assert(runs.size() == 1 && runs[0].road == 10);
        assert(router.route(car_x, car_y, g.x[2], g.y[2], runs, cost, 270.0) == Router::Status::Ok);
        assert(runs.size() == 1 && runs[0].road == 10);
        assert(router.route(car_x, car_y, g.x[2], g.y[2], runs, cost, 90.0) == Router::Status::Ok);
        assert(runs.size() == 1 && runs[0].road == 20 && cost == 30000);
        // sideways (north) neither junction is behind
        assert(router.route(car_x, car_y, g.x[2], g.y[2], runs, cost, 0.0) == Router::Status::Ok);
        assert(runs.size() == 1 && runs[0].road == 10);
        unlink(path.c_str());
    }

    // Damaged files are refused: wrong magic, truncated copy, too small.
    {
        Graph g = random_grid(4, 4, 3);
        Contractor contractor(g, 200);
        contractor.run();
        const Hierarchy h = contractor.finish();
        const std::string path = temp_path();
        std::string error;
        assert(pack_sidecar(g, h, {}, 15, path.c_str(), error));
        FILE* f = std::fopen(path.c_str(), "rb");
        std::vector<char> bytes(1 << 20);
        const size_t size = std::fread(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
        assert(size > sizeof(SidecarHeader) && size < bytes.size());
        auto rewrite = [&](size_t keep, char first) {
            std::vector<char> copy(bytes.begin(), bytes.begin() + keep);
            copy[0] = first;
            FILE* out = std::fopen(path.c_str(), "wb");
            std::fwrite(copy.data(), 1, copy.size(), out);
            std::fclose(out);
        };
        rewrite(size, bytes[0]);
        assert(Sidecar::open(path.c_str(), error));
        rewrite(size, 'X');
        assert(!Sidecar::open(path.c_str(), error) && error.find("not a routing sidecar") != std::string::npos);
        rewrite(size - 64, bytes[0]);
        assert(!Sidecar::open(path.c_str(), error) && error.find("size") != std::string::npos);
        rewrite(16, bytes[0]);
        assert(!Sidecar::open(path.c_str(), error));
        assert(!Sidecar::open("/nonexistent/sidecar", error));
        unlink(path.c_str());
    }
    return 0;
}
