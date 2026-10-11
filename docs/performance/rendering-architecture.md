# Rendering architecture for stable 30 fps

Status: first production path selected from live-radio evidence; retained symbol
rendering and final bench qualification remain.

## Target and definition of success

The target is the VP4R bench radio: two Cortex-A15 cores at 1.5 GHz, 2 GiB RAM,
and the IMG/PowerVR OpenGL ES 2 driver exposed through QNX Screen/EGL. The
visible navigation region is a 1024x574 Screen window at `(0,72)` inside the
stock 1024x768 HMI. QNX `pidin` reports about 200 MiB of process data after
loading OsmAnd, its
resources, Qt, the Kyiv OBF, LVGL, and the graphics stack.

The performance milestone is not an average FPS number. It requires a
repeatable moving-camera trace with:

- at least 30 presented frames per second over every rolling 5-second window;
- p95 CPU frame submission below 20 ms and p99 below 33.3 ms;
- no frame older than 100 ms presented after a newer camera state exists;
- no tile upload, route calculation, search, or label-layout task blocking the
  render thread for 20 ms;
- stable behavior while crossing tile boundaries, not only over a warm static
  viewport;
- the same result with the guidance UI visible;
- CPU and memory reported with the result. A cached-map run alone does not
  qualify cold movement.

Thirty fps is the acceptance floor, not a limiter or final target. Candidate
renderers also report uncapped weighted rate, rolling minimum, and CPU/GPU
headroom. Do not add a 30 Hz pacer: lower frame cost and higher achievable FPS
remain valuable for input latency, burst recovery, richer future scenes, and a
possible 60 Hz/direct-presentation path.

The provider owns the Screen window, EGL surface/context, and buffer swap. The
backend renders only while that context is current. Any design must preserve
that API boundary.

## Current frame path

```text
OBF
  -> ObfMapObjectsProvider                       worker CPU
  -> MapPrimitivesProvider / MapPrimitiviser     worker CPU
  -> Skia MapRasterLayerProvider_Software        worker CPU, 256x256 RGBA tiles
  -> OsmAnd AtlasMapRenderer_OpenGLES2plus       render thread, texture upload/draw
  -> LVGL full 1024x576 ARGB software canvas     render thread CPU
  -> glTexImage2D(full canvas)                    render thread upload
  -> one blended fullscreen quad                 GPU
  -> provider swap                               stock runtime
```

This uses OpenGL, but it is not a GPU-native map/UI pipeline. The basemap is
rasterized by Skia on the CPU before it becomes a texture. The checked-in
OsmAnd `MapRasterLayerProvider_GPU` is not an alternative: its implementation
contains `TODO: SkGpuDevice` and allocates the same CPU `SkBitmap` and
`SkCanvas` as the software provider.

The UI path has four avoidable costs on every requested frame:

1. `NavigationView::render()` clears the entire 3 MiB ARGB canvas.
2. It invalidates the whole LVGL screen and forces an immediate refresh.
3. Guidance updates delete and recreate the whole LVGL object tree.
4. `GlOverlayCompositor::present()` uses `glTexImage2D` for the entire surface
   and saves/restores GL state using many synchronous `glGet*` calls.

At 30 fps the UI upload alone is about 90 MiB/s before driver copies. More
importantly, reallocating the texture and querying GL state can serialize the
CPU with the PowerVR driver. This work continues even when most UI pixels have
not changed.

OsmAnd currently uploads at most one pending GPU resource per render-thread
frame because `gpuWorkerThreadEnabled` is false. That avoids a second EGL
context but couples texture arrival to frame submission. Enabling the worker
without a shared-context owner is invalid; it is not a safe one-line speedup.

An idle arrival-screen sample consumed approximately 1.0 s of user CPU in a
10 s interval (about 10% of one core) even though the camera was stationary.
That is a useful symptom, not a moving-route benchmark. The current trace does
not record presented-frame timestamps, stage time, queue age, or dropped camera
states, so it cannot establish FPS.

The first optimization patch adds opt-in one-second frame aggregates, retains
guidance widgets, suppresses repeated GPS-state logging, and reuses the overlay
texture. An unchanged UI is still composited after every map draw, but its
3 MiB canvas is no longer uploaded again. These changes need a live baseline
run; they do not establish that the current raster pipeline meets the target.

### First live baseline

The first instrumented simulation run used a 5.2 km route, a 200 m/s synthetic
position, rotating guidance, and the full 1024x768 target. That initial build
advanced the camera on 1 Hz GPS-unavailable events rather than interpolating
every presented frame, so this is a tile-churn/UI baseline rather than the final
smooth-motion qualification. After initial tile
arrival, one-second windows produced 10.7--19.2 presented fps, usually
13--17 fps. Representative p95 stages were 35--73 ms for map draw, 17--112 ms
for renderer update, and 25--54 ms for UI composition on the roughly one frame
per second that uploaded a changed guidance canvas. Those uploads were exactly
3,145,728 bytes each; unchanged frames uploaded zero bytes. The process accrued
about 5.35 seconds of user CPU time during a 10-second moving sample. No frame
fault occurred.

This run validates the measurement path and the retained-overlay optimization.
It also rejects “keep tuning the present raster pipeline” as the primary plan:
both OsmAnd CPU-side tile/update work and a full-canvas guidance upload can
independently exceed a 33.3 ms frame budget. The mixed-size tile mosaic visible
during movement confirms that resource arrival, not only swap cadence, is part
of the user-visible problem.

A second build corrected simulation to interpolate the camera continuously at
20 m/s, changed LVGL to a 64-row partial draw buffer, uploaded only the union of
dirty rectangles, and stopped rewriting unchanged labels. In a 20-second warm
moving sample it accrued 11.11 seconds of user CPU time and settled near
9--10 presented fps. Map-draw p95 was generally 89--152 ms, with a 2.98-second
total-frame outlier. UI composition was 0--5 ms p95 and UI traffic was only
about 17--68 KiB per second. The rendered guidance view remained visually
complete.

That result isolates the next decision: further LVGL tuning cannot recover the
missing 20 fps. The existing OsmAnd atlas path itself must be split into a
minimal GPU floor test, raster-without-symbols control, and replacement
basemap backend. The controls are needed because the current `map_render`
stage includes textured basemap tiles, a large independent symbol provider,
route vectors, resource upload, and driver synchronization.

### Live renderer shootout

The three-phase shootout ran on the radio with the same continuously moving
20 m/s Kyiv route. Each phase lasted 15 seconds and reset the synthetic camera
to the route start. Discarding the first two transition windows gave:

| Phase | Weighted presented fps | Stable range | Median total p95 | Median map p95 |
|---|---:|---:|---:|---:|
| Full OsmAnd map | 15.1 | 8.6--17.8 | 62.5 ms | 50.0 ms |
| Raster without the independent symbol provider | 18.4 | 14.5--20.5 | 54.0 ms | 33.5 ms |
| Route and marker only | 20.4 | 20.0--20.8 | 2.5 ms | 2.0 ms |

Removing symbols helps, but does not make the raster path viable. More
importantly, the route-only phase is locked near 20 fps despite requiring only
2--3 ms of measured backend work. The remaining roughly 47 ms is outside the
backend measurement and is consistent with the default `eglSwapBuffers`
cadence on this QNX/PowerVR window. The next radio build therefore requests
swap interval zero and explicitly paces presentation to 30 Hz. That separates
the compositor limit from renderer cost without busy-looping. If the route-only
control then holds 30 fps, the custom GLES2 renderer remains viable; if it does
not, the provider/window path must be fixed before investing in map meshes.

Two follow-up provider builds separated more of that external cost. Both used
the identical route, phase durations, and aggregation method:

| Provider policy | Full map | No symbols | Route-only floor | 52 s user CPU |
|---|---:|---:|---:|---:|
| Default swap/context churn | 15.1 fps | 18.4 fps | 20.4 fps | 21.05 s |
| Swap interval zero, explicit 30 Hz pacing | 17.6 fps | 19.7 fps | 22.5 fps | 14.01 s |
| Above plus persistent renderer-thread EGL context | 22.2 fps | 23.3 fps | 24.0 fps | 14.28 s |
| Persistent context, shortened 25 ms pacing control | 22.2 fps | 23.6 fps | 24.1 fps | 14.49 s |
| Persistent context, 30 Hz pacing, three Screen buffers | 23.2 fps | 23.4 fps | 24.2 fps | not sampled |
| Three buffers, no software pacing, full 1024x768 surface | 22.1 fps | 23.7 fps | 24.3 fps | not sampled |
| Three buffers, no pacing, visible 1024x574 band | 30.0 fps | 32.4 fps | 32.3 fps | not sampled |

The persistent-context route-only phase held 23.2--24.8 fps while backend p95
remained 2--3 ms. Full-map median map p95 improved to 25.5 ms, but its stable
range was only 15.9--24.8 fps and resource churn still produced long tails.
This proves three separate facts:

1. Rebinding and releasing EGL for every frame was material overhead and is no
   longer part of the architecture.
2. The software-raster basemap still cannot provide stable 30 fps even after
   the provider path is substantially cheaper.
3. A further provider/presentation cadence issue caps even trivial GPU scenes
   below 30 fps. It must be qualified with the custom-mesh and pre-rendered
   controls; fast backend time alone is not proof of visible 30 fps.

Shortening only the pacing interval from 33.3 ms to 25 ms changed the GPU floor
from 24.0 to 24.1 fps and did not materially change CPU consumption. The
aggressive interval is therefore rejected; the provider keeps the lower-load
33.3 ms interval. The cap is downstream of backend work and not imposed by the
pacer. The provider now reports the Screen window's swap interval, buffer
counts, and pipeline assignment so the next run can distinguish window policy
from EGL configuration without a separate Screen client.

The live Screen properties for the persistent-context build were
`screen_swap=0`, `buffer_count=2`, `render_buffers=2`, and `pipeline=1`.
Requesting three buffers produced `buffer_count=3` and `render_buffers=3`, but
the route-only floor remained 24.2 fps (23.2--24.9 stable range) with 2 ms
median map p95. Triple buffering is therefore not the missing 30 fps control;
the ceiling is downstream of buffer availability as well as backend work,
EGL context ownership, the requested swap interval, and the provider pacer.
The next presentation investigation should target the `NAV_main` Screen
class/pipeline/compositor policy or a proven direct-post path before map-mesh
work is used as evidence of visible frame rate.

Removing the provider pacer did not change the full-surface route-only floor.
Direct timing showed why: during the trivial scene, `eglSwapBuffers` itself
averaged roughly 38--40 ms and reached 45--50 ms, while the backend still used
only 2--3 ms. The panel is configured for 60 Hz, so this was composition
backpressure rather than panel refresh or provider scheduling.

The stock HMI exposes navigation only between its 72-pixel top chrome and
122-pixel bottom chrome. Rendering a 1024x574 Screen window positioned at
`(0,72)`, instead of a covered 1024x768 window, reduced submitted pixels by
25.3% without changing visible geometry or touch behavior. The route-only GPU
floor rose to 32.3 fps (31.6--32.9), and raster without symbols held 32.4 fps
(30.9--33.2). Warm full-map movement averaged 30.0 fps, but individual windows
still ranged from 25.2 to 32.5 fps and cold/resource-churn map p95 exceeded
100 ms. This proves the presentation path can exceed 30 fps and leaves the
CPU-raster basemap as the remaining blocker to stable movement.

The retained-mesh renderer should therefore target the visible 1024x574
surface from its first implementation. Full-screen hidden pixels, a
full-surface compatibility window, and explicit pre-swap pacing are not part of
the selected steady-state design. Once mesh rendering is stable, add a small
deadline limiter only if the natural Screen backpressure remains materially
above the desired 30 fps; do not reintroduce a fixed sleep ahead of a blocking
swap.

### Sustained moving-basemap qualification

The short shootout left one ambiguity: its symbol-free phase was only 15
seconds long. A dedicated mode therefore kept the real OsmAnd raster basemap,
route, moving camera, and guidance UI active while removing only the independent
`MapObjectsSymbolsProvider`. It did not cycle into a synthetic route-only phase.

On a 5.6 km Kyiv simulation at 20 m/s, the last uninterrupted trace contained
188 complete one-second movement windows. After discarding the first ten
seconds, all 174 rolling five-second windows remained above 30 fps:

| Metric | Result |
|---|---:|
| Weighted presented rate | 32.06 fps |
| Minimum rolling five-second rate | 30.77 fps |
| Maximum rolling five-second rate | 33.65 fps |
| Rolling windows below 30 fps | 0 / 174 |
| Median total p95 / map p95 | 3 ms / 2 ms |
| Worst observed frame | 119 ms |
| Renderer faults | 0 |

The long frame occurred at resource churn, but the retained neighboring frames
kept every five-second interval above the target. A separate 20-second active
movement sample accrued 4.894 CPU-seconds: 24.5% of one core, or 12.2% of the
radio's two-core capacity. Process data was approximately 200 MiB. Screenshots
at 4.5 km and 1.2 km remaining confirmed that the camera, route, basemap, and
guidance UI were genuinely moving rather than replaying a static surface.

This changes the first production choice. CPU-raster basemap generation is not
the immediate blocker once the surface is restricted to the visible band. The
independent OsmAnd symbol provider is. Replacing the entire basemap renderer
before replacing symbols would add the largest porting risk without solving the
now-measured bottleneck first.

## Approaches considered

| Approach | Runtime load | Porting risk | Map/data impact | 30 fps judgment |
|---|---:|---:|---|---|
| OsmAnd raster basemap without its independent symbol provider | 24.5% of one core during measured warm movement; low GPU | Low | Keeps OBF and style | **Selected first usable baseline.** Sustained live movement held every rolling five-second window above 30 fps. It still needs a replacement symbol layer and cold-start qualification, and its roughly 32 fps presentation rate is not treated as an optimization ceiling. |
| Pre-rendered raster/MBTiles basemap | Lowest CPU; simple GLES2 textured quads | Medium | Requires a second, potentially large map package and loses dynamic vector styling | Strong fallback and excellent benchmark control; viable for bounded regions, unattractive as the primary worldwide format. |
| Current MapLibre Native | GPU vector render with a good worker model | Very high on QNX 6.6/GCC 8/GLES2 | Requires MVT/PMTiles plus OsmAnd OBF for routing/search, or a new data pipeline | Rejected for the radio baseline. Current builds expose EGL/OpenGL backends, but the project is moving toward GLES3-era capabilities and a much newer toolchain/platform layer. |
| Tangram ES | GPU vector renderer designed for GLES2 and embedded Linux/Raspberry Pi | Medium/high | Requires MVT and a separate style/data package | Best external prototype/reference. Its small GLES2 core is attractive, but adopting it creates two map databases and depends on a comparatively quiet project. |
| Organic Maps drape | Mature GPU navigation renderer | Very high | Requires Organic Maps data/tooling and duplicates much of the navigation stack | Valuable design reference, not a practical component extraction for this provider. |
| Custom GLES2 renderer over OsmAnd primitives | Lowest render-thread CPU after tile preparation; bounded worker CPU | Medium/high but controllable | Keeps OBF, search, routing, and one installed map set | Retained as the performance track. Promote it whenever equal-content live tests show higher sustained FPS or meaningfully lower load, even if the raster baseline already clears 30 fps. |

References consulted:

- [MapLibre Native architecture](https://github.com/maplibre/maplibre-native/blob/main/ARCHITECTURE.md)
  separates style/tile parsing and buffer generation onto worker threads while
  keeping rendering on the context-owning thread.
- [MapLibre Native build options](https://github.com/maplibre/maplibre-native/blob/main/CMakeLists.txt)
  demonstrate explicit EGL/OpenGL backends, renderer modularity, and built-in
  benchmark/Tracy options, but also a modern CMake/toolchain surface.
- [Tangram ES](https://github.com/tangrams/tangram-es) is a C++ OpenGL ES
  2D/3D vector-map renderer with Linux and Raspberry Pi targets.
- [Organic Maps](https://github.com/organicmaps/organicmaps) provides the
  `drape`/`drape_frontend` GPU map architecture used by a complete offline
  navigator.
- [LVGL OpenGL ES draw backend](https://github.com/lvgl/lvgl/tree/master/src/draw/opengles)
  caches rendered widgets as GPU textures. The vendored LVGL implementation is
  still experimental for this driver and must pass a capability probe before
  adoption.
- [QNX Screen composition](https://www.qnx.com/developers/docs/7.1/com.qnx.doc.screen/topic/manual/cscreen_composition.html)
  documents hardware pipelines, overlay usage, and framebuffer composition;
  [Screen properties](https://www.qnx.com/developers/docs/8.0/com.qnx.doc.screen/topic/screen_8h_1Screen_Property_Types.html)
  defines swap interval, pipeline, and render-buffer semantics.

## Selected architecture

Keep OsmAnd as the offline map reader, search engine, router, guidance source,
style evaluator, and raster basemap producer. Do not attach its independent
dynamic symbol provider. Add labels and icons through a small retained GLES2
layer, and keep the already-optimized dirty-rectangle LVGL overlay until a
retained GPU UI is proven on the radio.

```text
OBF -> OsmAnd raster tile workers -> retained tile textures ----+
OBF -> bounded label selection -> retained glyph/icon atlases --+
GPS/guidance -> newest-camera mailbox --------------------------+--> GLES2 owner
route/marker -> small dynamic VBOs ------------------------------+       |
LVGL -> dirty rectangles -> retained overlay texture ------------+       v
                                                               provider swap
```

The render owner never waits for tile I/O, OBF parsing, label selection,
search, or routing. Worker output is immutable. Uploads are budgeted and
consumed only at frame boundaries. When data is late, the renderer keeps the
best already-resident tile instead of stalling or presenting an old camera.

The retained symbol layer follows the useful parts of mature GPU renderers
without importing their full platform stacks: cache glyph/icon textures,
retain drawables across camera frames, perform collision/priority work only
when the visible tile set changes, and adjust placement with uniforms. MapLibre
Native's dynamic atlases and cross-tile symbol index, Tangram ES's prioritized
collision model, and Organic Maps' split between scene/resource management and
graphics core all support this boundary.

A full GLES2 geometry backend remains behind the same renderer seam. It is not
the first implementation path because the measured raster baseline reaches a
usable result sooner. It remains the performance path even if raster plus
symbols clears 30 fps: promote it whenever an apples-to-apples live trace shows
higher sustained FPS or meaningfully lower load without losing visual quality.

### Map renderer scope

The first renderer deliberately supports the navigation style subset rather
than every OsmAnd rule:

- solid polygon fills for land, water, parks, and buildings;
- road casings and fills from pre-extruded triangle meshes;
- line joins/caps selected during worker-side mesh construction;
- a glyph atlas and icon atlas with GPU quads;
- decluttered labels computed at tile publication time and reused while moving;
- route line, destination, and vehicle marker as small dynamic layers;
- parent-tile fallback and overscaling while a detailed tile is unavailable.

Tile meshes are camera-independent and use tile-local fixed-point coordinates.
Camera movement changes uniforms, not vertices. VBOs and textures are retained
across frames and evicted by an explicit memory budget. Do not rebuild or
upload a tile because the car moved inside it.

### UI renderer

First remove the pathological work while retaining LVGL:

- preserve the canvas; do not clear/invalidate it every frame;
- mutate persistent guidance widgets instead of rebuilding the screen;
- allocate the GL texture once with `glTexImage2D`, then update dirty rectangles
  with `glTexSubImage2D`;
- replace per-frame `glGet*` state discovery with an explicit state contract at
  the OsmAnd/custom-renderer-to-UI boundary.

Then capability-test vendored LVGL's GLES2 backend. Adopt it only if the radio
provides the required OES/EXT entry points and it renders correctly inside the
borrowed context. Otherwise a tiny retained GLES2 overlay (atlas text, colored
quads, scissor rectangles) is lower risk and lower load than maintaining the
full software canvas. The navigation UI uses a small enough widget vocabulary
that this is realistic.

### Threading and scheduling

- The provider renderer thread is the sole owner of the borrowed EGL context
  and all visible GL commands.
- One map worker performs OBF read/style/mesh work. A second worker is allowed
  only after measurements show unused CPU headroom; the radio has two cores and
  the rest of the HMI must remain responsive.
- Search/routing remains on its existing worker but receives lower scheduling
  priority than frame delivery.
- Camera input is a latest-value mailbox. Intermediate GPS/simulation states
  may be coalesced; tile and route revisions may not be applied out of order.
- GPU uploads have a per-frame time/byte budget. Uploading never drains an
  unbounded queue before drawing.
- Rendering is event-driven when stationary and paced to display cadence while
  the camera is moving. It must not busy-loop from `!renderer->isIdle()`.

## Delivery sequence

### Gate 0: measure the current pipeline

Add low-overhead counters around update, prepare, map draw, UI render, UI
upload/draw, and the provider callback. Log one aggregate record per second:
presented/dropped/pending frames, p50/p95/p99 stage times, camera-state age,
tile queue depth, bytes uploaded, CPU time, and resident memory. Enable OsmAnd's
existing tile read/primitives/raster/sync metrics only in an explicit diagnostic
mode because their per-tile logging affects timing.

Run the same 60-second deterministic route trace for every candidate:

1. warm stationary viewport;
2. warm straight movement;
3. warm turns with rotating camera;
4. cold movement across tile boundaries;
5. cold movement with guidance UI changes;
6. route calculation while movement continues.

### Gate 1: remove known UI waste

Retain LVGL temporarily, update widgets in place, upload dirty rectangles, and
remove synchronous GL state queries. This should reduce idle CPU and separates
UI cost from basemap cost. It does not qualify the final map architecture.

### Gate 2: renderer shoot-out

Use the same camera trace and visual scene for three small backends behind one
render interface:

1. existing OsmAnd CPU-raster tiles;
2. pre-rendered raster tiles (performance floor/reference);
3. custom GLES2 meshes generated from OsmAnd primitives.

Tangram ES is retained as a time-boxed compatibility spike only if custom
geometry extraction is blocked. It is not the default dependency.

### Gate 3: productionize the winner

Promote the custom GPU renderer only after it passes all movement gates.
Validate context loss, surface recreation, stock-HMI visibility changes,
bounded cache eviction, missing/corrupt tiles, and route/search concurrency.
Keep the old renderer selectable at build time until the new path passes those
lifecycle tests; do not ship a runtime dual-render fallback that doubles memory
or complicates frame ownership.

## Immediate decision

Gate 0, Gate 1, and the sustained basemap control are complete. Build the
retained GLES2 symbol layer next: one bounded glyph atlas, one icon atlas,
priority-based collision, cross-tile identity, and camera-uniform updates with
no per-frame allocation. Keep the visible-band Screen surface, persistent EGL
context, triple buffering, no fixed pre-swap sleep, bounded uploads, and the
dirty-rectangle LVGL path.

Re-run the same sustained route with road/place labels enabled. If every
rolling five-second window remains at or above 30 fps and cold movement stays
within the CPU/memory budget, it qualifies as the first usable renderer—not as
the end of performance work. In parallel with subsequent product features,
activate stage two through the existing renderer seam and compare raster tiles
against OsmAnd-primitives polygon/road meshes uncapped. Prefer the faster,
lower-load result once it preserves the same visual content; do not import all
of MapLibre or Organic Maps before that smaller renderer proves the hardware
budget.
