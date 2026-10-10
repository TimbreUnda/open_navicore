# Open NaviCore technical reference

This is a standalone open ARM/QNX replacement provider for the QR39.5/QR39.6
`nv_navigation` loader. It does not link to or load the production
`libNaviCore.so`.

The recovered ABI is documented in
[`analysis/mock_navicore_abi.md`](../analysis/mock_navicore_abi.md). The mock
implements its four unmangled C exports:

- `panaVersionCheck(ServerVersionHeadUnit, client_out, compatibility_out)`
- `panaInitialize(init_record)`, returning status zero on success
- `panaGetSoftwareInfrastructure()`
- `panaDeinitialize()`

## Build and verification

```sh
make
make verify
make test
```

The QNX 6.6 ARMv7 little-endian artifacts are `dist/libNaviCore.so` and the
standalone `dist/mock_navicore_smoke` loader test.
Intermediate files default to `/tmp/mock-navicore-build`. The SDK locations
can be overridden with `QNX_SDK`, `HOST_LIBS`, `BUILD_DIR`, and `DIST_DIR`.

`make verify` checks ELF32/ARM/little-endian metadata, EABI5 soft-float flags,
SONAME, the four stock exports plus V1/V2 backend-registration exports, and their
Thumb-bit symbol values. `make test` runs host-native ABI behavior,
lifecycle/concurrency, backend event-loop, and file-trace
tests. The ABI test
checks the 24-byte server-version call, all client-version output fields,
initialization status, root identity, root vtable slot 3, stable identity for
all 256 per-enum mock adapters, and all 128 dummy virtual slots on each. The
trace test verifies append output and preservation of the caller's `errno`.
The host build substitutes a deterministic renderer stub and verifies that
renderer creation occurs exactly once between adapter 6 and adapter 10, and
that teardown occurs exactly once after the worker is quiescent. The lifecycle
test covers COLD/STARTING/ACTIVE/STOPPING serialization, COLD and STOPPING
rejection across provider classes, reentrant and concurrent teardown,
callback-triggered teardown from producer and readiness-worker paths, producer
races, epoch-qualified callback telemetry, and a clean second worker/renderer
lifecycle. The fake-backend suite verifies callback ordering, hide/show,
screen, GPS, pointer press/release, frame requests, nonzero callback status,
stale-service rejection across backend replacement, latest-state coalescing,
observable pointer overflow, timeout-bounded reentrant teardown at every
callback phase, interleaved final state for all three displays, atomic HMI
capacity rejection, forced cleanup context loss, concurrent teardown, and
rejected ABI registrations.

Both targets also run the fail-closed provider API coverage gate. The
machine-readable [`contracts/qr39/provider/api-coverage.csv`](contracts/qr39/provider/api-coverage.csv)
contains exactly the 36 adapter/slot pairs observed in the v11 provider trace
plus separately recovered GPS 9/4. Each row records the recovered prototype and
canonical argument shape, concrete function and vtable, compiler-checked C
type, behavioral level, test evidence, lifecycle rejection, and backend route.
The checker compares that catalog to the authoritative provider and pointer
matrices, expands the real vtables, and rejects missing, duplicate, mismatched,
generic, or variadic observed entries. `make provider-coverage-selftest`
additionally proves that duplicate, missing, prototype-corrupt, and
generic-symbol manifests fail.

The broader
[`contracts/qr39/provider/slot-manifest.csv`](contracts/qr39/provider/slot-manifest.csv) is the
QR39.6 static-call-graph boundary. It records 140 root/adapter slots reachable
by the stock executable and eight code-present slots belonging to adapters 21,
27, and 28 that stock `getAdapter` deliberately rejects. Recovered but not yet
semantically decoded calls use fixed, compiler-checked AAPCS word signatures;
they never dereference or retain an opaque word and return zero only while the
provider is ACTIVE. Known pointer layouts continue to use their narrower typed
implementations and bounded copies. `tests/check_static_provider_slots.py`
independently embeds the expected slot/word catalog, expands every concrete
vtable, checks the generated declarations, and rejects any reachable generic
or variadic binding. `make static-provider-coverage-selftest` proves seven
negative corruptions fail. The host smoke test invokes every generated method
both ACTIVE (success) and after teardown (rejection).
The manifest's repeated per-adapter `static_evidence` addresses are provenance
anchors, not a claim that every row originates at the same instruction; their
interpretation, input-only rule, and ABI limits are recorded in
[`docs/abi/provider-boundary.md`](docs/abi/provider-boundary.md).

The static evidence was checked against the QR39.6 executable with SHA-256
`8c8b5010d3db9062f2c970a8c932d707615eb7c40e1d4e9ad8f8eb9060b796ae`.
Its SI function/call graph and addresses match QR39.5; the binary difference is
outside these provider dispatches.

The first executable original-code fragment oracle is documented in
[`docs/development/arm-snapshot-replay.md`](docs/development/arm-snapshot-replay.md). It replays a guarded stock
Thumb callback thunk with explicit register, stack, object-graph, external-call,
fault, and execution-budget state. `make arm-snapshot-replay-selftest` proves
the baseline plus lane/return counterfactuals, graph relocation, null branch,
and failure guards. This is deliberately bounded fragment evidence, not a
claim to emulate the complete QNX process.

The behavioral labels are deliberately bounded: `semantic` means the mock has
an asynchronous/reply or renderer/backend effect, `owned_copy` means caller
pointer storage is synchronously copied into mock-owned state, and
`typed_telemetry` means the ABI and values are preserved but no stock feature
semantics are claimed. Coverage therefore establishes safe typed reception and
lifecycle behavior, not complete parity for optional diagnosis, connectivity,
phone, SiriusXM, or other telemetry-only services. GPS engineering units and
some recovered enum dictionaries also remain unknown.

## Mock object model

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
`slot4(self, const void *gpsRecord)`. The mock accepts both non-null and null
record pointers and returns success synchronously. Static analysis of
`SI_GPSData::OnLocationUpdateEvent` proves the input is a `0x120`-byte
little-endian record. Slot 4 copies exactly those bytes to a local array before
parsing; neither the parser nor backend receives or retains the caller pointer.
The normalized `MockGpsSnapshot` includes raw altitude, fix, heading, dilution,
accuracy, satellite, speed, quality, counter, distance, altitude-change, and
noise fields at the recovered offsets. Latitude and longitude are signed
degrees scaled by `1e7`; all other quantities deliberately keep `_raw` names
because their engineering units and masks are not yet proven. The sixteen
12-byte satellite entries remain opaque byte arrays.

`mock_gps_backend_submit()` retains diagnostic latest-value state. The same
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
of the QNX ARM mock object independently shows `mock_vehicle_speed` moving
incoming `r1` unchanged into the backend call.

`mock_scalar_backend` synchronously copies these values under a mutex and is
the isolated future seam for OsmAnd theme, feedback pacing, and prompt-volume
logic. Per-method saturating call counts and per-domain generations are kept.
No engineering meaning or range validation is imposed beyond the proven C
types. Statically reachable calls whose semantic types remain unknown use the
fixed-word acceptance layer rather than a variadic stub. Scalar state resets
on initialization and deinitialization.

All other `scalar_input` rows from the 36-row v11 provider matrix are also
typed. Each writes a bounded `mock_api_telemetry` entry keyed by adapter and
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

`mock_pointer_backend` synchronously converts all six recovered pointer APIs
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
display zero, screen type, and two zero fields; the mock replies through host
slot 10 as `(handle, 0 /* success */, screenType)`. The theme path calls
provider byte offset `0x28` (slot 10) with its generated handle and theme-name
pointer; the mock replies through host slot 12 with that same handle. This
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

The reverse NaviCore-to-PAS calls used by the mock no longer share a variadic
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
- `mock_cluster_backend_publish()` builds the recovered `0xa5c` cluster record,
  calculates field-granular changes against the last accepted publication,
  and synchronously forwards it through InstrumentCluster slot 3. A rejected
  callback does not advance publication history.
- A provider Voice slot-19 request is answered through callback slot 23 with
  that exact incoming handle and the backend's current result/guidance state;
  no timer or captured handle is replayed.
- `mock_callback_signal_thread_status()` forwards root slot 10 with a terminated
  wide name converted from at most 63 input bytes and stores an owned bounded
  diagnostic copy.

Optional service subscriptions from the captured boot are omitted where the
mock has no corresponding backend instead of synthesizing stale settings.

## Instrument-cluster backend and OsmAnd mapping

[`include/open_navicore/cluster.h`](include/open_navicore/cluster.h) is the
public producer boundary for a future OsmAnd adapter. It exposes route and
navigation state, numeric destination and maneuver distances plus recovered
units, ETA/RTA bytes, bounded road text, lane-active state, bar fill, compass,
and both speed-limit fields. Its semantic supported mask deliberately rejects
route-symbol bit 2, lane bit 7, and unknown bit 12 because their encodings or
meaning remain unresolved. The lower-level `mock_callback_send_cluster()`
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
`mock_cluster_backend_serialize()` seam takes an explicit changed mask, does
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

For OsmAnd, map its route phase to the recovered route-state enum and its
route availability to the navigation-status enum; convert destination distance
to a nonnegative integer in the selected unit and maneuver distance to tenths;
pass ETA/RTA clock components and current/next street names as bounded UTF-8;
and map compass/speed units only to the recovered enums. Keep route-symbol and
lane validity clear until a separate adapter owns a verified OEM encoding
table. Bar fill is accepted only in `0..100`. The time bytes remain unbounded
because static evidence proves only their byte width, not clock validation
rules.

## Reverse callback catalog coverage

`mock_pas_callbacks` is the typed NaviCore-to-PAS dispatch catalog. Its methods
take an explicit registered proxy and use a distinct recovered adapter/slot
signature; catalog presence never causes an emission. Existing startup,
HMI-reply, audio-transition, cluster, voice-provider-reply, and real thread
paths invoke the catalog, while optional methods remain callable seams for a
future backend.

[`contracts/qr39/callbacks/runtime-coverage.csv`](contracts/qr39/callbacks/runtime-coverage.csv) accounts for every one of the
61 rows in the authoritative v11 callback matrix:

| Coverage state | Rows |
|---|---:|
| `implemented_typed` | 61 |
| `disabled_needs_trace` | 0 |
| `unknown_not_emitted` | 0 |

All callback matrix rows now have typed callable seams. This does not enable
optional emissions: catalog methods remain dormant until a real backend event
or provider request justifies them. Cluster 17/3 now has a semantic producer
for every proven field while unknown record spans, symbol values, and lane
encoding remain deliberately uninterpreted.

The 61-row matrix is runtime-observed coverage. A subsequent audit of stock
provider SHA-256 `fd1e9314...64842` proved seven additional
dispatches: root 5, HMI 1/4 and 1/5, GPS 9/6, Connectivity 20/6, TTS 24/4,
and Phone 26/4. They are recorded in
[`contracts/qr39/callbacks/dispatch-additions.csv`](contracts/qr39/callbacks/dispatch-additions.csv).
None is emitted. Exact concrete GPS and Phone receivers now prove both former
pointer layouts and their typed dormant seams.
[`docs/abi/callback-boundary.md`](docs/abi/callback-boundary.md) documents the
reproducible Ghidra extraction. The 35 statically recovered dense-thunk ABI
gaps are recorded in
[`contracts/qr39/callbacks/static-contracts.csv`](contracts/qr39/callbacks/static-contracts.csv):
all 35 have typed dormant seams after concrete-consumer recovery of the final
three pointer-bearing rows. The independent checker reconciles 61 runtime + 7 prior static +
35 recovered rows + 14 Audio concrete-consumer rows + 63 complete
cross-version concrete-vtable rows and runs negative
mutations. The 59-thunk dense block is
closed: 56 thunks have positive CI ownership and contracts, while three are
whole-image-unreferenced callback-shaped orphans explicitly excluded by the
machine-readable guard.

The callback proxy surface itself is independently complete in
[`evidence/qr39/static/pas-proxy-vtables.csv`](evidence/qr39/static/pas-proxy-vtables.csv) and
[`contracts/qr39/callbacks/proxy-slot-manifest.csv`](contracts/qr39/callbacks/proxy-slot-manifest.csv).
QR39.6 contains 25 `IF_Server*Proxy` vtables with 249 true entries: 50 C++
destructors and 199 abstract application slots. The earlier 252-row extraction
had crossed three vtable tails into the same unrelated
`std::ios_base::Init::~Init` pointer; the corrected dumper requires each
abstract application tail to remain on `__cxa_pure_virtual` and asserts the
25/249 identity. All 199 application slots have fixed typed seams (80
currently emitted surface rows and 119 dormant); no pointer or aggregate
contract remains fail-closed. No ABI-unrecovered slot remains and no
generic or variadic emitter is generated. The five safe scalar direct-static rows
that formerly lacked functions now have tested dormant seams. Fourteen
AudioHandling seams come from exact QR39.6/QR40 concrete receiver alignment;
[`docs/abi/cross-version.md`](docs/abi/cross-version.md)
records the binaries, slots, operands, and limits.

A subsequent bounded concrete-vtable pass covers all 63 former ABI gaps. The independent
[`evidence/qr39/cross-version/concrete-vtable-audit.csv`](evidence/qr39/cross-version/concrete-vtable-audit.csv)
partitions all 63 as fixed typed-capable contracts. The exact pointer roles,
extents, lifetime, and AAPCS
alignment recovered for the former pointer-fail-closed set are documented in
[`docs/abi/pointer-callbacks.md`](docs/abi/pointer-callbacks.md).
QR39.6 and QR40 resolve every row to the same concrete target and have
identical complete target listings. All 63 rows now have fixed dormant
production seams and host smoke coverage; the pointer rows retain their
invalid-extent tests. The four by-value layouts have a separate QNX compiler
oracle and are documented in
[`docs/abi/aggregate-callbacks.md`](docs/abi/aggregate-callbacks.md).
None of the 63 dormant seams has a provider-side emitter.

The remaining 1,740 exact syntactic chains are no longer an unaccounted blob.
[`evidence/qr39/static/callback-outside-inventory.csv`](evidence/qr39/static/callback-outside-inventory.csv)
classifies every chain using callback-object and call-receiver provenance: nine
are receiver-proven callbacks already present in the runtime/static catalogs,
498 are excluded by concrete proxy-slot or receiver evidence, and 1,233 remain
explicitly unresolved and non-callable. The reproducible Ghidra p-code
definition/use artifact
[`evidence/qr39/static/callback-receiver-dataflow.csv`](evidence/qr39/static/callback-receiver-dataflow.csv)
accounts for all 1,799 raw chains and proves 103 outside-dense exclusions
where the actual `r0` and vtable-source objects have disjoint origins. The
SHA/image-base-guarded instruction artifact
[`evidence/qr39/static/callback-arm-slices.csv`](evidence/qr39/static/callback-arm-slices.csv) analyzes all
657 unresolved decompiler receiver gaps: 646 prove machine receiver equality,
ten prove disjoint origins, and one call-clobbered pair remains ambiguous.
Equality is not ownership, so no contract was
promoted. The pass also corrected a false hand-audited mismatch at `0x00386e64`
back to fail-closed unresolved. Unique CI caller adjacency is not enough; in
total, 114 callback-shaped chains are now proven to call on a different `r0`
receiver. The unresolved split and next
proof for every slot group are in
[`evidence/qr39/static/callback-unresolved-next-actions.csv`](evidence/qr39/static/callback-unresolved-next-actions.csv).
The independent inventory checker fixes both raw key digests and the dataflow
relation totals, reconciles the nine callbacks to existing contracts, checks
all classification invariants and next-action totals, and runs nine negative
mutations. Of the 1,233 remaining chains, 1,142 have instruction-proven local
receiver equality but still lack registered-CI ownership, while 91 retain
conditional/loop ambiguity. Whole-provider callback closure is still
pending those concrete object-origin proofs; none is silently promoted or
treated as safe merely because it matches the object+4/vtable/slot syntax.

The historical ABI-gap set also has a reproducible QNX compiler oracle; see
[`docs/abi/qnx-arm-oracle.md`](docs/abi/qnx-arm-oracle.md). It compiles 30 paired
caller/callee declaration families (60 functions) with the installed QNX 6.6
ARM compiler, including 0–10 scalar words, 64-bit alignment, soft float/double,
POD-by-value, hidden sret, pointers/references, and C++ member/virtual forms.
The bounded declaration permuter records normalized instruction/dataflow
fingerprints, exact flags, match and runner-up scores, and permits only unique
exact two-sided hazard-free matches to become ABI-equivalent candidates. The
oracle continues to assess the historical 63 rows and reports zero oracle
promotions because no independently owned provider callsite is present on
both sides of that matcher. Separate identical QR39.6/QR40 concrete-consumer
evidence resolves 59 fixed dormant seams. A separate exact aggregate oracle,
combined with the concrete message-builder copies, resolves the final four.
This separation prevents
compiler similarity from being mistaken for source-level type or ownership
proof. Slot 2 remains the abstract common `adapterReady` application method,
not a reserved ABI slot; its recovered concrete implementations are handled
by the same evidence rule.

The equal-receiver subset now has a second, context-sensitive ownership pass.
[`evidence/qr39/static/callback-field-origins.csv`](evidence/qr39/static/callback-field-origins.csv)
preserves field offsets and expands direct incoming call arguments and return
paths for all 1,142 targets: 494 reach interprocedural terminals, one uses the
independent long-timeout local p-code result, and 647 remain machine-only
because Ghidra exposes no receiver varnode at the indirect call.
[`evidence/qr39/static/callback-field-origin-groups.csv`](evidence/qr39/static/callback-field-origin-groups.csv)
collapses address-only differences into 132 reproducible shapes with exact slot
counts and next actions. It promotes zero callbacks and positively identifies
two slot-2 candidates as provider-constructed non-callback objects; no callback
contract is invented. The
checker reconciles the exact target set, regenerates the groups, and includes
negative tests against forged ownership and incomplete evidence.

HMI 1/13 is now the exact typed `signalPressStateAnimation` seam. Its callback
shape is `(self, press_state, unused_r2, x, y)`: `press_state` arrives in `r1`,
the ignored-but-required placeholder remains in `r2`, `x` arrives in `r3`, and
`y` is incoming stack word zero. Stock accepts states 1 (press/down) and 2
(release/up), converts unsigned integer x/y to floats for the PAS event, and
returns 2 for other states. The mock catalog deliberately forwards every raw
state and placeholder unchanged; validation remains consumer-side, and no
touch event is synthesized by the provider.

Logging 6/4 is the exact typed `signalWriteLogData` seam
`(self, methods, reserved_ignored, encoding, severity, byte_count, text)`.
The placeholder stays in `r2`; encoding stays in `r3`; severity, count, and
text occupy outgoing stack words 0, 1, and 2. The wrapper performs no string
inspection, encoding validation, or copy because the recovered stock consumer
handles its call-scoped pointer synchronously.

Voice 25/19 is correctly identified as `replyLocationGet`, not route guidance.
It forwards the ignored handle, result, seven-entry type-list pointer, and four
string pointers unchanged and synchronously. No enum labels, tokenization, or
string bounds are invented in the provider wrapper. The stock callback owns
validation and returns its status directly.

Formerly unclassified slot 3 is now proven to be root
`SI_SoftwareInfrastructure` slot 3. `mock_pas_root_3` exposes its exact no-arg
callback ABI, but the public method name remains unknown and the mock does not
emit it.

Pointer-bearing catalog calls copy before invoking PAS: timezone is exactly
`0x22c` bytes, software version is exactly four bytes, cluster is exactly
`0xa5c`, data-exchange arrays are capped at 64 `uint32_t` elements and permit
only the proven `count=0, pointer=NULL` special case, and diagnostic/thread wide
strings are copied into bounded terminated arrays. The catalog does not retain
caller pointers and never holds callback telemetry mutexes across PAS calls.
`tests/check_callback_coverage.sh` compares matrix and manifest keys, rejects
duplicates or invalid states, and verifies every callback currently referenced
by the provider source is represented by an implemented typed row.

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

[`include/open_navicore/backend.h`](include/open_navicore/backend.h) is the stable C
boundary intended for the future in-process OsmAnd adapter. `MockNavBackendV1`
and every payload start with `abi_version` and `struct_size`; descriptors are
copied at registration, and all event payloads are owned copies. The v1
contract covers backend start/stop, render-surface availability and geometry,
visibility/interactivity, screen changes, normalized GPS, pointer press,
release and move, and deterministic frame callbacks. The render target includes the
native Screen window and EGL display/surface/context handles.

`MockNavBackendV2` is a separate registration contract; the V1 structure and
`mock_navicore_backend_install_v1()` ABI are unchanged. Its first 48 target
bytes are the exact V1 callback prefix, followed by renderer-thread callbacks
for recovered semantic state:

- VehicleData slots 3/4 publish day/night status, slots 5/6 publish the
  day/night setting, and slot 8 publishes the exact soft-float speed bits.
- GPSData slot 6 publishes the requested map-matched feedback interval.
- AudioHandling slots 17/19 publish source and narrow volume, retaining the
  request handle for slot 17 and zero for the slot-19 notification form.

`MockNavBackendV3` preserves the complete V2 prefix and adds only
`start_v2`, which receives `MockNavBackendServicesV2`. V1 and V2 backends
still receive the original 16-byte ARM services object, so their binary and
startup contracts are unchanged. The V2 services prefix retains
`request_frame` and appends exactly three provider-owned outbound operations:

- publish semantic instrument-cluster navigation state;
- publish audio-source availability states 1 through 4;
- publish a real backend thread's bounded ASCII status.

`MockNavBackendV4` preserves the complete V3 prefix and appends two
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

`MockNavBackendV5` preserves the complete V4 prefix and appends one raw
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

Workstation-local OsmAnd build evidence proves that the pinned OsmAnd Core
static archive and a QNX ARM smoke binary build, but not a map-renderer/EGL
integration. This change intentionally stops at the backend boundary: no
OsmAnd object is constructed or linked yet.

## Process-lifetime backend plugin

The provider now has one production backend-selection path. During
`panaInitialize`, after entering `STARTING` but before renderer lifecycle
preparation, it attempts exactly once per process to load the compile-time
absolute path
`/datastore/navicore-backend/libMockNavBackendOsmAnd.so`. There is no
environment override, `LD_LIBRARY_PATH` lookup, basename search, arbitrary
configuration path, or constructor-based registration. `dlopen` uses
`RTLD_NOW | RTLD_LOCAL`, and every successfully opened handle is pinned until
process exit, including a DSO whose query is rejected. Provider teardown never
calls `dlclose`.

The plugin exports exactly one C function, `mock_nav_backend_query_v1`. The
same symbol accepts query ABI V4 first and falls back through the byte-for-byte
query-V3, query-V2 and query-V1 contracts for older reviewed plugins. Query V4
copies a `MockNavBackendPluginV4` handshake and V5 descriptor; query V3 copies a
`MockNavBackendPluginV3` handshake and V4 descriptor into provider-owned
memory; query V2 continues to copy the prior
`MockNavBackendPluginV2` handshake and V3 descriptor into provider-owned
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

`dist/libMockNavBackendReference.so` is a tiny, visibly rendering QNX ARM
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
`dist/libMockNavBackendOutboundTest.so` is the separately named QNX build of
that controlled path. Once per successful service-enabled lifecycle, on render sequence
one, it publishes cluster valid mask `0x0003` (follow-street `1`, no-route
`3`), audio availability `1`, and thread `(id=1, status=1,
name=ref-worker)`. Its trace records the trigger, all values, and each service
return code. It is a live-test artifact and must not replace the production
reference plugin after the test.
At the typed PAS boundary the provider also records one bounded line per
attempt in `/tmp/mock-navicore.log`: cluster adapter/slot, fixed `0xa5c` size,
route/navigation bytes, little-endian changed/valid masks and PAS status;
audio adapter 10 slot 10, state and status; and root slot 10, thread ID,
normalized ASCII name (at most 63 bytes), thread status and PAS status. These
records are emitted after the synchronous PAS call returns and do not change
callback results or accepted-history behavior.
`dist/mock_nav_backend_plugin_smoke` is an ARM/QNX loader
harness for the production provider bootstrap. On a target, install the
reviewed reference plugin at the production absolute path, then run
`mock_nav_backend_plugin_smoke ./libNaviCore.so`; it succeeds only if
`panaInitialize` causes that plugin's query to run, the real proxy/readiness
sequence reaches renderer creation, and normal teardown records plugin
start/surface-created/surface-destroyed/stale-service rejection/stop. The
harness does not call the registration export or load a caller-supplied plugin
path. It is built and ELF-checked offline here, but still requires a QNX target
to execute.

The reference-plugin trace at `/tmp/mock-nav-reference-plugin.log` records
`query_v4`, `query_v3`, `query_v2`, or `query_v1` for every requested query contract and records
`start_v2` on entry to the service-enabled startup callback. A live V5 selection is
therefore proven by `query_v4`, `start_v2`, and the normal surface/render
records rather than inferred from the cyan frame alone.

The fixed path must be deployed atomically from reviewed artifacts in a
root-owned directory that is not group/world writable. The build identifier is
an ABI/deployment pin, not a cryptographic signature; deployment tooling must
continue to verify the full artifact hash.

This proves that an independently built C backend can be placed behind the
stock provider lifecycle. It does not prove OsmAnd rendering. The current QNX
OsmAnd archive omits its OpenGL/OpenGLES renderer objects, and a real adapter
still needs renderer-enabled OsmAnd, deterministic map/style assets, conversion
of GPS and pointer events, context-loss handling, and on-radio EGL/GLES tests.

## Observable startup trace

Every exported call and adapter request is sent to syslog and also appended to
`/tmp/mock-navicore.log`. The file trace uses only libc/POSIX
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
`mock_navicore_smoke` to a writable test directory on a QNX target and run:

```sh
./mock_navicore_smoke ./libNaviCore.so
```

The harness performs `dlopen(RTLD_NOW)`, resolves all four exports, calls the
version and initialization entries, obtains the root, invokes vtable slot 3
for adapter enum 0, calls deinitialization, and closes the DSO. It prints each
checkpoint and exits zero only after the complete sequence. This is a
standalone provider test; it does not start, stop, configure, or replace the
stock navigation service. The provider also records the calls in
`/tmp/mock-navicore.log`.

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
[`docs/abi/provider-boundary.md`](docs/abi/provider-boundary.md).

The target is 32-bit ARM EABI5 soft-float. Opaque scalar/pointer calls are
represented by one 32-bit `uintptr_t` per proven core-register/stack word;
arguments beyond `r3` continue under the compiler's AAPCS stack convention.
This does not authorize doubles, 64-bit values, or by-value aggregates. The
known vehicle-speed float remains a real typed `float`, with its soft-float
`r1` bit transfer independently checked in the ARM object.

Many fixed-word methods deliberately provide only lifecycle/status acceptance
and diagnostics. Their domain state machines, enum meanings, callback/reply
requirements, timing, and payload semantics are not recovered, the three stock
unavailable adapters remain unavailable, and the current OsmAnd archive still
lacks its GLES renderer. Consequently this build does not claim stock feature
parity or production navigation readiness. The solid-color native surface is
only a rendering-contract probe.

This project performs no deployment and makes no changes to a radio. Preserve
the production provider and establish an independent rollback path before any
controlled device-side experiment.

For ABI discovery beyond guessed C prototypes, the optional
[`docs/development/raw-callback-capture.md`](docs/development/raw-callback-capture.md) component generates a
prototype-free ARM forwarding trampoline for every one of the 199 application
slots in the 25 PAS callback interfaces. It records bounded register/stack and
return snapshots into a nonblocking fixed ring and converts drained records
into hash-guarded `arm_snapshot_replay.py` starting fixtures. It is a separate
artifact and is not linked into or deployed with the current provider.
