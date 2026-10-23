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

## M1: CH route through OsmAnd's own result preparation (radio, 2026-10-19)

`route_bench ch` (ROUTE_BENCH_CH=<prefix of .coords/.ch/.meta>): nearest node for start and
end, CH query, unpack to original edges, merge consecutive edges of one road into
(road id, start index, end index), load each `RouteDataObject` with
`loadRouteSegment(anchor)` and the road id (as `HHRoutePlanner` does), build
`RouteSegmentResult(road, start, end)`, then the core's `prepareResult` (distance, time,
attached roads, turn types) and `makeStartEndPointsPrecise`. On M9304002366, all 26 maps
open, beside the running navigation:

| Route | HH | CH end to end | where the CH time goes |
|---|---|---|---|
| Kyiv -> Kherson | 13,836 ms, 615.0 km, 27,382 s | 2,685-3,075 ms, 614.2 km, 27,415 s | snap 106, query <1, roads 2,024-2,363 (995 tiles), prepare 554-602 |
| Maidan -> Troieshchyna | 5,170-5,454 ms, 15.2 km, 1,597 s | 662-800 ms, 12.8 km, 1,350 s | snap 106, query <1, roads 497-622 (80 road runs), prepare 59-71 |

Only the Kyiv map open: 15 km route 300-325 ms against HH 2,973 ms.

* The graph search is no longer the cost: loading the route's roads from the maps is
  (about 2 ms per tile). Next levers: a spatial grid for the start/end snap (106 ms of
  brute force here), loading only the tiles the route needs in one pass.
* The Kyiv-Kherson routes agree (OsmAnd's own time estimate for the CH route is 0.1% above
  HH's). The short route does **not**: CH picks 12.8 km / 1,350 s, HH 15.2 km / 1,597 s.
  Either the cost models differ or the CH route breaks a turn restriction or one-way
  that the graph does not model yet; this is what M2 has to settle before the CH route
  can be trusted.
* One-time cost: loading the whole-country hierarchy, coordinates and edge table
  (417 MB) from flash took 17 s; a product build must memory-map it instead.

## The product integration (M3, M4)

`ch_tool build` contracts the graph, `ch_tool pack <graph> <hierarchy> <out.chr> <map.obf>...`
writes the sidecar the radio reads: the hierarchy, the edge-to-road table, a snap grid with the
node coordinates, and the names and sizes of the maps it was built from. The worker
(`navigation_worker.cpp`, through `ch_planner.cpp`) uses `<map dir>/car.chr` when the open
maps are exactly those, tries CH first and falls back to HH, then A*; the straight-leg check
applies to CH routes too. `/datastore/navicore-backend/disable-ch` (or
`OPEN_NAVICORE_OSMAND_CH_DISABLE=1`) turns it off.

* The file is read through a 4 KB page cache (64 MB) over `pread`, not mapped: a 32-bit QNX
  process on the radio cannot map more than about 512 MB (`mmap` of the 1.39 GB sidecar fails
  with "Not enough memory", measured with a probe), and a route touches only a few thousand pages.
* Nodes are numbered along a Hilbert curve over the snap grid, so a route's arcs are close in
  the file: the 615 km Kyiv-Kherson route reads 2,048 pages (8 MB) cold; numbering by build order
  needed about 4,800 (79 MB).
* A route is refused (and the caller falls back) when a point is more than 1 km from every node,
  when start and end snap to one node, when a road is missing from the loaded maps, or when the
  sidecar was not built from exactly the loaded maps.
* Host test: `tests/ch_test.cpp` (hierarchy against Dijkstra, sidecar round trip, snap lookup,
  road-stretch merging, a three-page cache that evicts on every read, damaged files refused).

Whole Ukraine on the radio (26 maps open in `/navi_map/osm`, sidecar `car.chr` 1.39 GB read
from flash, beside the running navigation; ChPlanner through route_bench):

| Route | HH | CH, first route | CH, warm | where the warm CH time goes |
|---|---|---|---|---|
| Kyiv -> Kherson, 615.0 km (HH 615,033 m, CH 615,004 m) | 13,836 ms | 3,683 ms | 2,645 ms | search 13, roads 2,089 (995 tiles), prepare 543 |
| Maidan -> Troieshchyna, 12-15 km | 5,170-5,454 ms | 859 ms | 550 ms | search 6, roads 488, prepare 56 |

In the running product (installed 2026-10-19, autostart route from the bench marker, 26 maps):
`worker=ch_route search_ms=156 roads_ms=532 prepare_ms=68 runs=48 edges=109 settled=69`,
`worker=route_calculated method=ch ms=792` (HH logged 3.0-5.1 s for the same request on this
route earlier), then 8 maneuvers published and guidance started.

Opening the sidecar takes 35 ms (HH needs 10 s to open the 26 maps, which CH also needs once).
What is left of the CH time is loading the route's roads from the maps and the core's result
preparation; the search is a few milliseconds even through the page cache.

## M2: fidelity against OsmAnd's own routers (radio, Kyiv city map, 2026-10-19)

The graph models what the core's A* models: access, oneway, speed and priority of the `car`
profile; turn restrictions with the core's exact semantics (the candidates at a junction are
the *car-accessible* roads touching that point; NO_* forbids its target; an ONLY_* whose
target is a candidate forbids all other candidates; via restrictions are counted, not
modelled); and `calculateTurnTime`: half the `penalty_transition` difference between the two
roads, a roundabout entry/exit penalty, and 7.5/15/30 s above 30/60/120 degrees measured with
`directionRoute`'s 5 m look-ahead. Traffic-signal and barrier times are not modelled: they only
feed the reported time (`obstacle_time`), the routing cost uses `obstacle` (toll booths and
size limits, none by default). Junctions are expanded into per-edge nodes joined by transition
edges; only junctions where both roads end (a quarter of the two-road ones) fold into edge costs.

Restriction violations in random routes (`check_restrictions.py`, same semantics): naive graph
161 in 112 of 289 routes; with restrictions 0 in 294; with turn costs 0 in 290.

`route_bench compare` on 39 pairs of interior road shape points 2-20 km apart
(`gen_pairs.py`; junction coordinates tie between roads, so they are not used):

| CH model | reference | route time CH/ref (median, p90, max) | shared road (median) | reference ms | CH ms |
|---|---|---|---|---|---|
| restrictions + turn costs | plain A* (ground truth) | x0.998, x1.06, x1.13 | 93% | 21,326 | 407 |
| restrictions + turn costs | HH | x0.996, - , x1.13 | 91% | 2,851 | 375 |
| restrictions only, no turn costs | HH (60 junction-node pairs) | x1.001, x1.14, x1.36 | 79% | 3,177 | 351 |

Plain A* median 21.3 s (max 40.7 s) against CH 0.41 s; the CH route is within 6% of A*'s travel
time for 90% of the pairs and faster than A* by more than 3% for 8 of 39.

Whole Ukraine, restrictions and turn costs (PC): 15.2 M nodes, 30.8 M edges (22.3 M junction
transitions), graph built in 85 s, hierarchy contracted in about 15 minutes.

## What is modelled, and what is not (the first, simplified model)

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

All 26 regions as one graph (PC): 2,068,016 car roads, 22.3 M points, 3,333,879
junction nodes, 8,486,440 directed edges, built in 66 s; contracted in 93 s to
14,324,431 upward arcs (1.69 per base edge), 211.9 MB. `verify` on 300 random
pairs across the country: 0 mismatches, CH 0.075 ms (169 settled on average) against
Dijkstra 214 ms (1.6 M settled). Kyiv (Maidan) to Kherson (46.6354 N 32.6169 E): 614.7 km,
411.8 min by this cost model, CH 0.091 ms + 0.204 ms unpack (203 settled), Dijkstra
348 ms (2.2 M settled). Not run on the radio: `/navi_map` is read-only and the whole
set does not fit in `/datastore`.

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

#### Whole Ukraine on the radio: HH vs CH

All 26 regional OBFs in `/navi_map/osm` (2.3 GiB, hashes verified on the unit), run beside
the running navigation. HH is `route_bench hh` with every file open; CH is `ch_tool leanroute`
with the whole-country hierarchy (3,333,879 nodes, 14,324,431 upward arcs, 211.9 MB) and the
node coordinates (26.7 MB) from flash.

| Route | OsmAnd HH | CH query | CH query + unpack to base edges |
|---|---|---|---|
| Kyiv Maidan -> Kherson, HH 615.0 km / CH graph 614.7 km | 13,836 ms | 0.110 ms | 0.320 ms (1,591 base edges, 203 nodes settled) |
| Maidan -> Troieshchyna, 15.2 km | 5,170-5,454 ms (3 runs) | 0.070 ms | 0.095 ms |
| 3,000 random pairs (2,873 reachable), average | n/a | 0.153 ms | 1.07 ms |

* HH needs a one-time 9.8-10.1 s to open the 26 files; CH needs about 10 s to read its
  239 MB from flash. Neither is in the per-route figures.
* With all 26 files open HH takes 5.2-5.4 s on the 15 km route, against 3.0 s with only the
  Kyiv map open (tiles loaded 2,618 vs 1,305).
* The Kyiv-Kherson routes agree within 0.05% in distance (615.03 vs 614.70 km), so they are
  very likely the same road. Times differ by model: HH 27,382 s (OsmAnd, with turn and signal
  costs) against 26,231 s of profile cost for CH.
* CH figures are the graph search and unpacking only, as above. They exclude snapping the
  start/end to a road (the tool's brute-force nearest-node scan takes about 70 ms on the
  radio and is not part of CH), loading the route's roads, turn instructions, street names
  and geometry.
* A* was started on Kyiv-Kherson and dropped at the operator's request; the process was gone
  when I came to stop it and produced no result.

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
