# Rendering architecture for stable 30 fps

Status: selected architecture; implementation and bench qualification remain.

## Target and definition of success

The target is the VP4R bench radio: two Cortex-A15 cores at 1.5 GHz, 2 GiB RAM,
and the IMG/PowerVR OpenGL ES 2 driver exposed through QNX Screen/EGL. The
visible navigation region is about 1024x576 inside the stock 1024x768 HMI,
but the provider target and LVGL overlay canvas are the full 1024x768. QNX `pidin`
reports a 215 MiB data/address-space figure for the current replacement process
after loading OsmAnd, its
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

## Approaches considered

| Approach | Runtime load | Porting risk | Map/data impact | 30 fps judgment |
|---|---:|---:|---|---|
| Tune current OsmAnd raster pipeline | Medium/high CPU while tiles appear; low GPU | Low | Keeps OBF and style | Useful short-term baseline, but cannot guarantee cold tile-boundary pacing because rasterization remains CPU work. |
| Pre-rendered raster/MBTiles basemap | Lowest CPU; simple GLES2 textured quads | Medium | Requires a second, potentially large map package and loses dynamic vector styling | Strong fallback and excellent benchmark control; viable for bounded regions, unattractive as the primary worldwide format. |
| Current MapLibre Native | GPU vector render with a good worker model | Very high on QNX 6.6/GCC 8/GLES2 | Requires MVT/PMTiles plus OsmAnd OBF for routing/search, or a new data pipeline | Rejected for the radio baseline. Current builds expose EGL/OpenGL backends, but the project is moving toward GLES3-era capabilities and a much newer toolchain/platform layer. |
| Tangram ES | GPU vector renderer designed for GLES2 and embedded Linux/Raspberry Pi | Medium/high | Requires MVT and a separate style/data package | Best external prototype/reference. Its small GLES2 core is attractive, but adopting it creates two map databases and depends on a comparatively quiet project. |
| Organic Maps drape | Mature GPU navigation renderer | Very high | Requires Organic Maps data/tooling and duplicates much of the navigation stack | Valuable design reference, not a practical component extraction for this provider. |
| Custom GLES2 renderer over OsmAnd primitives | Low render-thread CPU after tile preparation; bounded worker CPU | Medium/high but controllable | Keeps OBF, search, routing, and one installed map set | **Selected.** It removes CPU pixel rasterization while preserving the already-working OsmAnd integration and old GLES2 driver compatibility. |

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

## Selected architecture

Keep OsmAnd as the offline map reader, search engine, router, guidance source,
and render-style evaluator. Replace only the final basemap rasterizer and the
software UI compositor.

```text
                         immutable, revisioned tile products
OBF -> OsmAnd objects -> style evaluation -> geometry/label workers
                                                |
                                                v
                                      bounded CPU tile cache
                                                |
                         newest camera only     v
GPS/guidance -> camera mailbox ---------> GLES2 render owner
                                           | polygons: indexed VBOs
                                           | roads: extruded line meshes
                                           | icons/glyphs: texture atlases
                                           | route/marker: dynamic VBOs
                                           | UI: retained GPU overlay
                                           v
                                     borrowed default FBO
                                           v
                                      provider swap
```

The render owner never waits for tile I/O, OBF parsing, style evaluation,
triangulation, labels, search, or routing. Worker output is immutable. Uploads
are budgeted and consumed only at frame boundaries. When data is late, the
renderer reprojects the best already-resident parent/neighbor tile instead of
stalling or presenting an old camera.

This is “full GPU rendering” in the useful embedded sense: polygons, road
strokes, symbols, route, marker, and UI are rasterized by GLES2. OBF decoding,
style evaluation, geometry construction, glyph shaping, and routing remain CPU
tasks off the render thread. Moving those control tasks to shaders would add
complexity without improving frame pacing on this GPU.

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

Proceed with Gate 0 and Gate 1, then build the custom OsmAnd-primitives GLES2
backend. Use pre-rendered raster tiles as the control case and emergency
low-spec fallback. Do not spend the next iteration tuning Skia CPU rasterization
as if it can become a GPU-vector renderer, and do not port all of MapLibre or
Organic Maps before the small renderer proves the hardware budget.
