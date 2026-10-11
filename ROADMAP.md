# Open NaviCore roadmap

This document tracks demonstrated project readiness, not intent. A checked box
means that the listed acceptance evidence exists. It does not imply that a
larger milestone containing it is complete.

## Performance policy

- 30 FPS in **every rolling five-second moving-map window** is the acceptance
  floor, never a frame-rate target or cap.
- Candidates run uncapped and are ranked first by sustained FPS with equivalent
  visible content, then by frame-time tails, CPU/GPU load, memory, and latency.
- Static, warm-cache, or symbol-free results do not qualify a complete renderer.
- Visual correctness, route calculation, and crash-free lifecycle behavior are
  gates; a faster incomplete frame is useful evidence but not feature parity.

## Ready foundations

- [x] Replace the QR39.5/QR39.6 provider while preserving the stock HMI process.
- [x] Recover 140/140 stock-reachable provider calls.
- [x] Recover 199/199 PAS application callback slots across 25 interfaces.
- [x] Preserve the provider lifecycle, HMI replies, GPS delivery, input, and
  Screen/EGL/GLES2 presentation on the stationary bench radio.
- [x] Provide a versioned C backend seam with a deterministic reference backend.
- [x] Build the native ARMv7/QNX artifacts reproducibly without repository-local
  absolute workstation paths.
- [x] Render an OsmAnd Kyiv basemap inside the stock navigation window.
- [x] Calculate and display a demo route using bench GPS simulation.
- [x] Keep LVGL as an overlay rather than copying map pixels through LVGL.

## Current performance checkpoint

- [x] Instrument presented frames, render stages, faults, and UI upload volume.
- [x] Establish a repeatable moving-route benchmark and rolling-window parser.
- [x] Demonstrate the symbol-free raster path for 188 seconds at 32.06 weighted
  FPS, with a 30.77 FPS minimum across 174 rolling five-second windows and zero
  windows below 30 FPS.
- [x] Reject baked symbols: 18.67 weighted FPS on the live radio.
- [x] Reject the bounded stock symbol path: 17.99 weighted FPS after warm-up,
  plus malformed oversized imagery.
- [x] Reject the first retained-atlas worker lifecycle: its initializer started
  the thread before later request-state members were constructed and crashed the
  QNX process. The corrected construction order still awaits live qualification.
- [ ] Render a visually complete moving map with labels and icons while keeping
  every rolling five-second window at or above 30 FPS.
- [ ] Improve beyond the current floor; no artificial 30 Hz pacing is planned.
- [ ] Record comparable CPU, memory, cold-tile, frame-age, and input-latency data
  for every surviving renderer candidate.

## Milestone 1 — stable navigation vertical slice

- [x] Home and destination-search screens render through LVGL.
- [x] A destination can be selected and routed with local OsmAnd data.
- [x] A simulated position can drive the route on a bench without a GPS fix.
- [ ] Qualify search, route selection, guidance, hide/show, service restart, and
  EGL context recovery in one repeatable live-radio run.
- [ ] Remove experimental feature markers from the normal startup path.
- [ ] Add a release manifest containing exact provider/backend versions and a
  reproducible bench validation report.

## Milestone 2 — production map renderer

- [ ] Live-qualify the corrected retained-atlas construction order; if it still
  fails, replace the extra thread with a renderer-owned asynchronous path.
- [ ] Retain labels/icons across moving frames; recompute only when visibility or
  collision state materially changes.
- [ ] Preserve road-label orientation, icon anchors, ordering, collision rules,
  language, and density closely enough for normal navigation.
- [ ] Eliminate avoidable full-surface LVGL invalidation and texture allocation.
- [ ] Measure whether a GPU-native vector path beats retained raster tiles on the
  PowerVR driver with equal content; select from evidence rather than design
  preference.
- [ ] Qualify warm and cold tile-boundary movement at the uncapped maximum rate.

## Milestone 3 — guidance product

- [ ] Apply the approved visual system to home, search, results, route preview,
  active guidance, maneuvers, and arrival states.
- [ ] Connect meaningful maneuver, distance, ETA, reroute, and arrival semantics.
- [ ] Add voice guidance and stock audio-focus integration.
- [ ] Add cluster guidance using recovered typed callbacks.
- [ ] Define and test degraded states for missing GPS, missing map data, route
  failure, storage failure, and renderer recovery.

## Milestone 4 — feature expansion

- [ ] Favorites, recent destinations, home/work, categories, and POI details.
- [ ] Route alternatives, avoidances, waypoint editing, and trip overview.
- [ ] Traffic ingestion and presentation where a lawful data source is available.
- [ ] Localization, units, accessibility, and driver-distraction behavior.
- [ ] Offline map/package lifecycle and safe updates.

## Release gate

A build is release-ready only when all of these are checked:

- [ ] No crash, process restart, radio reboot, or stock-stack corruption during
  the complete qualification sequence.
- [ ] Visually complete guidance stays at or above 30 FPS in every rolling
  five-second window, with the achieved uncapped FPS published.
- [ ] Search, routing, GPS/simulation, touch, audio, cluster, hide/show, restart,
  and context-loss cases pass on the target radio.
- [ ] Stock backup and a tested recovery path remain available.
- [ ] Public documentation distinguishes verified behavior from dormant ABI
  capability and planned features.

