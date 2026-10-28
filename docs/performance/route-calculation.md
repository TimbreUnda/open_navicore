# Route calculation

## Intermittent delays, 2026-10-28

Two independent delays were reproduced on M9304002366 using the product's
`NavigationWorker` in a separate process with the installed core, all 26 maps,
the CH sidecar, and Organic Maps turn generation. No UI simulation or service
restart was needed for these measurements.

Dropping a pin used to run reverse geocoding to replace the coordinates on its
card with a street/address. It used the same queue as routing. For Maidan
(50.4501, 30.5234) to Kozatskiy (50.451725, 30.5220848), requesting a route
100 ms after the lookup began waited **50,346 ms** in the queue; the route
calculation itself took 575 ms. Repeating the route took 408 ms overall.
Passing a cancellation controller reduced the first request to 13,564 ms, but
the core's initial map/road loading still held the queue for 12,894 ms.

Pin reverse geocoding is removed: the card retains its coordinates and distance,
and routing uses the selected coordinates directly. There is no extra address
thread. Named destinations and typed address search still use their existing
names/search path. The same hotel route now takes **572 ms**, then **342 ms**,
including worker publication; both produce 179 m / 16 geometry points.

Separately, the point (46.0, 31.0) has no CH route from Maidan. Previously it
silently switched to HH and returned a route after **12,508 / 10,077 ms**.
The selected CH router now reports that failure in **781 / 221 ms**. A missing
or stale selected sidecar also reports failure instead of changing engines.
The existing `disable-ch` marker/environment switch explicitly selects HH
(or A* for maps without HH); a failed HH query no longer retries with A*.
This intentionally trades automatic slow retries for visible route failure.

Successful routing remains functional: Kyiv–Dnipro (478,427 m / 4,707 points)
took 6,399 / 6,039 ms including Organic Maps turns. Explicit HH mode on
Maidan–Troieshchyna produced a 15,171 m route in 6,248 / 6,634 ms.

These figures exclude one-time router warm-up. A first request can still wait
for that initialization. `worker=request_begin` now records `queue_ms`,
`worker=route_ready` records the warm-up wait, and `worker=request_done` records
work and end-to-end time even for cancelled/failed requests. Existing CH-stage
and Organic Maps timings remain. The older `worker=placed runmask=1` field is
a success flag for allowing all CPUs, not a CPU0-only affinity mask.

`make` builds `bench/libroute_worker_bench.so` alongside `route_bench_run`.
Copy both to `/tmp` on the radio and supply the routing XML:

```sh
ROUTE_WORKER_MAX_MS=5000 /tmp/route_bench_run worker /navi_map/osm /tmp/routing.xml \
    /navi_map/osm/car.chr 50.4501 30.5234 50.451725 30.5220848 route
```

The runner uses the actual worker, timestamps its trace to stdout, repeats the
request twice, and checks the revision and route outcome. `reject` expects no
route, `cold` submits before warm-up, and sidecar `-` selects OsmAnd explicitly.
The optional latency limit applies to each submitted request; a 120-second
process timeout bounds a stuck diagnostic. It does not control the installed
navigation process. The runner is not included in the installed backend/image.

Validation: QNX build and ELF checks, 35 host tests, pin-card render, and the
four on-radio worker cases above. After installing the backend and rebooting once,
a real long-press pin at (50.45132, 30.52168) retained its coordinates and
produced an 88 m route preview: queue 0 ms, warm-up wait 0 ms, request work
957 ms, Organic Maps 11 ms. No address job was queued.

Earlier measurements below describe older
router implementations and hardware; they are retained as history.

## Earlier routing measurements

Measured on the bench unit (M9148801407, 2 × Cortex-A9) with the Kyiv map
(`kyiv.obf`, 87 MB, with two HH routing sections), 2026-10-15, using
`deploy/bench/route_bench.cpp` (see below).

## Where the time went

The routing worker called `RoutePlannerFrontEnd::searchRoute()` without an
HH configuration, so every route was plain A* over the detailed road graph
(core-legacy, OsmAnd's native router; "legacy" is its folder name, it is the
router the OsmAnd apps use). For a 15 km route A* visited 47,800 segments
and loaded 2,959 routing tiles (791 unloaded and 476 reloaded under the
64 MB limit): 15.4 s on a free core.

In the app it was slower again: nv_navigation is pinned to CPU1, where the
render thread (priority 20) takes about half the core, and the worker had no
placement of its own. Pinned to CPU1 at priority 15 beside the running
navigation, the same route took 20-36 s and a 25 km route 45 s. Three routes
(alternatives) with A* would be 1-2 minutes.

| | A* (before) | HH |
|---|---:|---:|
| 15 km, free core | 15.4 s | 3.2 s |
| 15 km, A* raised to 256 MB | 14.6 s | 3.2 s |
| 15 km, CPU1 at prio 15 (like the app) | 36.0 s | 7.3 s |
| 15 km, CPU0 at prio 9-10 | | 4.0 s |
| 15 km, either CPU at prio 9-10 | | 3.7 s |

The memory limit is not the problem (6%); the algorithm and the shared core
are.

## What changed

- **HH routing.** The worker runs `HHRoutePlanner` (highway hierarchy over
  the map's precomputed HH sections) and falls back to A* when the map has no
  HH data or HH finds nothing. Same route, same distance and time as A*.
- **Worker placement.** The worker thread may run on either CPU, at
  priority 9 (below the default 10, so it only takes idle time from the
  services on CPU0).
- **Alternatives.** Route preview asks for up to two alternatives, sorted by
  duration (`Fastest`, then `Shorter`/`Alternative`); a reroute during
  guidance asks for the fastest only. The map draws the selected card's
  route. OsmAnd's native HH alternatives never returned any; four core bugs
  are fixed by `patches/osmand-core/qnx-hh-alternatives.patch`.

Beside the running navigation (simulated guidance), worker at priority 9:

| Route | A*, fastest only (before) | HH, fastest + alternatives |
|---|---:|---:|
| Maidan → Lavra (5.6 km) | 4.8 s | 3.0 s (+2 alternatives) |
| Maidan → Troieshchyna (15 km) | 19.8-36 s | 5.9 s (+2) |
| Troieshchyna → Teremky (25 km) | 44.9 s | 7.7 s (+2) |

In the app the autostart route (Lavra) logged
`worker=route_calculated method=hh routes=3 ms=5140` (cold caches), with
rendering at 30 fps throughout.

## Decoded route data cache (reverted)

> Reverted 2026-10-16: the route was drawn through buildings with the cache
> in place. The patch file is gone; the numbers below are kept for reference
> only. Rebuild the OsmAnd core without it before re-testing.

HH unloads every routing tile before each detailed network segment and each
start/end search (the search state lives in the tiles' `RouteSegment`
wrappers), and the core decoded the same OBF blocks again each time: about
1,100 unloads for one 15 km route. That is CPU, not I/O: with the map
copied to RAM the route took 3.1 s against 3.2 s from flash.
`patches/osmand-core/qnx-routing-decoded-cache.patch` keeps the decoded
objects process-wide (48 MB cap) and rebuilds only the wrappers. Routes,
distances and alternatives are identical.

| HH + 2 alternatives, free core | Before | First request | Repeated |
|---|---:|---:|---:|
| 15 km | 5.2 s | 4.0 s | 3.4 s |
| 25 km | 7.3 s | 5.5 s | 4.8 s |

In the app the autostart route went from 5,140 ms to 2,991 ms.

## What is left in an HH route

For the 15 km route (3.2 s on a free core): attaching start and end to the
HH network 1.2 s (`findFirstLastSegments`), network routing plus detailed
geometry 1.6 s, result preparation 0.25 s; tile loading (1.9 s) is spread
over these. The detailed pass calls `unloadAllData()` before each network
segment and reloads its tiles (1,100 unloads; upstream marks it "needed for
proper multidijkstra"). Alternatives cost 20-120 ms to find and about
1-2 s for their detailed geometry. With the cache, a repeated 15 km
request spends about 1.6 s in tile loading (now mostly rebuilding the
per-point `RouteSegment` wrappers: one heap allocation per road point) and
1.0 s in the start/end search.

Further options, by expected gain for effort:

1. Publish the fastest route first and the alternatives when ready: the
   preview appears about 1-2 s sooner (alternatives' detailed geometry is
   most of their cost).
2. Reset the search state in place instead of `unloadAllData()` and fresh
   wrappers: up to about 1 s, needs care (which fields a search writes).
3. Start-side search from the current position in the background while the
   destination is being chosen (half of the 1.0 s start/end search).
4. Precomputed detailed geometry of the HH network segments in a file next
   to the map (per map version): removes the per-segment A* of the detailed
   pass, about 1 s, but needs a generator run and invalidation.

Not worth it: a decoded-tile file on disk (decoding from RAM is what the
cache already avoids, and I/O is not the cost), compiler flags (the core is
built with -O3), a larger memory limit (6%).

## Measuring

`route_bench` is a bench-only tool (not in navigation.ifs): a shared library
like the plugin plus a launcher.

```sh
make -C $BUILD/qnx route_bench route_bench_run
scp -O -p $BUILD/qnx/bench/libroute_bench.so $BUILD/qnx/bench/route_bench_run \
    $OSMAND_QNX_ROOT/source/resources/routing/routing.xml root@radio:/tmp/
# on the radio (MODE astar | hh | hhalt; `on -C 1 -p 15` imitates the old worker)
cd /tmp; ./route_bench_run /datastore/navicore-backend/maps/kyiv.obf /tmp/routing.xml \
    hhalt 64 50.4501 30.5234 50.5130 30.6040
```

`ROUTE_BENCH_CORE=/tmp/x/libOsmAndCore_shared.so` tries another core
without installing it (the library's RPATH would otherwise win).
