# Prototype-free PAS callback capture

`libMockRawCallbackCapture.so` is an offline-built instrumentation component;
it is not linked into `libNaviCore.so` and this repository does not deploy it.
It wraps a PAS callback object with a copied vtable and replaces all 199
application slots from the 25 QR39.6 proxy interfaces with generated ARM
trampolines. No callback prototype is guessed.

Each per-slot stub saves incoming `r12` and `lr`, identifies its interface and
slot, and enters one shared assembly path. That path saves `r0`--`r3`, entry
SP and CPSR flags on an eight-byte-aligned frame before calling the recorder.
The recorder copies only register words and up to eight words starting at
entry SP; it never follows an argument pointer. Stack copying is disabled
unless the owning thread's known-readable `[low, high)` stack range was
registered outside callback context. It copies only complete words below that
registered high bound, avoiding speculative reads across a guard/top page.
It then restores CPSR flags, the logical
argument lanes and exact entry SP and branches to the original vtable target.
The original callback receives its original object as `this`. Both ordinary
`this` in `r0` and an ARM hidden-return-buffer form with `this` in `r1` are
recognized by pointer identity against the fixed wrapper registry; arbitrary
argument pointers are not probed.

The target returns through an assembly continuation backed by a fixed return
context. It records `r0`/`r1`, restores the complete caller-scratch return
frame, and branches through the incoming LR. Literal LR cannot be presented to
the target because LR must name the continuation; this is the sole deliberate
call-state difference. `r12` likewise holds the branch target, as permitted by
AAPCS. The semantic argument lanes and stack presented to the target are
unchanged except that the wrapper `this` lane is replaced by original `this`.

Capture performs no allocation, locks, syslog, file I/O, clocks, thread API,
or argument-pointer dereference. It uses a 256-record CAS ring and 64 fixed
return contexts. A full record ring drops only the record. A full context pool
bypasses capture and tail-forwards the call with its original LR. Separate
drop counters expose both cases. The consumer drains completed records outside
the callback. `mock_raw_callback_wrapper_destroy` first makes a wrapper
unavailable and refuses destruction while a forwarded call is in flight; the
caller must unregister the wrapper from PAS before destruction.

Slots 0 and 1 are the Itanium complete/deleting destructors and are copied,
not instrumented. The wrapper is non-owning and must never be given to a
component that assumes ownership or invokes those destructor slots. This
matches the provider's callback registration contract but is a hard boundary,
not a guessed destructor ABI.

`mock_raw_callback_wrapper_init_indirect` supports the stock interposer's
late-bound registration layout. Its caller supplies the address of a trusted,
pointer-sized callback registration word. The recorder loads that word once
per invocation and resolves the corresponding vtable target; it does not
retain or inspect any argument pointer. This permits PAS to populate the proxy
after `getAdapter` returns while still forwarding directly to the current PAS
proxy. The registration word and proxy must remain alive until the wrapper is
unregistered, made inactive, and all in-flight calls have returned. Indirect
wrappers are application-slot capture objects only; their destructor entries
remain null and must never be invoked.

## Record and replay

`MockRawCallbackRecord` is the fixed 136-byte little-endian
`mock-nav-raw-callback/v1` record. It contains a caller-supplied 32-byte binary
SHA-256 identity, adapter/interface/slot identity, sequence, flags, `r0`--`r3`,
entry SP/LR/CPSR, target PC, original `this`, up to eight bounded stack words, and
returned `r0`/`r1`. Time and thread ID are deliberately omitted because
obtaining them without target-specific reentrancy assumptions is not safe;
the flag records that omission.

Drain records to a binary file as consecutive `MockRawCallbackRecord` values,
then combine one with a hash-matched replay template:

```sh
python3 -m tools.replay.raw_callback_to_replay capture.raw template.json replay.json \
  --sequence 17 --manifest contracts/qr39/callbacks/proxy-slot-manifest.csv
python3 -m tools.replay.arm_snapshot_replay replay.json
```

The converter maps the captured stack words, selects ARM/Thumb from target PC,
and imports the register state. It intentionally does not invent pointee
memory. The first guarded replay fault identifies the next object page that
must be captured and added to the fixture. The template binary SHA-256 must
equal the record identity.

Build and verify without changing the provider artifact:

```sh
make raw-callback-capture-selftest
```

The gate regenerates and byte-compares all 199 stubs, runs host ring/overflow
and wrapper-lifecycle tests, tests record conversion, builds the QNX ARM DSO,
and inspects real ARM disassembly for register saves, balanced/aligned frames,
exact-SP recovery, forwarding, and return continuation behavior.
