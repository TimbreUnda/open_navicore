# OsmAnd backend

This optional ARM/QNX backend renders OsmAnd into Open NaviCore's borrowed EGL
target. PR #2 (`9d87881624ad1609449dc9200dcef582e297a2ed`) proved the Kyiv map in
the stock UI on an unlocked stationary bench unit. It now includes an LVGL
search/route/guidance flow, asynchronous offline routing, route geometry and
coordinate-based map following. The new guidance slice has host and cross-build
verification and still needs the bench qualification below. Pan/zoom controls,
rerouting, audio and cluster output are not implemented.

## Components and ownership

| Component | Responsibility |
|---|---|
| `osmand_backend.cpp` | Query-V1/backend-V2 C entry points, ABI validation, exception containment, EGL current-context query |
| `config.*` | Owned environment snapshot at start |
| `session.*` | Lifecycle, attachment generations, worker wakeups, frame policy and rollback decisions |
| `engine.h` | Small injectable engine interface; no Qt/OsmAnd types |
| `osmand_engine.cpp` | Core, resource bundle, map pipeline and renderer ownership |
| `navigation_worker.*` | Off-thread offline search and legacy OsmAnd route calculation |
| `guidance_tracker.*` | Route projection, monotonic progress and maneuver selection |
| `ui/compact_text.*` | Bounded Cyrillic-to-Latin conversion for the compact LVGL font |
| `ui/navigation_model.*` | Host-testable destination/search/route state with revisioned asynchronous result seams |
| `ui/navigation_view.*` | LVGL 9.6 software-rendered 2a/2b/2c/1b screen flow |
| `ui/gl_overlay_compositor.*` | GLES2 texture composition inside the already-current borrowed context |
| `diagnostics.*` | Nonthrowing, best-effort trace to `/tmp/open-navicore-osmand.log` |

The provider owns Screen/EGL display, window, surface and context. The backend
never creates, destroys, binds or swaps those objects. Callbacks execute on the
renderer thread; only the engine's frame wakeup may arrive from a worker.
The C boundary hides C++ types from the provider API, but does **not** isolate
the C++ runtime within the process. The plugin, OsmAnd core, PIC Qt and compiler
runtime are one qualified binary set. In the local build, undefined C++ symbols
in the plugin resolve from the core even with `-static-libstdc++`,
`-static-libgcc` and `-Bsymbolic`.

## Runtime contract

The plugin's public ABI, query sizes and build identifier are unchanged.
Each input must have the expected ABI version and sufficient structure size
before any payload field is read. An accessible two-word header remains the
caller's responsibility. Unknown tail fields are ignored. All exported and
callback entry points contain C++ exceptions; a void cleanup callback records
quarantine instead of propagating a failure.

| State | Accepted transition |
|---|---|
| Stopped | `start` snapshots config/services and enters Started |
| Started | `surface_created` enters Attaching; duplicate start is rejected |
| Attaching | Success enters Attached; safe rollback returns to Started; unsafe rollback enters Quarantined |
| Attached | Frames return Pending, Presented or Fault; duplicate attach is rejected |
| Faulted | Rendering stops; normal surface teardown still releases the engine |
| Quarantined | Absorbing state; no new engine/core initialization until process restart |

`surface_destroyed` is idempotent and returns a safely released Attached or
Faulted session to Started. `stop` is idempotent and enters Stopped unless the
session is quarantined. A successful teardown permits another attach or
stop/start, but these restart paths still need testing with the real bundle.

At attach and render, the supplied EGL context must be current. At detach,
the adapter compares `eglGetCurrentContext()` with the attached context.
Only an exact nonzero match permits `releaseRendering(false)`. No context or
a different current context selects `releaseRendering(true)`, OsmAnd's
context-loss path. Stop has no current-context guarantee and also uses that
path if surface teardown was omitted.

The runtime calls `surface_destroyed` only for a successful attach, so failure
rollback happens inside attach while its expected context is still current.
The loader defers teardown following a render fault until the ordinary
surface-destroyed/stop sequence. The backend does not tear down GL inside render.

Each attachment creates a new reference-counted wakeup gate with an immutable
service snapshot and generation. Workers capture that gate, never the session.
Detach, stop, initialization failure and frame fault revoke it before cleanup.
Old callbacks cannot read a later session's services. A callback already racing
with revocation may finish one request using the old runtime token; the runtime
rejects stale lifecycle tokens. Within the same start it is only a coalesced
frame request and carries no old engine state.

### Frame policy

The concrete engine preserves the proven order and settings: embedded default
style, Ukrainian labels, 256-pixel primitives/symbol providers, software raster
provider, Kyiv 50.4501/30.5234 browse fallback, zoom 13.5, azimuth 0, elevation
90, and no GPU worker thread. Guidance targets the latest valid coordinate and
uses zoom 16. Camera, symbols and raster providers are installed before
`initializeRendering(true)`. Each frame runs update, prepare, then render. The
map renderer is OpenGL ES; vector-tile preparation and rasterization remain
CPU-side, so progressive population on the bench is a performance constraint,
not by itself a rendering fault.

| Engine observation | Internal outcome | Plugin return / wakeup |
|---|---|---|
| `update() == false` or exception | Fault | Nonzero; loader selects fallback |
| `prepareFrame() == false` | Pending (prepare) | Zero; request again if renderer is not idle |
| `renderFrame() == false` | Pending (render) | Zero; request again |
| `renderFrame() == true` | Presented | Zero; request again if renderer is not idle |

The core's bool render API collapses both `StageResult::Wait` and
`StageResult::Fail`. A false is therefore **not proof of a harmless wait**.
The session logs each pending stage once per attachment and a persistent
warning after every 1024 consecutive pending frames. Presentation resets the
consecutive counter. There is no arbitrary failure timeout: it would break
resource loading proven by the bench run. Consult core stage diagnostics when
pending persists; explicit stage results require a future core API change.

Query V1 has no separate pending/no-swap result. Zero causes the runtime to
swap even if the engine is pending; nonzero causes permanent loader fallback
for that lifecycle. This PR retains that behavior. Internal Presented means
the engine rendered successfully, not that the runtime's later swap succeeded.
A future no-swap result would need an append-only ABI negotiated with the
runtime and loader.

### Partial initialization and release failures

The inspected external core is a locally patched source tree without a Git
checkout. The following source semantics govern the conservative cleanup:

- `src/OsmAndCore.cpp`, `InitializeCore`: assigns the global resources provider,
  adds logging and starts/initializes the Qt application before SKIA/ICU failure
  returns. `ReleaseCore` has no matching partial-init status or documented
  partial rollback API.
- `src/Map/MapRenderer.cpp`, `initializeRendering`: initializes GPU API,
  resource manager and renderer stages before `postInitializeRendering` sets
  `_isRenderingInitialized`. `releaseRendering` returns false when that flag is
  unset; it cannot unwind these earlier failures.
- `releaseRendering` may return false between release stages. Ignoring that
  result and destroying the graph risks destructors/workers using partial
  state. The adapter checks the result and retains the graph on failure.

Resource-load failures and failures after a successful core initialization but
before entering rendering initialization can roll back normally. An exception
or false return **during** either initialization boundary quarantines the
session. A false/exception during release also quarantines it.

Quarantine revokes wakeups, stops further engine dispatch and retains the
dependency graph for process lifetime; it does not claim that workers or
globals were cleaned up. Stop cannot make it reusable. The runtime retains the
plugin DSO for process lifetime; unloading an active or quarantined plugin is
unsupported. This is a deliberate bounded-to-one-graph leak requiring process
restart, not an upstream rollback fix. It cannot protect against core crashes,
assertions, hangs, or dependency destructors that terminate the process.

A core follow-up needs stage-aware, idempotent partial initialization rollback,
worker draining/callback revocation, and a trustworthy release completion result.
Until that exists, do not retry initialization in a poisoned process.

For source identification, the inspected files had these SHA-256 values:

| Source | SHA-256 |
|---|---|
| `src/OsmAndCore.cpp` | `9b764a3fd4a15d292108739d8b874954988ec0633bf8fd18d001184df1e19f7d` |
| `src/Map/MapRenderer.cpp` | `984a834482cf781a8a69449d859024dc7f78b212500498cd3d21337e4b591224` |
| `src/Map/OpenGL/AtlasMapRenderer_OpenGL.cpp` | `dd97dd83104a54f98fc3624e209d0d8636620a5f33287dba36b7b6a0de0c4cd2` |

These identify inspected semantics, not a reproducible source release or a
claim that a different core has identical behavior.

## Build and dependency bundle

The optional Make targets need:

- `OSMAND_QNX_ROOT`: workspace with `source/core`, `gcc-out` and PIC Qt;
- `OSMAND_CORE_LIB_DIR`: renderer-enabled `libOsmAndCore_shared.so` and resource
  bundle (default `source/binaries/qnx/gcc-armv7/Release` below the root);
- `OSMAND_LEGACY_ROOT`: matching `core-legacy` sources used by offline routing
  (default `source/core-legacy` below the root);
- `OSMAND_QT_PREFIX`: the PIC Qt build used by that core (default `qt-pic-prefix`);
- `OSMAND_CXX`: its matching C++17 ARM/QNX compiler (default under `gcc-out/bin`);
- `QNX_SDK` and `QNX_HOST_LIBS`: licensed SDK and required host libraries;
- `OSMAND_BUNDLE_ID`: a versioned external-bundle identity; defaults to
  `local-unqualified` for exploration.

```sh
make osmand-check osmand-obf-smoke osmand-route-smoke \
  osmand-plugin-smoke osmand-manifest \
  QNX_SDK=/path/to/qnx660-sdk \
  QNX_HOST_LIBS=/path/to/qnx-host-libs \
  OSMAND_QNX_ROOT=/path/to/osmand-qnx-build \
  OSMAND_BUNDLE_ID=reviewed-bundle-001
```

The external Core must export its legacy native routing implementation. Apply
[`qnx-legacy-routing.patch`](../../patches/osmand-core/qnx-legacy-routing.patch)
from the OsmAnd source-aggregate root before building Core. The patch enables
the already-listed legacy routing sources for QNX and makes two QNX/GCC 8
compatibility corrections; it does not revive the abandoned `core/Routing`
implementation.

```sh
git apply --ignore-space-change \
  "$OPEN_NAVICORE_ROOT/patches/osmand-core/qnx-legacy-routing.patch"
```

`--ignore-space-change` accommodates the CRLF line endings retained by the
legacy transport source; it does not broaden the patch's file or hunk scope.

Objects and linked artifacts live in `build/osmand/<configuration-key>/`,
independent of the shared legacy `BUILD_DIR`. The key includes worktree,
toolchain/SDK roots, flags, dependency paths and bundle ID. Override
`OSMAND_BUILD_ROOT` to relocate that tree. Header dependency files are generated;
the core binary and compiler executable are explicit prerequisites.
Every requested public build target copies its selected artifact to `dist/`,
so switching back to an earlier configuration cannot leave a newer, unrelated
artifact selected there.

Change `OSMAND_BUNDLE_ID` when replacing a dependency bundle in place.
Dependency files and timestamps cannot detect all compiler-internal, archived
source or timestamp-preserving changes. This is configuration isolation and
invalidation, not a hermetic build of external dependencies.

`make osmand-check` checks ELF32 ARM EABI5 soft-float, exact export/dependency
sets, SONAME, absence of TEXTREL/RUNPATH and the sole permitted target RPATH.
It has a separate checker; the strict reference-plugin checker remains unchanged.
`make osmand-session-test` runs the fake-engine/C-entry tests without Qt, EGL,
OsmAnd or QNX. It is also part of `make test-host`.

`make osmand-manifest` writes `bundle-manifest.json` beside the build artifacts.
It inventories plugin/core/resources, installed Qt archives, compiler, runtime
archives, selected SDK libraries/tools and the inspected core source files.
Generated paths are local evidence and are not committed. Preserve alongside it
the complete source archives/revisions, patches, build scripts, Qt configuration,
compiler recipe, SDK version and link flags. This repository does not build or
supply those external dependencies; the generated inventory alone cannot
reproduce them.

The plugin still uses the existing query build ID expected by the provider.
That handshake is an ABI/deployment label, not a unique content identity.
Record the manifest and deployed provider/plugin/core set together.

### Runtime assets

| Asset | Default target path |
|---|---|
| Backend | `/datastore/navicore-backend/libMockNavBackendOsmAnd.so` |
| Core | `/datastore/navicore-backend/libOsmAndCore_shared.so` |
| Resources and embedded default style | `/datastore/navicore-backend/libOsmAndCore_ResourcesBundle_shared.so` |
| Map | `/datastore/navicore-backend/maps/kyiv.obf` |
| Fonts | `/datastore/navicore-backend/fonts/` |

`OPEN_NAVICORE_OSMAND_RESOURCES`, `OPEN_NAVICORE_OSMAND_MAP` and
`OPEN_NAVICORE_OSMAND_FONTS` are copied at start; environment changes affect
the next start only. The sole runtime search path is
`/datastore/navicore-backend`. Record map region/date/source/license and font
files/licenses with the bundle. Map data, fonts and binary/resource bundles
are not committed. SDK library hashes identify offline link inputs; they do
not establish equivalence to the radio's graphics implementation.

For a stationary-bench route-following test, set
`OPEN_NAVICORE_OSMAND_SIMULATE_ROUTE=1` before provider start or create the
marker `/datastore/navicore-backend/enable-route-simulation`. When no real
coordinate is available, guidance then advances over calculated route geometry
at 20 m/s and requests continuously interpolated camera frames. Real
coordinates take precedence. The guidance rail is explicitly
labelled `ROUTE SIMULATION`, diagnostics record start/arrival, and deleting the
marker affects the next provider start. This mode is test instrumentation, not
a navigation fallback for normal use.

For a bounded performance run, set
`OPEN_NAVICORE_OSMAND_PERFORMANCE_METRICS=1` before provider start or create
`/datastore/navicore-backend/enable-performance-metrics`. The backend emits one
`perf` aggregate per second with presented FPS, pending/fault counts,
p50/p95/p99 frame time, stage p95 values, and UI upload bytes. This counter path
does not enable OsmAnd's verbose per-tile logging. For a short diagnostic run,
that separate instrumentation can be enabled with
`OPEN_NAVICORE_OSMAND_DETAILED_TILE_METRICS=1` or the marker
`/datastore/navicore-backend/enable-detailed-tile-metrics`; it perturbs timing
and must not be used for qualification. The selected renderer direction and
benchmark gates are in
[`rendering-architecture.md`](../../docs/performance/rendering-architecture.md).

For the controlled renderer isolation run, additionally set
`OPEN_NAVICORE_OSMAND_RENDER_SHOOTOUT=1` or create
`/datastore/navicore-backend/enable-render-shootout`. Once simulated guidance
starts, the renderer runs 15 seconds of the full map, 15 seconds without the
independent map-symbol provider, and 15 seconds with only the GPU route layer,
resetting simulated progress at each boundary. It then restores the full map.
Trace records identify every phase. This visibly removes layers and is a bench
diagnostic, not a production fallback.

For a sustained moving-basemap qualification, set
`OPEN_NAVICORE_OSMAND_SUSTAINED_BASEMAP_BENCHMARK=1` or create
`/datastore/navicore-backend/enable-sustained-basemap-benchmark`. This disables
the phase cycling and leaves the real raster basemap, route, marker, camera and
guidance UI active without OsmAnd's independent symbol provider.

The rejected baked-symbol experiment can be reproduced by setting
`OPEN_NAVICORE_OSMAND_BAKED_MAP_SYMBOLS=1` or creating
`/datastore/navicore-backend/enable-baked-map-symbols`. A bounded set of
priority-sorted OsmAnd glyph/icon images is then composited into each basemap
tile once. The renderer retains the resulting tile as one texture, so moving
frames do not perform dynamic symbol placement or upload one resource per
label. Live movement reached only 18.67 weighted FPS after its first route
window, so this is diagnostic code rather than a candidate production path.

The retained-GPU symbol experiment is enabled with
`OPEN_NAVICORE_OSMAND_BOUNDED_MAP_SYMBOLS=1` or
`/datastore/navicore-backend/enable-bounded-map-symbols`. It publishes at most
12 priority-sorted symbol groups per tile through OsmAnd's normal retained
GLES2 resource path. This is retained as diagnostic code, not a production
candidate. On the live sustained route, the post-warmup sample delivered 17.99
weighted FPS and every rolling five-second window was below 30 FPS
(13.91--19.38 FPS), compared with 32.06 FPS for the symbol-free control. The
provider filters groups only after OsmAnd has performed its upstream symbol
rasterization, and the live output also exposed malformed oversized imagery.

The custom retained-atlas candidate is enabled with
`OPEN_NAVICORE_OSMAND_RETAINED_MAP_SYMBOLS=1` or
`/datastore/navicore-backend/enable-retained-map-symbols`. OsmAnd rasterizes a
bounded visible-tile symbol set on a background worker only when the visible
tile set changes. The renderer thread retains one 512x512 atlas, projects up to
32 map anchors, performs a small priority/collision pass, and emits one GLES2
batch. Camera movement does not rerasterize or re-upload glyphs. This mode is a
candidate pending equal-route live qualification; 30 FPS is its acceptance
floor, not a limiter.

## Stationary bench qualification checklist

1. Preserve stock/active-file backups and recovery access. Record the complete
   provider/plugin/core/resources/map/font bundle and target graphics versions.
2. Run the OBF, route and plugin-query smoke executables; verify map/routing
   indexes, a non-empty route geometry, query selection and expected resources
   before rendering. Cross-building them does not run them.
3. Start navigation, verify the fixed Kyiv map and Ukrainian labels in the stock
   tile, then confirm hide/show and normal teardown/start cycles.
4. Observe prepare/render pending followed by `frame_rendered=kyiv`; check core
   logs if the persistent-pending warning repeats.
5. Exercise normal context-current teardown and, with a controlled harness,
   no-context/context-loss teardown. Verify no backend-owned EGL destruction.
6. Test missing assets (safe rollback) and controlled initialization/release
   failures. A quarantined session must reject restart until process restart;
   do not claim that an injected failure reproduces every real core failure.
7. Check late worker wakeups, repeated attachments, lifecycle restart, worker
   drainage and memory behavior across cycles.

This change performs no live deployment. Host tests establish session policy
and C-entry validation, cross-build/ELF checks establish artifact properties,
and only the bench can establish this binary set's rendering/lifecycle behavior.
