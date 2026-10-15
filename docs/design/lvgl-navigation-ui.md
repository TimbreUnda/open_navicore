# LVGL navigation UI slice

The first implemented flow follows the selected 1024x574 night design:

`FreeDrive -> Search -> RoutePreview -> Guidance`

The 336-pixel rail, 64-pixel minimum touch targets, 6/8-pixel radii and night
palette are taken from design options 2a, 2b, 2c and 1b. Layouts still use the
actual render-target dimensions and degrade to a single pane on narrow targets.

## Architecture

- `NavigationModel` owns screen transitions and has no LVGL, GLES, Qt or
  OsmAnd types. Search and route requests carry monotonically increasing
  revisions; the navigation worker runs slow work off-thread and the model
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
make test   # or: ctest --test-dir build/host -R navigation_ui_test
```

`navigation-ui-test` builds the pinned LVGL source with the same project config
and exercises the complete state flow with test-only owned result data,
including provider press/release input and stale-result rejection.

## Honest current boundary

The target plugin now installs an asynchronous offline OsmAnd search/routing
worker. Results are copied back to the renderer thread, stale revisions are
rejected, and calculated route geometry is drawn in the preview. Starting a
route enables coordinate-based map following, remaining-distance/time updates,
and maneuver presentation. Missing coordinates produce an explicit paused
"No GPS fix" state instead of fabricated guidance.

Host tests cover search/route UI state, GPS delivery, progress projection,
maneuver advancement, monotonic behavior under backward jitter, arrival and
surface reattachment. Radio-side guidance remains bench qualification, not a
claim of a completed driven route. Position freshness, off-route detection,
rerouting, spoken prompts, and qualified speed/heading semantics remain to be
implemented.

An opt-in stationary-bench simulator can advance along calculated geometry
when the receiver has no fix. Simulated guidance is visibly labelled and uses
the same projection, maneuver and camera path as real coordinates; it does not
alter or fabricate incoming GPS events.
