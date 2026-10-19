// Packs a road graph and its hierarchy into the routing sidecar the radio maps
// (backends/osmand/ch/ch_sidecar.h describes the layout).
#pragma once

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../backends/osmand/ch/ch_sidecar.h"
#include "ch_graph.h"

namespace onc_ch {

inline bool pack_sidecar(const Graph& g, const Hierarchy& h, const std::vector<SidecarMap>& maps,
                         u32 grid_shift, const char* path, std::string& error)
{
    if (h.n != g.n) { error = "hierarchy and graph have different node counts"; return false; }
    // original edges: roads first, junction transitions (road -1) after
    u32 real = 0;
    while (real < g.m && g.road[real] >= 0) ++real;
    for (u32 e = real; e < g.m; ++e)
        if (g.road[e] >= 0) { error = "graph edges are not ordered roads first, transitions last"; return false; }

    std::vector<SidecarEdge> edges(real);
    std::vector<SidecarRoad> roads;
    std::unordered_map<int64_t, u32> road_index;
    for (u32 e = 0; e < real; ++e) {
        auto it = road_index.find(g.road[e]);
        if (it == road_index.end()) {
            it = road_index.emplace(g.road[e], (u32)roads.size()).first;
            roads.push_back({g.road[e], g.x[g.from[e]], g.y[g.from[e]]});
        }
        const bool reversed = g.rev[e] != 0;
        edges[e] = {it->second, reversed ? g.pt[e] : g.pf[e], reversed ? g.pf[e] : g.pt[e]};
    }

    // snap grid over the nodes
    int32_t min_x = INT32_MAX, min_y = INT32_MAX, max_x = INT32_MIN, max_y = INT32_MIN;
    for (u32 i = 0; i < g.n; ++i) {
        min_x = std::min(min_x, g.x[i]); max_x = std::max(max_x, g.x[i]);
        min_y = std::min(min_y, g.y[i]); max_y = std::max(max_y, g.y[i]);
    }
    if (g.n == 0) { error = "empty graph"; return false; }
    const u32 grid_w = (u32)(((int64_t)max_x - min_x) >> grid_shift) + 1;
    const u32 grid_h = (u32)(((int64_t)max_y - min_y) >> grid_shift) + 1;
    const uint64_t cells = (uint64_t)grid_w * grid_h;
    if (cells > (1ull << 28)) { error = "snap grid too large; use a bigger cell"; return false; }
    std::vector<u32> grid_first(cells + 1, 0), grid_nodes(g.n);
    auto cell_of = [&](u32 i) {
        const u32 cx = (u32)(((int64_t)g.x[i] - min_x) >> grid_shift);
        const u32 cy = (u32)(((int64_t)g.y[i] - min_y) >> grid_shift);
        return (uint64_t)cy * grid_w + cx;
    };
    for (u32 i = 0; i < g.n; ++i) ++grid_first[cell_of(i) + 1];
    for (uint64_t c = 0; c < cells; ++c) grid_first[c + 1] += grid_first[c];
    std::vector<u32> fill(grid_first.begin(), grid_first.end() - 1);
    for (u32 i = 0; i < g.n; ++i) grid_nodes[fill[cell_of(i)]++] = i;

    std::vector<SidecarPoint> points(g.n);
    for (u32 i = 0; i < g.n; ++i) points[i] = {g.x[i], g.y[i]};

    SidecarHeader head {};
    std::memcpy(head.magic, kSidecarMagic, 8);
    head.version = kSidecarVersion;
    head.header_bytes = sizeof(SidecarHeader);
    head.nodes = g.n; head.real_edges = real; head.roads = (u32)roads.size(); head.maps = (u32)maps.size();
    head.out_arcs = (u32)h.out_arcs.size(); head.in_arcs = (u32)h.in_arcs.size();
    head.grid_min_x = min_x; head.grid_min_y = min_y;
    head.grid_shift = grid_shift; head.grid_w = grid_w; head.grid_h = grid_h;

    uint64_t position = sizeof(SidecarHeader);
    auto place = [&](uint64_t bytes) {
        position = (position + 7) & ~7ull;
        const uint64_t at = position;
        position += bytes;
        return at;
    };
    head.off_nodes = place((uint64_t)g.n * sizeof(SidecarPoint));
    head.off_out_first = place(((uint64_t)g.n + 1) * 4);
    head.off_in_first = place(((uint64_t)g.n + 1) * 4);
    head.off_out_arcs = place((uint64_t)h.out_arcs.size() * sizeof(QArc));
    head.off_in_arcs = place((uint64_t)h.in_arcs.size() * sizeof(QArc));
    head.off_edges = place((uint64_t)real * sizeof(SidecarEdge));
    head.off_roads = place((uint64_t)roads.size() * sizeof(SidecarRoad));
    head.off_grid_first = place((cells + 1) * 4);
    head.off_grid_nodes = place((uint64_t)g.n * 4);
    head.off_maps = place((uint64_t)maps.size() * sizeof(SidecarMap));
    head.file_bytes = (position + 7) & ~7ull;

    std::FILE* f = std::fopen(path, "wb");
    if (!f) { error = std::string("cannot write ") + path; return false; }
    uint64_t written = 0;
    bool ok = true;
    auto put = [&](uint64_t at, const void* data, uint64_t bytes) {
        static const char zeros[8] = {0};
        if (at > written) ok = ok && std::fwrite(zeros, 1, at - written, f) == at - written;
        if (bytes) ok = ok && std::fwrite(data, 1, bytes, f) == bytes;
        written = at + bytes;
    };
    put(0, &head, sizeof(head));
    put(head.off_nodes, points.data(), points.size() * sizeof(SidecarPoint));
    put(head.off_out_first, h.out_first.data(), h.out_first.size() * 4);
    put(head.off_in_first, h.in_first.data(), h.in_first.size() * 4);
    put(head.off_out_arcs, h.out_arcs.data(), h.out_arcs.size() * sizeof(QArc));
    put(head.off_in_arcs, h.in_arcs.data(), h.in_arcs.size() * sizeof(QArc));
    put(head.off_edges, edges.data(), edges.size() * sizeof(SidecarEdge));
    put(head.off_roads, roads.data(), roads.size() * sizeof(SidecarRoad));
    put(head.off_grid_first, grid_first.data(), grid_first.size() * 4);
    put(head.off_grid_nodes, grid_nodes.data(), grid_nodes.size() * 4);
    put(head.off_maps, maps.data(), maps.size() * sizeof(SidecarMap));
    put(head.file_bytes, nullptr, 0);
    ok = std::fclose(f) == 0 && ok;
    if (!ok) error = "write failed";
    return ok;
}

}  // namespace onc_ch
