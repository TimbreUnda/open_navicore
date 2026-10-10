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

The complete pointer payload is `action`, `x`, `y` and `buttons`, with only
press, release and move actions. It contains no touch identifier, contact
count, pressure, velocity, scroll delta, gesture, key or text event.

Consequently, the current API cannot provide:

- true pinch-to-zoom, two-finger rotate or any other multi-touch gesture;
- concurrent pointers or reliable tracking of separate fingers;
- pressure-sensitive, wheel/scroll or stylus-specific behavior;
- stock keyboard/IME input, hardware-key events, accessibility focus, haptics,
  rotary input or steering-wheel input.

This is a restriction of `MockNavBackendPointerV1`, not proof of a hardware
limit. Open NaviCore owns this backend seam and can extend it append-only. The
preferred path to multi-touch is to capture the original QNX Screen event
before `mock_screen_input` reduces it, verify which contact fields the radio
actually delivers, and introduce a versioned touch payload with contact ID,
phase, coordinates and timestamp. Pressure or device information should be
added only if observed on the bench.

If the stock HMI or `nv_navigation` has already discarded additional contacts
before the event reaches our process, extending the backend payload alone will
not recover them. A bounded raw-event trace is therefore required before
claiming multi-touch support. Reading the Screen queue directly from the
OsmAnd plugin or interposing Screen calls may help discovery, but is not the
preferred product architecture: it can compete with stock event ownership,
routing, coordinate transforms and visibility policy.

Tap, drag/pan, on-canvas buttons and timer-derived long press are feasible with
the single pointer stream. Zoom can use on-canvas buttons or another
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

The present OsmAnd backend is a rendering proof, not the finished navigation
application. These limitations can be removed without changing the stock
provider boundary:

- the camera is fixed on Kyiv at 50.4501, 30.5234 and zoom 13.5;
- GPS is accepted but not used to move the map;
- pan, zoom, search, routing, rerouting and guidance are not implemented;
- no route, maneuver, audio or cluster state is produced;
- the plugin advertises V2 rather than the available V5 backend seam;
- no final UI toolkit or component system has been selected.

The API does **not** require LVGL. The backend can use direct GLES2, a custom
scene/UI layer, LVGL with a suitable OpenGL integration, or another renderer
that respects the borrowed-context and thread rules. The choice is an
architecture and resource-budget decision.

## Feature gate for design and planning

| Desired behavior | Current status | Minimum enabler |
|---|---|---|
| Custom map, panels, buttons and markers | Available | Implement inside the GLES canvas |
| Tap, one-finger pan and UI buttons | Available | Implement pointer handling |
| Pinch zoom or two-finger rotate | Blocked by Pointer V1; hardware path unverified | Capture raw QNX events, then extend input ABI with contact IDs/count |
| Stock keyboard or text input | Hard restriction | Recover a text/IME service, or ship our own keyboard |
| Route search and guidance in our canvas | Implementation gap | Add OsmAnd navigation/application layers |
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
