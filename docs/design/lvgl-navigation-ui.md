# LVGL navigation UI slice

The first implemented flow follows the selected 1024x574 night design:

`FreeDrive -> Search -> RoutePreview -> Guidance`

The 336-pixel rail, 64-pixel minimum touch targets, 6/8-pixel radii and night
palette are taken from design options 2a, 2b, 2c and 1b. Layouts still use the
actual render-target dimensions and degrade to a single pane on narrow targets.

## Architecture

- `NavigationModel` owns screen transitions and has no LVGL, GLES, Qt or
  OsmAnd types. Search and route requests carry monotonically increasing
  revisions; a future coordinator can run slow work off-thread and the model
  rejects stale copied results when they return.
- `NavigationView` maps model state to LVGL widgets. LVGL 9.6.0 is pinned as a
  Git submodule. Its software renderer writes one owned, full-screen ARGB8888
  buffer; its QNX and OpenGL drivers are disabled.
- `GlOverlayCompositor` uploads that buffer as a GLES2 texture and alpha blends
  it after OsmAnd renders the map. It never creates, binds or swaps EGL/Screen
  objects. Creation, input, drawing and GL release all remain on the serialized
  provider renderer thread. Context-loss cleanup deliberately skips GL calls.
- The stock pointer callback feeds a bounded LVGL input queue so press/release
  edges are not collapsed before LVGL reads them.

Initialize submodules before building:

```sh
git submodule update --init --recursive
make navigation-ui-test
```

`navigation-ui-test` builds the pinned LVGL source with the same project config
and exercises the complete state flow with test-only owned result data,
including provider press/release input and stale-result rejection.

## Honest current boundary

The target plugin does not yet install a search/routing coordinator. The
inspected QNX OsmAnd archive exposes offline address/amenity search primitives,
but they are not wired by this slice. It exposes no route-planner symbols and
its QNX build excludes routing. Therefore this change establishes the working
UI, input and rendering architecture, but does **not** claim working radio-side
destination search, calculated route geometry or guidance.

The next implementation step is an offline OsmAnd search adapter that copies
results back to the renderer thread. A real route planner must then be added to
the QNX dependency bundle (or implemented as a separate qualified service)
before the route preview can display calculated alternatives and geometry.
