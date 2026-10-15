# UI restrictions imposed by the current API

This document is the design boundary for the first Open NaviCore UI. It says
what the **current, qualified backend API** can and cannot support. It does not
claim that the radio can never gain a feature: a restriction can be removed by
recovering and qualifying another stock contract or by extending the backend
ABI and runtime.

Use these labels consistently:

- **Hard API restriction** — unavailable through the current backend API. Do
  not include it in a design as working behavior without an API change.
- **Current implementation gap** — the API permits it, but the OsmAnd backend
  does not implement it yet.
- **Unverified stock capability** — suggestive code or a callback name exists,
  but ownership, semantics, state transitions, or live behavior are not proven.

## The usable surface

The backend receives one borrowed Screen/EGL render target and can draw GLES2
pixels into the stock navigation canvas. The stock application continues to
own the surrounding HMI, process lifecycle, window, EGL display, surface and
context.

This gives us substantial freedom *inside the canvas*: map styling, markers,
route lines, custom panels, buttons, transitions and other OpenGL-rendered UI
are all possible. It does not make Open NaviCore the owner of the complete
radio UI.

The observed radio layout and resolution are useful design references, not an
API guarantee. Designs must tolerate the width and height supplied with each
render target and must not assume that observed stock chrome dimensions are a
stable safe-area contract.

## Hard API restrictions

### Stock chrome and windows

- The backend cannot restyle, move, hide or intercept the stock top bar,
  bottom bar, app switcher, status area or native dialogs through this API.
- It cannot create another OEM window, system popover or overlay layer. Panels
  belonging to our navigation experience must be rendered inside our canvas.
- It cannot reuse stock widgets, layouts, menus or dialog components. A stock-
  looking control must be recreated by us and remains our control.
- Only one borrowed render target is exposed. Multiple surfaces, independent
  windows and multiple backend instances are not API capabilities.

### Graphics lifecycle

- Screen and EGL objects are borrowed. The backend must not create, destroy,
  rebind or swap the provider-owned window, display, surface or context.
- GLES work is valid only when the runtime invokes a callback with the expected
  context current. Rendering from an arbitrary worker thread is unsupported.
- Backend callbacks are serialized on the renderer thread. Workers may request
  a future frame, but they do not own an independent presentation loop.
- The V1 render result has no `pending` or `do not swap` state. Returning zero
  makes the runtime swap buffers; a nonzero plugin result selects the fallback
  for the rest of that lifecycle. Correct no-present behavior during async map
  loading requires an append-only API/runtime extension.
- Context loss is signalled during teardown by the absence of the expected
  current context. In that case the backend must skip GL cleanup calls.

### Touch and input

The V1 pointer payload is `action`, `x`, `y` and `buttons`, with only press,
release and move actions. `MockNavBackendPointerV2` appends `contact_id` (0 for
the first finger, 1 for a second) and `contact_count`. A plugin receives V2
payloads, for both contacts, only when it declares the optional
`MOCK_NAV_PLUGIN_CAP_POINTER_CONTACTS` capability (accepted in every query
version); every other backend still receives V1 payloads for the first contact
only. Moves are coalesced per contact in the provider queue.

Hardware path: the raw Screen input trace (2026-10-14, marker
`/datastore/navicore-backend/enable-input-trace`) showed a second contact
(`touch_id` 1) reaching the navigation window while the first is down, so the
contacts are not discarded before our process. At most two contacts are
tracked; a third is counted in `secondary_touch` and dropped.

The payloads still carry no pressure, velocity, scroll delta, gesture, key or
text event, so the API cannot provide:

- more than two fingers, pressure-sensitive, wheel/scroll or stylus input;
- stock keyboard/IME input, hardware-key events, accessibility focus, haptics,
  rotary input or steering-wheel input.

The OsmAnd backend declares the capability and implements pinch-zoom and
two-finger rotate (the map point under the fingers' midpoint stays under it).
Gesture behaviour on the unit's touch panel is still to be verified by hand.

Tap, drag/pan, on-canvas buttons, double tap and timer-derived long press work
with the first contact alone. Zoom can use on-canvas buttons or another
single-pointer interaction. An on-canvas keyboard is technically possible,
but it would be our keyboard, not the stock IME, and would not gain stock
accessibility behavior automatically.

### Navigation application services

The current backend API has no semantic request/result contracts for stock
destination entry, address or POI search, favorites, recents, contacts, route
calculation, route alternatives, rerouting or trip management. Those flows
must be implemented within our UI and navigation engine, or added as explicit,
qualified contracts.

The stock HMI therefore cannot currently command an OsmAnd route merely because
a similarly named provider slot or application callback exists. A callable
slot without decoded payload meaning, replies, ordering and timeout behavior is
not a usable feature API.

### Audio and voice

- Incoming V2 audio data contains only a raw source value and volume.
- The V3 outbound audio service publishes an availability state. It is not a
  PCM stream, TTS service, prompt queue, focus/ducking contract or proof that a
  spoken maneuver will play.
- No qualified speech-recognition or microphone contract is exposed to the
  backend.

Spoken guidance and voice search therefore require a separately recovered and
qualified audio/voice contract, or a new platform implementation. Named voice
callbacks in the compatibility layer are not sufficient evidence.

### Instrument cluster

V3 can publish one constrained cluster payload: guidance/navigation state,
destination and maneuver distances, lanes, ETA/RTA, progress bar, compass,
speed fields, and current/next road text. It cannot send arbitrary graphics,
custom widgets or a second copy of the map.

Several numeric values still lack qualified semantic enum mappings. Designs
must not assign user-visible meanings to raw values until live evidence
validates them. The current OsmAnd plugin advertises backend V2, so it cannot
call the V3 cluster publishing service yet.

### Vehicle state, localization and distraction

- V2 supplies day/night and speed snapshots, but some values remain raw and
  must not be given guessed labels.
- Language, date/time-format and cluster-display inputs require V4. Their raw
  values intentionally have no asserted enum labels.
- Driver-distraction input requires V5 and is likewise raw pending semantic
  qualification.
- The current OsmAnd plugin is V2, so it receives none of the V4/V5 inputs.

A responsive theme is possible once day/night values are qualified. Correct
localization, cluster-display policy and distraction lockouts cannot be
promised from the current V2 plugin.

### Platform services not exposed

The backend seam provides no networking, traffic provider, account, cloud
sync, phone handoff, persistence, stock notification or general IPC service.
The engine might eventually implement some of these independently if the QNX
platform and deployment permit it, but they are outside the current API and
must not be represented as stock-integrated capabilities.

## Current OsmAnd gaps, not hard API limits

The present OsmAnd backend is an evolving navigation application, not only a
rendering proof. It uses LVGL for the in-canvas UI, performs offline search and
routing, draws route geometry, and follows valid GPS coordinates with basic
maneuver/remaining-trip guidance. Without a usable position it presents a
paused state and retains the Kyiv fallback only for browse/search routing.

The remaining implementation gaps can be removed without changing the stock
provider boundary:

- position freshness, off-route detection and automatic rerouting;
- pan, zoom controls and an explicit browse/recenter interaction;
- qualified heading/speed/fix-type meanings and heading-aware camera behavior;
- audio prompts and stock cluster maneuver output;
- the plugin advertises V2 rather than the available V5 backend seam;
- target performance budgets and renderer-quality tradeoffs are not yet
  measured on the bench.

The API does **not** require LVGL. The backend can use direct GLES2, a custom
scene/UI layer, LVGL with a suitable OpenGL integration, or another renderer
that respects the borrowed-context and thread rules. The choice is an
architecture and resource-budget decision.

## Feature gate for design and planning

| Desired behavior | Current status | Minimum enabler |
|---|---|---|
| Custom map, panels, buttons and markers | Available | Implement inside the GLES canvas |
| Tap, one-finger pan and UI buttons | Available | Implement pointer handling |
| Pinch zoom or two-finger rotate | Implemented (Pointer V2 contacts, opt-in capability); second contact seen in the raw trace | Verify the gestures by hand on the unit |
| Stock keyboard or text input | Hard restriction | Recover a text/IME service, or ship our own keyboard |
| Route search and guidance in our canvas | Implemented; live qualification in progress | Complete bench and driven/simulated-route qualification |
| Stock UI starts or edits our route | Hard restriction | Recover and qualify destination/route request contracts |
| Spoken maneuver prompts | Hard restriction | Qualify PCM/TTS, audio focus and ducking contracts |
| Cluster maneuver fields | API available, plugin gap | Upgrade to V3+ and qualify enum semantics live |
| Arbitrary cluster graphics or map | Hard restriction | A different cluster graphics contract |
| Automatic localization | API available, plugin gap | Upgrade to V4 and qualify raw locale mappings/fonts |
| Driver-distraction lockouts | API available, plugin gap | Upgrade to V5 and qualify raw status semantics |
| Online traffic | Outside current API | Platform network access, provider and data contract |
| Modify top/bottom stock chrome | Hard restriction | A separately recovered HMI contract |
| Background/minimized navigation behavior | Unverified | Define lifecycle policy and qualify it on the bench |

## Evidence rules

When deciding whether a design is implementable, apply these rules:

1. A symbol, class or callback name is not proof of a usable feature.
2. A callable vtable slot proves callability, not payload semantics, ownership,
   replies, state transitions, ordering, timeout behavior or user-visible effect.
3. An incoming notification does not imply that the backend can control the
   corresponding stock service.
4. A successful mock or host test does not prove stock HMI behavior.
5. The backend version actually advertised by the plugin controls which API
   tail is available; provider support for V5 does not give a V2 plugin V5.
6. Observed layout and live behavior are evidence for that tested firmware and
   lifecycle, not a promise for every firmware version.

The authoritative C contract is
[`include/open_navicore/backend.h`](../../include/open_navicore/backend.h).
See the [provider boundary](../abi/provider-boundary.md) for the limits of the
recovered ABI evidence and the [OsmAnd backend notes](../../backends/osmand/README.md)
for current implementation and lifecycle status.
