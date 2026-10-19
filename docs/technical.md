# Open NaviCore technical reference

This is a standalone open ARM/QNX replacement provider for the QR39.5/QR39.6
`nv_navigation` loader. It does not link to or load the production
`libNaviCore.so`.

The recovered ABI is documented in
[`analysis/mock_navicore_abi.md`](../../analysis/mock_navicore_abi.md). The provider
implements its four unmangled C exports:

- `panaVersionCheck(ServerVersionHeadUnit, client_out, compatibility_out)`
- `panaInitialize(init_record)`, returning status zero on success
- `panaGetSoftwareInfrastructure()`
- `panaDeinitialize()`

## Build and verification

```sh
make          # build/qnx: provider, OsmAnd backend, reference backends, smokes
make ifs      # build/qnx/ifs/navigation.ifs (STOCK_NV_NAVIGATION_CFG=... once)
make test     # build/host: host tests and contract checks (ctest)
```

One toolchain builds everything for the radio: GCC 8.5 from the OsmAnd QNX
build with the QNX 6.6 SDK's sysroot and binutils (`cmake/qnx-armv7.cmake`).
The provider `libNaviCore.so` is checked after linking: ELF32/ARM/EABI5
soft-float, SONAME, exactly the four stock exports with Thumb symbol values
(`tests/check_elf.sh`), and the argument passing of every recovered entry
point and callback wrapper (`tests/check_arm_abi.py`, frame-aware so it does
not depend on one compiler's register allocation). The reference and OsmAnd
plugins get their own ELF checks.

`make test` runs the host-native ABI behavior, lifecycle/concurrency, backend
event-loop and file-trace tests, the plugin loader tests, the OsmAnd
backend's logic and UI tests, and the contract checks. The ABI test checks
the 24-byte server-version call, all client-version output fields,
initialization status, root identity, root vtable slot 3, stable identity for
all 256 per-enum adapters, and all 128 dummy virtual slots on each. The trace
test verifies append output and preservation of the caller's `errno`. The
host build substitutes a deterministic renderer stub and verifies that
renderer creation occurs exactly once between adapter 6 and adapter 10, and
that teardown occurs exactly once after the worker is quiescent. The
lifecycle test covers COLD/STARTING/ACTIVE/STOPPING serialization, COLD and
STOPPING rejection across provider classes, reentrant and concurrent
teardown, callback-triggered teardown from producer and readiness-worker
paths, producer races, epoch-qualified callback telemetry, and a clean second
worker/renderer lifecycle. The fake-backend suite verifies callback ordering,
hide/show, screen, GPS, pointer press/release, frame requests, nonzero
callback status, stale-service rejection across backend replacement,
latest-state coalescing, observable pointer overflow, timeout-bounded
reentrant teardown at every callback phase, interleaved final state for all
three displays, atomic HMI capacity rejection, forced cleanup context loss,
concurrent teardown, and rejected ABI registrations.

The contract checks include the fail-closed provider API coverage gate. The
machine-readable [`contracts/qr39/provider/api-coverage.csv`](../contracts/qr39/provider/api-coverage.csv)
contains exactly the 36 adapter/slot pairs observed in the v11 provider trace
plus separately recovered GPS 9/4. Each row records the recovered prototype
and canonical argument shape, concrete function and vtable, compiler-checked
C type, behavioral level, test evidence, lifecycle rejection, and backend
route. The checker compares that catalog to the authoritative provider and
pointer matrices, expands the real vtables, and rejects missing, duplicate,
mismatched, generic, or variadic observed entries.

The broader [`contracts/qr39/provider/slot-manifest.csv`](../contracts/qr39/provider/slot-manifest.csv)
is the QR39.6 static-call-graph boundary. It records 140 root/adapter slots
reachable by the stock executable and eight code-present slots belonging to
adapters 21, 27, and 28 that stock `getAdapter` deliberately rejects.
Recovered but not yet semantically decoded calls use fixed, compiler-checked
AAPCS word signatures; they never dereference or retain an opaque word and
return zero only while the provider is ACTIVE. Known pointer layouts continue
to use their narrower typed implementations and bounded copies.
`tests/check_static_provider_slots.py` independently embeds the expected
slot/word catalog, expands every concrete vtable, checks the generated
declarations, and rejects any reachable generic or variadic binding. The
host smoke test invokes every generated method both ACTIVE (success) and
after teardown (rejection).

The evidence behind these contracts (static and cross-version inventories,
QNX ARM compiler oracles, the stock-code snapshot replay and the raw-callback
capture probe) lives in the sibling research repository `open_navicore-re`,
which checks it against this repository's contracts and sources.

The behavioral labels are deliberately bounded: `semantic` means the provider
has an asynchronous/reply or renderer/backend effect, `owned_copy` means
caller pointer storage is synchronously copied into provider-owned state, and
`typed_telemetry` means the ABI and values are preserved but no stock feature
semantics are claimed. Coverage therefore establishes safe typed reception and
lifecycle behavior, not complete parity for optional diagnosis, connectivity,
phone, SiriusXM, or other telemetry-only services. GPS engineering units and
some recovered enum dictionaries also remain unknown.

## Provider object model

The root is an explicit C representation of an Itanium-layout C++ object.
Its first word is a vtable pointer and its second word is a writable,
initially-null host proxy registration field. Byte offset `+0x0c` in the
vtable on the 32-bit target is `getAdapter(root, enum, IF_Object**)`.

Adapter IDs 0 through 255 receive separate process-lifetime dummy objects,
except IDs 21, 27, and 28. Those three match the captured stock provider by
setting the output pointer to null and returning status one.
Each begins with a non-null 128-slot vtable, followed by a writable null host
proxy at byte offset `+4` and the diagnostic adapter ID at `+8`. Generic dummy
virtual functions return zero without inspecting arguments. Adapter 1 (HMI)
implements slot 5 for requested HMI/render transitions, slot 9 for
`requestScreenChange(self, handle, display, screenType, animation, reserved)`,
and slot 10 for `requestScreenThemeChange(self, handle, wideThemeName)`.
Requests share an ordered asynchronous event FIFO; transitions retain their
bounded eight-pending-request limit. Each completion echoes the exact request
handle. Overflow is logged and rejected with status one.
Out-of-range adapter IDs receive a stable overflow object and a warning. No
static object is freed during deinitialization, making repeated teardown safe.

Adapter 9 has a separate `SI_GPSData` vtable. Stock provider target offset
`0x2d7440` proves slot 4 uses the two-register ABI
`slot4(self, const void *gpsRecord)`. The provider accepts both non-null and null
record pointers and returns success synchronously. Static analysis of
`SI_GPSData::OnLocationUpdateEvent` proves the input is a `0x120`-byte
little-endian record. Slot 4 copies exactly those bytes to a local array before
parsing; neither the parser nor backend receives or retains the caller pointer.
The normalized `OpenNaviGpsSnapshot` includes raw altitude, fix, heading, dilution,
accuracy, satellite, speed, quality, counter, distance, altitude-change, and
noise fields at the recovered offsets. Latitude and longitude are signed
degrees scaled by `1e7`; all other quantities deliberately keep `_raw` names
because their engineering units and masks are not yet proven. The sixteen
12-byte satellite entries remain opaque byte arrays.

`open_navi_gps_backend_submit()` retains diagnostic latest-value state. The same
owned normalized snapshot is also queued to the versioned map backend seam
described below. The diagnostic implementation copies the value into
bounded mutex-protected latest-snapshot state and advances a saturating
generation. A null input is counted and logged but is not parsed and does not
advance that generation. Tests also prove that mutating the caller buffer after
slot 4 returns cannot alter the stored snapshot. Other statically reachable
slots have either their documented semantic implementation or the fixed-word,
lifecycle-gated acceptance behavior described above; only genuinely
unreachable vtable positions retain generic stubs.

Successful high-frequency GPS slot-4 calls update their count, parser, and
backend on every invocation, but file logging is limited to call one and
power-of-two checkpoints. Null inputs are always logged. This bounds routine
trace growth logarithmically without dropping navigation state updates.

## Typed scalar backend seam

Adapters 8, 9, and 10 have specialized vtables for the scalar methods proven
by the v11 provider trace and caller analysis:

- VehicleData slot 4: raw `uint8_t day_night_status`, stock offset `0x263154`.
- VehicleData slot 6: raw `uint8_t day_night_setting`, offset `0x263050`.
- VehicleData slot 8: `float displayed_vehicle_speed`, offset `0x278e70`.
- GPSData slot 6: raw `uint32_t interval_ms`, offset `0x2a0e7c` (observed 1000).
- AudioHandling slot 19: raw `uint32_t audio_source` and `uint8_t volume`,
  offset `0x2eae18` (observed source 1, volume 15).

At the VehicleData slot-8 call site (`nv_navigation` `0x0812b3c6`), ARM/Thumb
instructions load the provider object into `r0`, execute `vmov r1,s14`, load
vtable byte offset `0x20`, and `blx` it. Thus this QNX soft-float boundary
carries the float's exact 32-bit representation in `r1`; the typed C method and
backend preserve both its float value and diagnostic bit pattern. Disassembly
of the QNX ARM provider object independently shows `open_navi_vehicle_speed` moving
incoming `r1` unchanged into the backend call.

`open_navi_scalar_backend` synchronously copies these values under a mutex and is
the isolated future seam for OsmAnd theme, feedback pacing, and prompt-volume
logic. Per-method saturating call counts and per-domain generations are kept.
No engineering meaning or range validation is imposed beyond the proven C
types. Statically reachable calls whose semantic types remain unknown use the
fixed-word acceptance layer rather than a variadic stub. Scalar state resets
on initialization and deinitialization.

All other `scalar_input` rows from the 36-row v11 provider matrix are also
typed. Each writes a bounded `open_navi_api_telemetry` entry keyed by adapter and
slot: saturating call count, promoted argument count, and the latest four raw
32-bit argument words. Narrow arguments are zero-extended, no scalar ranges are
rejected, and no callback is synthesized. The high-value vehicle, GPS interval,
and audio methods update both telemetry and their domain backend.

| Matrix rows | Coverage |
|---|---|
| 1/9 | Typed HMI screen request and recurrent completion lifecycle |
| 6/3 | Typed pointer input; copies only observed count 4 (4 level bytes and 16 size bytes) |
| 7/3 | Typed pointer input; copies exact four-byte internationalization record |
| 8/3, 8/4, 8/5, 8/6, 8/8 | Typed scalar plus telemetry and vehicle backend |
| 8/12 | Typed pointer input; copies exactly 17 VIN bytes and appends owned NUL |
| 9/6 | Typed scalar plus telemetry and GPS interval backend |
| 10/17, 10/19 | Typed scalar plus telemetry and audio backend |
| 12/5, 12/6, 12/7 | Typed scalar telemetry |
| 13/3, 13/4, 13/12, 13/13, 13/14 | Typed scalar telemetry |
| 17/6 | Typed scalar telemetry |
| 19/3 | Typed pointer input; count 0/1 only, copies named fields and never padding |
| 20/3, 20/4, 20/7, 20/8 | Typed scalar telemetry |
| 20/6 | Typed pointer input; copies outer fields and three nested strings with 128-byte limits |
| 25/19 | Typed scalar telemetry |
| 25/21 | Typed pointer input; copies filename with a 512-byte limit |
| 26/5 | Typed scalar telemetry |
| 29/3, 29/4 | Typed scalar telemetry, including the zero-argument slot |
| 30/3, 30/4 | Typed scalar telemetry |
| 31/3, 31/5 | Typed scalar telemetry |

This table accounts for all 36 CSV rows: 29 scalar rows, the HMI row, and all
six pointer-bearing rows now have exact typed entry points. GPS location slot
9/4 is additional separately recovered evidence and uses the synchronous
0x120-byte parser described above.

## Owned pointer-input seam

`open_navi_pointer_backend` synchronously converts all six recovered pointer APIs
into owned state under a mutex. It retains no caller or nested pointer, keeps a
saturating per-method call count plus aggregate generation, and resets on both
initialization and deinitialization. NULL inputs are accepted as an absent
value and still return success.

Bounds follow only proven callers. Logging reads arrays only when `count == 4`
and both pointers are non-null; any other count is recorded as unsupported and
never dereferenced. Transport accepts count zero without reading and count one
by reading only kind at `+0`, buffer size at `+8`, and field `+0x0c`; counts
greater than one are unsupported and the pointer is untouched. In particular,
transport bytes `+1..+7` are never copied, compared, or treated as semantic
data. Internationalization copies exactly four bytes, while VIN copies exactly
17 bytes into an owned 18-byte buffer.

Connection-device outer fields are copied by value before its nested pointers
are followed. Each IMEI, MSISDN, and SIM ICCID source is inspected for at most
128 bytes and copied into an owned 129-byte NUL-terminated buffer. TEF filenames
similarly use a 512-byte inspection ceiling and an owned 513-byte buffer. A
source that contains no NUL within its proven extent is preserved in full and
terminated only in the owned destination. Encoding and unresolved enum/field
semantics remain deliberately uninterpreted.

Connectivity slot 20/3 is the representative maximum-arity scalar ABI case.
Its fourth word after `self` is `extended_status`, passed on the ARM stack. The
QNX object verifier confirms the wrapper loads it from the caller's stack
location (current `sp+24` after its 24-byte prologue) and forwards the unchanged
word into telemetry. The same verifier checks the slot-8 float `r1` path.

Root slot 4 (`signalStart`) synchronously calls SoftwareInfrastructure proxy
slot 12 (`adapterReady`) when its host proxy is registered, exactly where the
captured stock trace nests that callback. It then arms a joinable readiness
worker. The worker retains a bounded fallback for a late host-proxy write. It
first calls common host slot 2 (`adapterReady`) for adapters
`8,12,24,6,10,13,20,29,7,26,31,14,4,19,23,17`, then calls HMI proxy slot 2.
It then reproduces the recovered QR39.5 HMI initialization contract without waiting
for a nested transition: HMI proxy slot 3 subscribes with `-1`,
then displays 0, 1, and 2 each receive slot 7 `requestHMIState`, slot 6
`requestScreenWindowIdentifier` with the same per-display handle, and slot 14
`signalNavigationHMIReady` in that order. All request handles come from one
thread-safe monotonic allocator, are nonzero, wrap from `UINT32_MAX` to one,
and reset with provider initialization. Handles are correlation IDs rather
than captured constants or display IDs. Only after all three display
triplets return, it calls common host slot 2 for adapters `9,25,32,30`, matching
the captured stock activation order. Only then does the worker drain
transitions queued through provider slot
5 and reply through host slot 9. It then continues servicing later HMI events
in FIFO order. Replies are delayed briefly to keep them observably
asynchronous. Stock `nv_navigation` decompilation shows its screen-change path
calling provider vtable byte offset `0x24` (slot 9) with the generated handle,
display zero, screen type, and two zero fields; the provider replies through host
slot 10 as `(handle, 0 /* success */, screenType)`. The theme path calls
provider byte offset `0x28` (slot 10) with its generated handle and theme-name
pointer; the provider replies through host slot 12 with that same handle. This
handle-paired behavior applies to every request, including repeated Apps-to-
Navigation cycles; there are no fixed startup-only handles. HMI slot 2 is not
repeated, and no slot-8 callback or navigation-ready retry is generated. The
worker never holds its mutex across a host callback;
deinitialization wakes and joins it. The trace records the complete contract
order and arguments, all callback results, and transition queue acceptance.

Initialization and deinitialization clear every host-proxy field; the stock
bridge must register proxies for each new lifecycle. The lifecycle gate has
four serialized states: COLD, STARTING, ACTIVE, and STOPPING. Every provider
input and every outbound producer obtains an epoch lease only in ACTIVE, and
teardown waits for foreign leases without holding the gate across host code.
If a host callback requests teardown reentrantly, it returns without waiting
on its own lease. The final release performs teardown. If that release belongs
to the readiness worker, finalization is handed to the worker epilogue so the
old worker has stopped touching shared flags before COLD is published. This
also makes immediate same-process reinitialization safe.

## Typed callback backend seam

The reverse NaviCore-to-PAS calls used by the provider no longer share a variadic
or fixed two-word scalar dispatcher. Every emitted call uses its recovered
zero-, one-, two-, three-, or pointer-argument ABI. Callback results are
recorded in a bounded mutex-protected backend and classified as either
startup-critical or optional. A nonzero optional/no-data status, including the
observed Diagnosis status 5, is retained as a failure instead of being logged
as success, but it does not abort unrelated startup work.

The retained v11 startup emissions are deliberately narrow:

- Vehicle slots 3, 5, 6 and 11 subscribe and request status, setting, and VIN.
- GPS slot 3 subscribes; its synchronous provider slot-6 interval reply is
  accepted by the existing typed provider path.
- Audio slot 3 is emitted exactly once with the observed two-word shape, and
  slot 19 requests volume with a newly allocated handle and source 1.
- InstrumentCluster slots 5, 6 and 7 use, respectively, one argument, a true
  no-argument call, and an allocated request handle.

Each request is entered into bounded pending telemetry before invoking PAS, so
a synchronous provider reply can match adapter, provider slot, and handle.
Provider replies for Vehicle 3/5/12, Audio 17, and Cluster 6 complete that
correlation. Pending state, counters, owned callback data, and handle allocation
are reset during initialize/deinitialize, and no host callback occurs while the
callback backend mutex is held.

Additional backend-driven APIs expose only contracts supported by real state:

- Audio slot 10 is emitted only when the supplied availability state changes;
  repeating the same state is suppressed only after PAS accepts it. A missing
  callback or nonzero status leaves the state retryable, and attempt tokens
  prevent an older concurrent completion from replacing a newer state.
- `open_navi_cluster_backend_publish()` builds the recovered `0xa5c` cluster record,
  calculates field-granular changes against the last accepted publication,
  and synchronously forwards it through InstrumentCluster slot 3. A rejected
  callback does not advance publication history.
- A provider Voice slot-19 request is answered through callback slot 23 with
  that exact incoming handle and the backend's current result/guidance state;
  no timer or captured handle is replayed.
- `open_navi_callback_signal_thread_status()` forwards root slot 10 with a terminated
  wide name converted from at most 63 input bytes and stores an owned bounded
  diagnostic copy.

Optional service subscriptions from the captured boot are omitted where the
mock has no corresponding backend instead of synthesizing stale settings.

## Instrument-cluster backend and OsmAnd mapping

[`include/open_navicore/cluster.h`](../include/open_navicore/cluster.h) is the
public producer boundary used by the OsmAnd backend. It exposes route and
navigation state, the route-guidance symbol, numeric destination and maneuver
distances plus recovered units, ETA/RTA bytes, bounded road text, lane-active
state, bar fill, compass, and both speed-limit fields. Its semantic supported
mask deliberately rejects lane bit 7 and unknown bit 12 because their
encodings or meaning remain unresolved.

The route-guidance symbol (bit 2, byte `+0x003`) was mapped live on the
installed bench unit (2026-10-19, analysis `Cluster_TBT_Symbols.md`; table in
`enum OpenNaviClusterRouteSymbol`). The cluster uses `symbol & 0x3f`; 1..61
are pictures, 0 makes it leave its navigation page and 62/63 draw nothing, so
the API accepts only 1..61 (`OPEN_NAVI_CLUSTER_INVALID_VALUE` otherwise). The
turn view is drawn only while bit 2 **and** bar-fill bit 10 are valid; the
bar value itself is not drawn. Of a guidance record the turn view shows only
the maneuver distance (with unit), one road text and the symbol; route state,
navigation status, ETA/RTA, destination distance and both speed limits are
not drawn there (other cluster pages unchecked). A change of road text alone
does not redraw the cluster: the new text appears with the next symbol change.
In the backend ABI (`OpenNaviBackendClusterV1`) the symbol uses the former
reserved byte after `legal_speed_unit` (layout and size unchanged; read only
with bit 2); the semantic `OpenNaviClusterNavigationState` gains it as an
appended member. The lower-level `open_navi_callback_send_cluster()`
remains the explicit expert passthrough for a caller that already owns a fully
verified exact OEM record; it performs no semantic repair.

Road text is supplied as an explicit pointer/length pair tagged UTF-8 or ASCII
and is deterministically serialized as little-endian UTF-32 with a trailing
NUL. Although each OEM road array physically holds 255 UTF-32 units, the
semantic API enforces the consumer-safe limits of 127 Unicode scalars and 127
UTF-16 code units. The second limit prevents astral characters from overflowing
the downstream one-byte UTF-16 length. Invalid UTF-8, embedded NULs, non-ASCII
bytes in ASCII mode, and overlong values are rejected instead of truncated.

Destination distance is an unsigned integer limited to `0..65535`. Maneuver
distance is supplied in tenths and limited to `0..65535` (`0..6553.5`). The
backend formats both centrally with ASCII decimal digits, no locale, no sign,
and at most one maneuver fractional digit. Unknown and reserved record spans
are always zero. Fields whose validity bit is clear are also zero, so a shorter
update cannot retain stale route data.

After unsupported bits are rejected, `valid_attributes` is copied exactly from
the caller. `changed_attributes` is calculated over every supported semantic
field, including validity changes. The first accepted publication marks every
valid field changed. Each publish receives a monotonic history token; no
history mutex is held across PAS. Cross-thread publications are serialized by
an owner/condition gate so every wire change mask is based on the record the
receiver most recently observed. Synchronous same-thread reentry remains
allowed without holding a lock across host code and uses the outer provisional
record as its baseline. A callback commits its owned snapshot only when it
succeeds, its reset epoch is unchanged, and its request sequence is newer than
the last successful commit. Thus a newer successful nested publication cannot
be overwritten by an older outer completion, while a newer failed nested
attempt does not suppress an older eventual success. Reset moves to a new epoch
and invalidates every older in-flight request. The serialization-only
`open_navi_cluster_backend_serialize()` seam takes an explicit changed mask, does
not publish, and does not modify history.

Cluster transport acquisition and semantic history commit are covered by one
outer lifecycle epoch lease. Initialization clears
stale root/adapter proxies before registration; teardown closes acquisition,
waits for in-flight cluster calls without holding the gate across host code,
then clears proxies. A synchronous callback-side teardown is marked for
deferred full teardown: it first waits for foreign in-flight callbacks, returns
without waiting on its own stack, and the final outer release performs proxy
clearing and backend resets. This avoids both deadlock and post-reset callback
telemetry or semantic-history resurrection. Cluster
diagnostic generation records only accepted publications from the current
lifecycle, not attempted or teardown-invalidated sends.

For OsmAnd (`backends/osmand/cluster_payload.cpp`), the route phase maps to
the recovered route-state enum and route availability to the navigation-status
enum; destination distance is a nonnegative integer in the selected unit and
maneuver distance is in tenths; ETA/RTA clock components and current/next
street names are passed as bounded UTF-8; compass and speed units map only to
the recovered enums. Lane validity stays clear (no lane picture rendered live
with any tried encoding). Bar fill is accepted only in `0..100`. The time
bytes remain unbounded because static evidence proves only their byte width,
not clock validation rules.

During guidance with a position every record carries a symbol and a valid
bar fill (the countdown, 100 from 400 m to 0 at the turn; 0 once arrived):

| Guidance | Symbol |
|---|---:|
| straight / continue | 9 |
| left, right | 1, 2 |
| slight left, slight right | 3, 4 |
| sharp left, sharp right | 5, 6 |
| keep left, keep right (fork) | 7, 8 |
| U-turn (OsmAnd TU), right U-turn (TRU) | 10, 28 |
| roundabout, right-hand traffic (RNDB) | 19..26 |
| roundabout, left-hand traffic (RNLB) | 36..43 |
| next manoeuvre is the destination | 15 |
| arrived | 27 |
| recalculating | 16 |

A roundabout takes the clock position nearest to OsmAnd's exit angle
(`TurnType::getTurnAngle()`, positive clockwise from the entry direction):
12:00 = 0 degrees, 1:00 = 30, 3:00 = 90, 4:00 = 120, U-turn = 180, 7:00 = -150,
9:00 = -90, 11:00 = -30. The set follows OsmAnd's driving side (RNLB is its
clockwise roundabout); the worker does not set OsmAnd's
`leftSideNavigation`, so routes are calculated for right-hand traffic.

Text-only change: when a new record keeps the published symbol but changes a
road text, `apply_text_redraw()` sends it once with a neighbouring symbol of
similar meaning (left 1 -> 50, right 2 -> 49, straight 9 -> 56, a roundabout
exit -> the same exit of the other traffic set, ...); the next publication
(the engine publishes at most every 500 ms) carries the real symbol again,
and both symbol changes redraw the cluster with the new text. The flash and
its 500 ms length are not yet confirmed live. Untested alternative: the
cluster ignores bits 6..7 (`n + 64` draws `n`), which might redraw without a
visible flash if `vs_can` forwards the whole byte; the semantic API does not
allow it.

## Native Screen/EGL milestone

The readiness worker creates a minimal native renderer after adapter 6 is
ready and before adapter 10 is announced, still before HMI readiness. This
matches the live v9 stock trace boundary and its observed native call order:
`screen_create_context` (type 0), `eglGetDisplay(EGL_DEFAULT_DISPLAY)`,
`screen_create_window`, `screen_set_window_property_cv` for property 7 with
the exact eight bytes `NAV_main`, two Screen window buffers, then
`eglCreateWindowSurface`. The surface attribute list is exactly
`{EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE}`.

The renderer prefers EGL config ID `0x16`, validates WINDOW and GLES2 support,
and falls back only to another config with both required capability bits. A
dedicated bounded event-loop thread owns every Screen/EGL object for its whole
lifetime. It makes the context current for surface-created, render, and
surface-destroyed callbacks, swaps only successful frames, and releases the
binding before `stop` or an event wait. Teardown
destroys EGL objects before the Screen window/context and intentionally does
not call `eglTerminate` on the shared display. Every creation failure records
its exact Screen errno or EGL/GL error and remains non-fatal to the recovered
protocol sequence.

## Versioned map-backend seam

[`include/open_navicore/backend.h`](../include/open_navicore/backend.h) is the stable C
boundary used by the optional in-process OsmAnd adapter. `OpenNaviBackendV1`
and every payload start with `abi_version` and `struct_size`; descriptors are
copied at registration, and all event payloads are owned copies. The v1
contract covers backend start/stop, render-surface availability and geometry,
visibility/interactivity, screen changes, normalized GPS, pointer press,
release and move, and deterministic frame callbacks. The render target includes the
native Screen window and EGL display/surface/context handles.

`OpenNaviBackendV2` is a separate registration contract; the V1 structure and
`open_navi_backend_install_v1()` ABI are unchanged. Its first 48 target
bytes are the exact V1 callback prefix, followed by renderer-thread callbacks
for recovered semantic state:

- VehicleData slots 3/4 publish day/night status, slots 5/6 publish the
  day/night setting, and slot 8 publishes the exact soft-float speed bits.
- GPSData slot 6 publishes the requested map-matched feedback interval.
- AudioHandling slots 17/19 publish source and narrow volume, retaining the
  request handle for slot 17 and zero for the slot-19 notification form.

`OpenNaviBackendV3` preserves the complete V2 prefix and adds only
`start_v2`, which receives `OpenNaviBackendServicesV2`. V1 and V2 backends
still receive the original 16-byte ARM services object, so their binary and
startup contracts are unchanged. The V2 services prefix retains
`request_frame` and appends exactly three provider-owned outbound operations:

- publish semantic instrument-cluster navigation state;
- publish audio-source availability states 1 through 4;
- publish a real backend thread's bounded ASCII status.

`OpenNaviBackendV4` preserves the complete V3 prefix and appends two
renderer-thread input callbacks needed by a future map backend. Internationalization
adapter 7 slot 3 is delivered as a fixed-width payload containing the request
handle, presence flag, native-little-endian 16-bit language value, and raw
date/time-format bytes. The four-byte stock record is copied synchronously;
the enum dictionaries remain unknown and the ABI deliberately assigns no
locale or format labels. InstrumentCluster adapter 17 slot 6 is delivered as
the request handle plus the raw narrow enable byte. The recovered stock name
is `onRequestTBTSettingsEnableStatus`; treating it as a TBT-display gate is an
integration interpretation, and values other than the single observed `1`
remain uninterpreted. Both inputs use latest-state mailboxes in the global
renderer ordering domain and are discarded at lifecycle boundaries. V1/V2/V3
install descriptors and query-V1/query-V2 handshakes remain unchanged.

`OpenNaviBackendV5` preserves the complete V4 prefix and appends one raw
driver-distraction callback. Diagnosis_Navigation adapter 13 slot 3 retains
its request handle; slot 4 is the subscription notification and carries a
zero handle. The status remains an uninterpreted `uint32_t` because only raw
value `0` was observed and no enum dictionary is recovered. Delivery uses the
same renderer-thread ordering/latest-state mailbox and lifecycle reset rules.
V1 through V4 descriptors and query V1 through V3 remain unchanged.

Connectivity adapter 20 intentionally remains bridge-owned typed telemetry.
Its request and subscription shapes are recovered, but the only observed
service ID is raw `6`, slot 8's first word is still unresolved, and observed
strength values include both `0` and `0xff`; treating these as a named network
or a strict percentage would freeze guesses into the backend ABI. Offline
OsmAnd rendering/routing has no concrete dependency on PAS connectivity, and
an eventual online feature can use the OS network layer. A raw connectivity
tail should be added only when a real backend consumer requires PAS policy and
the service/status/sentinel contracts are resolved.

The service payloads are individually version/size prefixed. Pointer-bearing
UTF-8/ASCII spans are borrowed synchronously and use explicit `uint32_t` byte
counts; no cross-DSO `size_t` is present. Cluster publication converts into
the existing validated semantic builder, which owns `0xa5c` serialization,
accepted-history change masks, PAS transport, and lifecycle epoch commit.
Route-symbol bit 2, lane-data bit 7, and unknown bit 12 remain rejected.
Audio keeps provider-side accepted-state deduplication and retry. Thread names
reject NUL, non-ASCII, and lengths over 63 bytes. Voice request handles and all
HMI replies remain provider-owned and are intentionally absent from this API.

Outbound calls are accepted only after `start_v2` succeeds and before stop.
The opaque generation token is never reused; a copied service from failed
start, fallback, stop, or an older lifecycle deterministically returns stale
before inspecting its payload. Calls are synchronous and thread-safe. Cluster
publishers apply blocking backpressure rather than building an unbounded
queue, and PAS rejection never advances cluster or audio accepted history.

Every V2 payload has its own version/size prefix and owned storage. Vehicle
valid bits accumulate for the provider lifecycle and its changed mask covers
all values coalesced into the delivery. Vehicle, GPS-interval, and audio each
use a latest-state mailbox in the same global ordering domain as V1 display,
screen, GPS, pointer, and frame events. Lifecycle reset discards pending state,
and semantic updates accepted for one lifecycle cannot enter the next. A V1
backend receives no V2 callbacks or extra semantic-triggered frames.
`surface_created` and `render_frame` run on the renderer owner thread with that
context current. Before `surface_destroyed`, the runtime makes it current again
so GL resources can be released. That callback is still guaranteed after a
make-current failure; `eglGetCurrentContext() == EGL_NO_CONTEXT` is the
unambiguous v1 context-loss signal and the backend must then skip GL calls.
`stop` runs after any current context is released. The
backend can request another frame through the call-safe `request_frame`
service. Its non-null opaque context is a never-reused lifecycle generation
token. A backend may copy the service values, but calls are accepted only from
`start` until stop begins. A copied service from any stopped or replaced
backend always returns failure and cannot enqueue into a later lifecycle.

Backend callbacks are serialized and run with no provider lifecycle,
readiness-worker, event-queue, or renderer mutex held. Visibility and screen
each have separate latest-state mailboxes for observed displays 0, 1, and 2;
GPS has one mailbox. Interleaved displays therefore never overwrite one
another. Unsupported display IDs are rejected and counted rather than
aliased. Repeated updates overwrite only the matching pending value and
advance its global ordering token, so a slow backend eventually receives the
newest state instead of a stale FIFO prefix while preserving order among each
display, GPS, frames, and pointer edges. Pointer input uses an ordered 32-entry
FIFO. Consecutive tail moves coalesce, moves cannot consume the final two
edge-reserve slots, and a full queue evicts its oldest move before dropping a
press or release. If no move remains, the oldest complete press/release pair
is evicted as a unit so overload cannot create an orphaned gesture. Saturating
accepted, dropped, coalesced, move/pair-evicted, and edge-drop telemetry makes
backpressure observable. Provider
GPS/HMI inputs are accepted only when their state was retained, including a
pre-render mailbox prepared at provider initialization. HMI queue capacity is
reserved first under its own lock; mailbox retention then commits the request,
or failure rolls back that exact tail reservation before the worker can see
it. Capacity rejection never publishes backend state. Renderer notification
only copies state and signals its thread; it never invokes a backend callback
synchronously on the provider call stack. Frame requests
coalesce to one pending frame.

A nonzero `start` fails renderer creation, while nonzero event or render status
drops only that event/frame. Concurrent external teardown has one joiner.
Renderer-thread identity is checked before all join-owner and wait branches,
so destroy is safe from start, surface-created, render, state, pointer,
surface-destroyed, and stop callbacks—even when an external thread already
owns the join. A subsequent create waits until all old callbacks, platform
destruction, and thread joining have completed. The built-in backend uses this
same interface and preserves the unmistakable bright-magenta output.

The QNX platform owns one `screen_event_t` on the renderer thread and performs
bounded nonblocking `screen_get_event(..., 0)` drains. A monotonic-clock,
ten-millisecond timed
condition wait discovers touch while idle; the existing stop broadcast still
wakes teardown immediately. Only events whose `SCREEN_PROPERTY_WINDOW` is the
owned `NAV_main` window are accepted. Multitouch TOUCH/MOVE/RELEASE maps to one
tracked primary contact; secondary contacts are counted and ignored because
the V1 payload is intentionally single-pointer. The first input class to
produce a valid press owns the renderer generation, so a platform exposing
both POINTER and MTOUCH forms cannot duplicate a gesture; rejected-class
events are counted without changing that class's button/contact shadow state.
POINTER actions are derived from button transitions.
`SCREEN_PROPERTY_SOURCE_POSITION` is preferred. Raw
screen position is used only when creation-time position, rotation, window,
source, and buffer geometry prove an identity transform. Coordinates outside
the owned render target are rejected and counted; a release with a missing or
out-of-bounds coordinate uses the last valid owned coordinate so an active
gesture cannot remain stuck. No QNX display handle is guessed to be one of the
stock logical HMI display IDs.

The Screen drain consumes at most sixteen events per renderer iteration and
assigns accepted input the same global sequence used by provider, GPS, display,
and frame events. A consumed but rejected input is reported as such by both
platform implementations instead of being confused with an empty Screen
queue. Teardown is checked between every bounded drain batch, so even a
perpetual unrelated-event backlog cannot starve stop/join. The drain never
invokes a backend callback itself. An own-window
UNREALIZE or CLOSE becomes a controlled renderer failure followed by normal
surface/backend/platform cleanup and immediate rejection of further provider
updates. EINTR is retried and other Screen read/property errors are counted as
transient instead of tearing down the renderer. Other Screen events are bounded
and counted. Window sensitivity is observed and reported but is not forced;
that policy remains a live-radio validation item.
All decoder, platform-stub, queue, and contact state is reset at lifecycle and
renderer generation boundaries.

Host tests use deterministic raw Screen events through the platform stub and
cover idle discovery without a producer signal, foreign windows, malformed
properties, identity fallback, primary/secondary touch, pointer button
transitions, mixed POINTER/MTOUCH arbitration and reset, ordering, move floods,
release preservation, stale lifecycle discard, perpetual unrelated-event
teardown, and reentrant teardown from MOVE. Live validation is still required
to prove that the radio routes physical input to the recreated `NAV_main`
application context and that its source coordinates match the rendered
800-by-480 buffer under the installed Screen class policy.

The optional OsmAnd backend introduced by PR #2 rendered a fixed Kyiv map
through the provider-owned EGL surface in the stock UI. The foundation
refactor separates its C entry points, session policy and concrete engine;
see [the backend runtime contract](../backends/osmand/README.md) for failure,
context-loss and pending-frame behavior. Offline tests do not requalify that
refactored binary set on the bench.

## Process-lifetime backend plugin

The provider now has one production backend-selection path. During
`panaInitialize`, after entering `STARTING` but before renderer lifecycle
preparation, it attempts exactly once per process to load the compile-time
absolute path
`/open-navicore/libOpenNaviBackendOsmAnd.so` (inside `navigation.ifs`, next
to the provider). There is no
environment override, `LD_LIBRARY_PATH` lookup, basename search, arbitrary
configuration path, or constructor-based registration. `dlopen` uses
`RTLD_NOW | RTLD_LOCAL`, and every successfully opened handle is pinned until
process exit, including a DSO whose query is rejected. Provider teardown never
calls `dlclose`.

The plugin exports exactly one C function, `open_navi_backend_query_v1`. The
same symbol accepts query ABI V4 first and falls back through the byte-for-byte
query-V3, query-V2 and query-V1 contracts for older reviewed plugins. Query V4
copies a `OpenNaviBackendPluginV4` handshake and V5 descriptor; query V3 copies a
`OpenNaviBackendPluginV3` handshake and V4 descriptor into provider-owned
memory; query V2 continues to copy the prior
`OpenNaviBackendPluginV2` handshake and V3 descriptor into provider-owned
memory. The
provider rejects anything that does not exactly match both magic words, query
ABI and structure size, byte-order tag, pointer width, V1/V2 descriptor and
payload sizes, the V2-services/V3/V4/V5/payload sizes, the complete known capability
set, the reviewed 32-byte build identifier, descriptor identity, or every
required callback. A rejected or
missing plugin leaves the existing backend selection untouched; in normal
production startup that means the built-in magenta backend. This also preserves
the documented explicit-registration API for standalone embedders and tests.
The original query-V1 build identifier remains unchanged for compatibility;
query V2 has a distinct reviewed identifier so it cannot be mistaken for the
older contract, query V3 has another identifier for the V4 input tail, and
query V4 has one for the V5 distraction tail.

For a valid query-V4 plugin, the provider installs its own copied V5 wrapper;
query-V3 and query-V2 plugins retain the prior V4 and V3 wrappers. No path installs the plugin
descriptor directly. The wrapper resets selection on
every renderer lifecycle, gives the plugin a separate never-reused service
generation rather than the renderer's token, and revokes it before failed
start, surface/render fallback, stop, or a later lifecycle. Query-V1 plugins
retain the original V2 wrapper and receive no outbound service tail. Both
paths preserve the existing callback serialization and
EGL ownership rules. Plugin `start` failure immediately selects default. A
`surface_created` failure selects default in the still-current context but
defers plugin `stop` until the context has been released. A `render_frame`
failure draws default magenta in that same frame, stops sending events to the
plugin, and deliberately retains the failed plugin's GL objects until normal
`surface_destroyed`; this avoids unsafe GL teardown from inside a render
callback. Normal cleanup calls every successfully surfaced backend's
`surface_destroyed` while current, then every successfully started backend's
`stop` after release. The next provider lifecycle tries the pinned plugin
again. Stale copied frame services remain rejected by the renderer generation
token.

`build/qnx/libOpenNaviBackendReference.so` is a tiny, visibly rendering QNX ARM
reference plugin, not OsmAnd. In `render_frame` it uses the provider-current
GLES2 context, resets the framebuffer, scissor, color-mask, and viewport state,
clears the complete target to cyan, finishes the draw, and reports any GL
error. It neither owns EGL nor swaps buffers, and owns no GL resources, so
surface loss and cleanup require no GL calls. Its cyan output is deliberately
different from the built-in magenta fallback. It has hidden visibility plus a
linker export map, so its query is the only public function. Host lifecycle and
fault-injection DSOs are compiled headless; a fake-GLES test separately proves
the exact production state reset, color, viewport, completion, and error path.
Host tests load real success and fault-injection DSOs
through the actual `panaInitialize` and readiness-worker boundary. A
test-only reference-plugin build publishes one validated cluster/audio/thread
set on its first render; the production reference build does not synthesize
those stock-facing states. Tests cover
two lifecycles, bootstrap idempotence, absent and malformed plugins, all strict
handshake fields, missing capabilities and callbacks, start/surface/render
fallback, provider-owned payload validation and copying, accepted-history
change masks, audio retry/dedup, thread bounds, worker join, stale-service
rejection, cleanup ordering, and pinned handles.
`tests/check_plugin_elf.sh` additionally enforces ARM/EABI metadata,
the exact export set, no RPATH/RUNPATH/TEXTREL, and an allowlist containing
only `libc.so.3` and the SDK's actual `libGLESv2.so.1` SONAME.
`build/qnx/libOpenNaviBackendOutboundTest.so` is the separately named QNX build of
that controlled path. Once per successful service-enabled lifecycle, on render sequence
one, it publishes cluster valid mask `0x0003` (follow-street `1`, no-route
`3`), audio availability `1`, and thread `(id=1, status=1,
name=ref-worker)`. Its trace records the trigger, all values, and each service
return code. It is a live-test artifact and must not replace the production
reference plugin after the test.
At the typed PAS boundary the provider also records one bounded line per
attempt in `/tmp/open-navi.log`: cluster adapter/slot, fixed `0xa5c` size,
route/navigation bytes, little-endian changed/valid masks and PAS status;
audio adapter 10 slot 10, state and status; and root slot 10, thread ID,
normalized ASCII name (at most 63 bytes), thread status and PAS status. These
records are emitted after the synchronous PAS call returns and do not change
callback results or accepted-history behavior.
`build/qnx/tests/plugin_smoke` is an ARM/QNX loader
harness for the production provider bootstrap. On a target, install the
reviewed reference plugin at the production absolute path, then run
`open_navi_backend_plugin_smoke ./libNaviCore.so`; it succeeds only if
`panaInitialize` causes that plugin's query to run, the real proxy/readiness
sequence reaches renderer creation, and normal teardown records plugin
start/surface-created/surface-destroyed/stale-service rejection/stop. The
harness does not call the registration export or load a caller-supplied plugin
path. It is built and ELF-checked offline here, but still requires a QNX target
to execute.

The reference-plugin trace at `/tmp/open-navi-reference-plugin.log` records
`query_v4`, `query_v3`, `query_v2`, or `query_v1` for every requested query contract and records
`start_v2` on entry to the service-enabled startup callback. A live V5 selection is
therefore proven by `query_v4`, `start_v2`, and the normal surface/render
records rather than inferred from the cyan frame alone.

The fixed path must be deployed atomically from reviewed artifacts in a
root-owned directory that is not group/world writable. The build identifier is
an ABI/deployment pin, not a cryptographic signature; deployment tooling must
continue to verify the full artifact hash.

The reference-plugin result proves that an independently built C backend can
be placed behind the stock provider lifecycle. Separately, PR #2's optional
renderer-enabled OsmAnd binary set proved fixed Kyiv rendering in that UI.
Live positioning, pan/zoom, routing and guidance remain unimplemented. Repeat
lifecycle/context-loss tests for the foundation refactor remain a bench task.

## Observable startup trace

Every exported call and adapter request is sent to syslog and also appended to
`/tmp/open-navi.log`. The file trace uses only libc/POSIX
`open(O_APPEND)`, `write`, and `close`; it is best effort, bounded to one short
line per event, preserves the caller's `errno`, and never changes an ABI return
value. This provides direct proof of how far the loader progressed even when
the product's syslog routing is unavailable. Failure to create or append the
file is silently ignored. New trace files are owner-only (`0600`), and the
logger requests `O_NOFOLLOW` where the target headers support it.

The trace includes raw pointer values for startup triage. Treat it as temporary
diagnostic output and remove it from later production-oriented builds.

## On-target loader smoke test

Before involving the stock service, copy `libNaviCore.so` and
`open_navi_smoke` to a writable test directory on a QNX target and run:

```sh
./open_navi_smoke ./libNaviCore.so
```

The harness performs `dlopen(RTLD_NOW)`, resolves all four exports, calls the
version and initialization entries, obtains the root, invokes vtable slot 3
for adapter enum 0, calls deinitialization, and closes the DSO. It prints each
checkpoint and exits zero only after the complete sequence. This is a
standalone provider test; it does not start, stop, configure, or replace the
stock navigation service. The provider also records the calls in
`/tmp/open-navi.log`.

## Current safety boundary

For the captured QR39.6 executable, the provider-side static call boundary is
now closed: all 140 reachable root/adapter slots have fixed, compiler-checked
signatures and ACTIVE-state rejection, while the eight code-present rows for
adapters 21, 27, and 28 remain generic only behind `getAdapter` rejection.
The verifier fails if a reachable row becomes generic or variadic. This is a
safe ABI-reception boundary, not proof of stock navigation behavior.

An undecoded `uintptr_t` word is treated as input-only only when the recovered
caller supplies it as an outbound value and consumes only the returned status;
there must be no store-through, post-call output read, by-value aggregate,
64-bit scalar, or hidden return-buffer evidence. Known pointer layouts instead
use their typed bounded-copy implementations. No reachable provider virtual
method was found to have an output pointer. The actual recovered output paths
are root slot 3 `getAdapter(self, enum, void **)`, which writes `NULL` before
any rejection and a stable object only on success, and exported
`panaVersionCheck`, which initializes every field of each non-null client and
compatibility output. Full rules and provenance are in
[`open_navicore-re/docs/abi/provider-boundary.md`](../../open_navicore-re/docs/abi/provider-boundary.md).

The target is 32-bit ARM EABI5 soft-float. Opaque scalar/pointer calls are
represented by one 32-bit `uintptr_t` per proven core-register/stack word;
arguments beyond `r3` continue under the compiler's AAPCS stack convention.
This does not authorize doubles, 64-bit values, or by-value aggregates. The
known vehicle-speed float remains a real typed `float`, with its soft-float
`r1` bit transfer independently checked in the ARM object.

Many fixed-word methods deliberately provide only lifecycle/status acceptance
and diagnostics. Their domain state machines, enum meanings, callback/reply
requirements, timing, and payload semantics are not recovered, the three stock
unavailable adapters remain unavailable. The reference backend's solid-color
surface is a rendering-contract probe; the OsmAnd backend is the navigation
(map, search, routing, guidance). Neither claims stock feature parity for the
services listed above.

Installing on a radio is described in `deploy/navigation-ifs/README.md` (one
image with all code, one reboot): keep the stock provider and an independent
rollback path (the disable marker, SD recovery) before any device change.

For ABI discovery beyond guessed C prototypes, the research repository's
[`open_navicore-re/docs/development/raw-callback-capture.md`](../../open_navicore-re/docs/development/raw-callback-capture.md) component generates a
prototype-free ARM forwarding trampoline for every one of the 199 application
slots in the 25 PAS callback interfaces. It records bounded register/stack and
return snapshots into a nonblocking fixed ring and converts drained records
into hash-guarded `arm_snapshot_replay.py` starting fixtures. It is a separate
artifact and is not linked into or deployed with the current provider.
