# Contraction-hierarchy spike (PC only)

Feasibility spike: build a car routing graph from the OBF map files, contract it
into a hierarchy offline, and measure query time. Nothing here runs on the radio
or is part of the product build.

```sh
# 1. OBF routing sections -> directed car graph (needs numpy)
python3 build_graph.py <routing.xml> out.graph Ukraine_*_europe_2.obf
# 2. contract, then check every query against plain Dijkstra
g++ -O2 -std=c++17 -o ch_tool ch_tool.cpp
./ch_tool build  out.graph out.ch
./ch_tool verify out.graph out.ch 3000
./ch_tool route  out.graph out.ch 50.4501 30.5234 50.5130 30.6040
```

| File | What |
|---|---|
| `obf_reader.py` | OBF container: top-level sections, OsmAnd's wire type 6 (4-byte big-endian length) |
| `obf_routing.py` | routing section -> roads (tile box tree, points, rule ids), recovered from core-legacy `binaryRead.cpp` |
| `obf_schema.py` | the OBF protobuf schema decoded from the generated `OBF.pb.cc` (there is no .proto) |
| `hh_info.py` | HH sections of OBF files: profile, parameters, edition |
| `routing_rules.py` | evaluates the `car` profile of `routing.xml` (access, oneway, speed, priority) |
| `build_graph.py` | junction graph, edge cost = length / (speed * priority) |
| `ch_tool.cpp` | contraction, bidirectional upward query with stall-on-demand, shortcut unpacking, verification |

## What is modelled, and what is not

Modelled: access, oneway, speed (maxspeed or class default, clamped to the profile
min/max) and priority of the `car` profile with no parameters set.

Not modelled yet: point obstacles (traffic signals, barriers), turn penalties,
`penalty_transition`, turn restrictions, directional `maxspeed:forward/backward`,
snapping to the middle of an edge (queries snap to the nearest junction).
Route lengths therefore do not match OsmAnd's HH routes and must not be compared
to them as if they did.

## Results (PC, one core, 2026-10-18)

| Map | Graph | Contraction | Hierarchy | CH query | Dijkstra |
|---|---|---|---|---|---|
| Kherson | 67,694 nodes, 181,785 edges | 1.5 s | 1.70 upward arcs per edge, 4.5 MB | 0.008 ms, 58 settled | 1.31 ms, 21,191 settled |
| Kyiv city | 89,272 nodes, 203,517 edges | 1.3 s | 1.63 arcs per edge, 5.1 MB | 0.018 ms, 98 settled | 2.84 ms, 43,287 settled |

`verify` on 2,000 (Kherson) and 3,000 (Kyiv city) random pairs: every cost equals
Dijkstra's and every unpacked path is a connected edge chain with that cost.
Kyiv test routes (nearest-junction snapping, guessed coordinates for Lavra and
Teremky): 4.8, 12.8 and 27.4 km, 0.02-0.03 ms plus 0.005-0.023 ms to unpack.

All 26 regions as one graph: 2,068,016 car roads, 22.3 M points, 3,333,879
junction nodes, 8,486,440 directed edges, built in 66 s. Not contracted
(stopped by the operator: the proof of concept is Kyiv/Kherson).

### On the radio (M9304002366, 2 x Cortex-A15 1.5 GHz per `pidin info`, 2026-10-18, beside the running navigation)

`ch_tool` cross-compiled with the product toolchain, run from `/tmp`; Kyiv city graph
(89,272 nodes). Costs, edge counts and lengths equal the PC run, so the ARM build is
correct. The unit's clock ticks in whole milliseconds, so `bench` and `benchfull` time a
batch of 5,000 random pairs (4,777 reachable) and divide.

| What | Time per route |
|---|---|
| CH query (`bench`) | 0.073 ms |
| CH query + unpack to base edges (`benchfull`) | 0.126 ms |
| Plain Dijkstra, same graph, same CPU | 19-20 ms |
| OsmAnd HH (`route_bench hh`), Maidan -> Troieshchyna, 15.17 km | 2,973 ms |
| OsmAnd A* (`route_bench astar`), same pair, same 15,171 m route | 14,724 ms |

The CH figure is the graph search plus unpacking only. It does not include attaching the
start and end to a road, loading the route's roads, turn instructions, street names or
geometry, which are part of the HH and A* figures (HH: about 1.2 s attaching start/end,
0.25 s result preparation, the rest network search, detailed geometry and tile loading).
CH graph routes use a simplified cost model (no turn penalties), so the 12.8 km CH route
and the 15.17 km HH route are different routes; compare times, not routes.

Not measured: memory-mapped loading, and the guidance step (street names and turns for
the route).

## Findings

* All 26 `Ukraine_*_europe_2.obf` carry two HH sections: `car`
  (avoid_toll, prefer_unpaved, avoid_motorway) and `bicycle`, edition 2026-10-03.
* The routing section holds the roads, so the CH can be built from the same OBF
  files the radio already has, with the same road ids (`road_id` per edge).
* Road lookup by id is not an OBF feature. The HH planner finds a road with
  `loadRouteSegment(x31, y31)` and compares ids (`hhRoutePlanner.cpp`), so a
  sidecar must carry one coordinate per road.
