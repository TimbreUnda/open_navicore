# Open NaviCore roadmap

This document tracks demonstrated project readiness, not intent. A checked box
means that the listed acceptance evidence exists. It does not imply that a
larger milestone containing it is complete.

## Current state (2026-10-16)

- Maps: the map path is a directory; every `*.obf` in it is loaded
  (`/navi_map/osm`: 26 Ukraine regional files and the OsmAnd world basemap).
  Tiles are built from z4 to z13 (the basemap up to z10, the regional files
  from z11); sea, roads, borders and places are drawn at low zoom.
- Language: the UI strings follow the radio language (en, uk, fr-CA, es-MX);
  map labels and search results follow it too (checked live in Ukrainian; the
  other languages are host-tested only).
- Crimea is drawn as part of Ukraine (no country border around it, the Russian
  "Республика Крым" entity is not drawn).
- Routing: highway-hierarchy results with straight filler legs are rejected and
  routed again with A*; alternatives are off (fastest route only).
- Branch history before: `feat/production-offline`.
- Renderer: the GPU vector map is the only map renderer. OsmAnd provides the
  camera, routing and search; the OsmAnd raster path, its experiments and the
  vector/raster crash guards were removed (see `backends/osmand/README.md`).
- UI: Uconnect navigation design (free drive, search, route preview,
  guidance, place and arrival cards, lanes, speed limit, off-route
  recalculation).
- Install: `deploy/navigation-ifs/` builds `navigation.ifs`, which makes Open
  NaviCore the default navigation provider at boot with no fallback to the
  native NaviCore.

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
- [x] Eliminate the retained-atlas constructor race and verify live that the
  worker starts only after the first presented frame.
- [x] Reject concurrent symbol extraction through the shared
  `MapObjectsSymbolsProvider`: the process restarts before atlas publication.
- [ ] Render a visually complete moving map with labels and icons while keeping
  every rolling five-second window at or above 30 FPS.
- [ ] Raise the presentation ceiling. The route-only GPU floor (32.3 FPS, 2 ms
  backend) equals the symbol-free map rate, so Screen composition, not map
  work, currently bounds FPS. Stock `graphics.conf` places `NAV_main` below the
  full-screen RGBA `HMI_main` on composited pipeline 4, so every map frame is
  recomposited. Qualify, one change at a time, against the route-only floor:
  - [x] opaque `NAV_main` (`SCREEN_TRANSPARENCY_NONE`), now the default:
    32.61 fps versus the 32.06 reference, within run-to-run spread; no
    measurable ceiling change;
  - [x] `SCREEN_USAGE_OVERLAY` window usage: accepted but Screen kept pipeline
    1 (32.69 fps); an explicit pipeline 2 was rejected;
  - [x] rule out vsync and our own GPU work: glFinish 0.04-0.4 ms, swap
    18-36 ms continuous; the ~27 ms is Screen's composition/post path;
  - [x] overlay usage on video1 (pipeline 1): reported applied, no change;
  - [ ] enumerate display-2 windows from a display-manager context to see what
    Screen composites per NAV post;
  - [ ] a non-composited layer Screen actually grants (a dedicated
    `graphics.conf` class on video1), including HMI popups and RVC ordering.
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

- [ ] Replace shared-provider concurrent extraction with serialized ownership,
  a dedicated provider graph, or a renderer-owned asynchronous path.
  (Dedicated private provider graph implemented offline; needs a live run.)
- [ ] Retain labels/icons across moving frames; recompute only when visibility or
  collision state materially changes. (64-symbol retained atlas with
  placement hysteresis implemented offline; needs a live run.)
- [ ] Persistent tile pack cache with a shipped pre-rendered base and
  pre-cache manifests (implemented and host-tested; needs live hit-rate,
  stutter and storage measurements).
- [ ] Car camera: heading-up, speed zoom, GPS extrapolation, pan/recenter,
  overview (host-tested; needs a live drive-feel check).
- [ ] Preserve road-label orientation, icon anchors, ordering, collision rules,
  language, and density closely enough for normal navigation.
- [x] Upload only dirty LVGL rectangles into a retained overlay texture.
- [ ] Blend the overlay only over occupied canvas bands and skip it entirely
  when the UI is transparent (implemented; needs live frame-time evidence).
- [ ] Replace per-frame `glGet*` save/restore at the OsmAnd/overlay boundary
  with an explicit state contract once OsmAnd's VAO emulation is accounted for.
- [x] Select the map renderer from evidence: the GPU vector map replaced the
  OsmAnd raster path (tile fetch 1.5-2.3 s -> ~0.33 s per tile, 1% worst
  frames roughly halved on the bench drive). The raster-path items (GPU upload
  worker, RGB565 tiles, tile-bitmap release, raster control) are superseded
  and their code removed.
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
  (Recent destinations persist and route in one tap.)
- [ ] Route alternatives, avoidances, waypoint editing, and trip overview.
  (Alternatives are implemented but off: they cost routing time. One switch,
  `kAlternativeRoutes` in `navigation_worker.cpp`, brings them back.)
- [ ] Traffic ingestion and presentation where a lawful data source is available.
- [ ] Localization, units, accessibility, and driver-distraction behavior.
  (UI strings en/uk/fr-CA/es-MX with the Ukrainian screens checked on the
  radio; units are metric only; the fr-CA/es-MX wording is unreviewed.)
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

