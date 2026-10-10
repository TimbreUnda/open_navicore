# QR39.6 static provider-slot provenance

This note defines what
[`contracts/qr39/provider/slot-manifest.csv`](../../contracts/qr39/provider/slot-manifest.csv) proves
and, just as importantly, what it does not prove.

## Input and identity

The authoritative executable is QR39.6 `nv_navigation`:

```text
${FIRMWARE_ROOT}/USA-ASO-8.4-QR-39.6.0-20210405-Q0H4/
  unpacked/encrypted_flash/extracted/seg4/pas/bin/nv_navigation
SHA-256 8c8b5010d3db9062f2c970a8c932d707615eb7c40e1d4e9ad8f8eb9060b796ae
```

It was imported as ARM little-endian ELF into Ghidra 12.1.3. Demangled
`SI_*` constructors establish the adapter enum identity. In each adapter,
provider calls load the proxy stored at object offset `+0x10`, load a function
from that proxy's vtable, and branch through it. Dividing the vtable byte
offset by four gives the manifest slot. Decompiled argument expressions were
cross-checked against the ARM call setup; stack arguments were included for
calls exceeding the available `r0`-`r3` core registers.

The `static_evidence` value is a stable QR39.6 function-entry provenance
anchor for the corresponding adapter analysis. Several rows intentionally
share an anchor because a class was audited as one dispatch cluster. It is not
the address of every individual `blx`. The independent expected slot/word
catalog in `tests/check_static_provider_slots.py` prevents a deleted or changed
CSV row from redefining coverage.

QR39.5 has the same named SI functions, entry addresses, and provider dispatch
graph. Its executable is not byte-identical; this audit and its hash refer to
QR39.6.

## Reachability classification

There are 140 reachable root/adapter slots. Each has either an existing
semantic typed method or a generated fixed-arity method from
`src/compat/qr39/provider_slots.def`. The checker expands the actual vtables
and rejects a reachable binding to `mock_generic_virtual`, a variadic
prototype, an arity mismatch, or a method without the shared ACTIVE lifecycle
gate.

Eight call sites are code-present but unreachable through the captured stock
construction path: adapter 21 slots 3-4, adapter 27 slots 3-7, and adapter 28
slot 4. Stock and mock `getAdapter` reject those enum values. Their ABI remains
explicitly `unknown`; the manifest does not infer a prototype merely because
dead class code exists.

## Why an opaque word can be input-only

An unknown argument is admitted to the fixed-word layer only when all of the
following hold in the recovered path:

1. The caller constructs or loads the argument as an outbound value and calls
   the provider proxy.
2. The caller consumes the provider call only as a status result; it does not
   read caller storage as newly produced output after the call.
3. There is no store-through parameter, hidden structure-return pointer,
   by-value aggregate, `double`, or 64-bit scalar evidence.
4. A pointer-shaped word is not dereferenced or retained by the mock. If its
   pointee layout and bound are known, the call instead uses a semantic typed
   implementation that synchronously copies only the proven extent.

Thus `input-only` describes ownership and memory access, not recovered domain
meaning. The generated methods record at most the first four word values for
diagnostics and ignore the remainder after accepting their ABI positions. They
never guess or inspect an aggregate layout.

## Output initialization

No reachable provider-side adapter virtual call in this audit has a recovered
output pointer. The output-bearing paths are:

- Root slot 3, `getAdapter(self, enum, void **interface_out)`: when the pointer
  is non-null, `*interface_out` is set to `NULL` before lifecycle or enum
  rejection. A stable adapter/overflow object is stored only on success. A
  null output pointer returns status one.
- Exported `panaVersionCheck`: a non-null client output has every API/version
  field initialized, and a non-null compatibility output is set to one.

Known provider input pointers have separate extent rules in
`contracts/qr39/provider/api-coverage.csv` and the pointer matrix used by
`tests/check_provider_coverage.py`. The HMI window/theme pointers are logged
synchronously only and are neither dereferenced nor retained in the
asynchronous reply queue.

## ARM ABI boundary

The target artifacts are ELF32 ARM EABI5 soft-float. `uintptr_t` is therefore
one 32-bit AAPCS core word. Generated methods declare exactly the recovered
number of words after `self`; the compiler places the first available words in
core registers and the rest on the stack. This representation is used only
for arguments proven to occupy individual words.

It is not a fallback for an unknown 64-bit scalar, `double`, by-value
aggregate, vector/VFP argument, or hidden return buffer. Discovery of any such
case would make that row unsafe until a real typed prototype was recovered.
Known non-word semantics retain real C types: notably VehicleData slot 8 uses
`float`, and ARM object inspection verifies the soft-float bit pattern arriving
in `r1` is forwarded unchanged.

All fixed-word methods return status zero only with a valid ACTIVE lifecycle
lease and status one before initialization or during/after teardown. This
matches the recovered provider status convention but does not synthesize a
feature completion callback.

## Remaining semantic boundary

The audit establishes callability, arity, ownership safety, status behavior,
and teardown rejection for the captured QR39.6 static call graph. It does not
establish stock feature parity. Many slots still lack decoded enum and payload
meaning, state-machine effects, reply/callback obligations, ordering, timeout
behavior, and error mapping. Adapters 21, 27, and 28 remain unavailable. Future
software builds may add paths outside this captured graph. Finally, the current
OsmAnd archive lacks its GLES renderer and has not undergone on-radio EGL,
context-loss, map/style, or navigation behavior tests.
