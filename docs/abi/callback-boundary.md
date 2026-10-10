# Stock callback-dispatch static audit (owned-thunk contracts closed)

## Inputs and identity

The provider dispatch source is the untouched USA stock `libNaviCore.so`
retrieved before mock deployment:

```text
/mnt/d/analysis/mock_navicore_runtime/final-mock-transaction-20261008-attempt1/
  live.before/libNaviCore.stock.so
size 19935860
SHA-256 fd1e931430cca61116c533cdf617dc805548e312a9131e972f55a7d30ab64842
```

It is an ELF32 ARM EABI5 shared object.  The callback target/type companion is
QR39.6 `nv_navigation`, SHA-256
`8c8b5010d3db9062f2c970a8c932d707615eb7c40e1d4e9ad8f8eb9060b796ae`.
The provider is the authoritative source for *which calls can be made*; the
PAS executable is the authoritative source for target behavior and pointer
consumption.

## Method

The stock provider was imported into Ghidra 12.1.3 as ARM little-endian and
fully analyzed.  `FindExactCallbackDispatches.java` recognizes the canonical
instruction chain that loads the client callback object from object offset
`+4`, loads its vtable, loads a word-aligned slot, and branches through it.
It found 1,799 exact syntactic chains across the complete 20 MB provider.
That count is deliberately **not** called a callback count: unrelated C++
objects also use a pointer at `+4`.

`AttributeCallbackDispatches.java` follows both normal and Thumb-bit function
references and walks back toward embedded `PanaNav::CI_*` pretty-function
strings.  Its raw output attributes 71 exact-chain functions, but that number
contains demonstrable false positives from shared helpers.  Unique CI caller
attribution and an in-bound QR39.6 `IF_Server*Proxy` slot are therefore only
leads, not ownership proof.  Callsite `0x0028bd06` meets those two weak tests
while both the audited instructions and independent machine slice prove that
the vtable-source object is not the `r0` call receiver.  It is excluded, not
promoted.  An earlier hand audit had also labelled `0x00386e64` a mismatch;
the reproducible instruction slice corrected that claim because its target is
loaded through unchanged `r0`.  It is now fail-closed as unresolved because
its registered-CI field origin is still not proved.

The densest stripped forwarding region (`0x00298034`–`0x00298a28`) contains 59
exact thunks.  Direct and Thumb-bit references plus pretty-function callers
uniquely own 24 immediately.  A second constructor/vtable proof owns all 25
member pointers installed by constructor `0x0028ce5c`: its installed vtable at
address point `0x012a2f88` contains independently named
`CI_VoiceRecognition` methods.  A final constructor/member-pointer and named
caller pass proves another 14 owners.  The final dense result is 56 uniquely
owned thunks and three callback-shaped orphan functions.  Those three have no
normal-address, Thumb-address, data, vtable, or member-pointer reference in the
whole loaded provider image and are machine-marked
`excluded_unreachable_orphan`; they are not assigned invented CI owners.  The
complete machine-readable split is
`evidence/qr39/static/callback-thunk-ownership.csv`, guarded by
`tests/check_static_callback_thunk_ownership.py`.  Unique ownership alone was
not treated as an ABI contract.  Each of the 35 statically recovered gaps was joined
to its direct named CI caller, full pretty-function/template signature, ARM
register/stack setup, and QR39.6 target consumer.  Their contracts are in
`contracts/qr39/callbacks/static-contracts.csv`: all 35 have typed dormant seams after
exact concrete-consumer recovery closed the final three pointer contracts.
None is emitted.

Seven dispatches have direct named-method provenance and do not overlap the
61 runtime-observed rows.  They are recorded in
`contracts/qr39/callbacks/dispatch-additions.csv`. Scalar-only rows have a fixed AAPCS
word count. The GPS map-matched-address and Phone call rows now have exact
concrete receiver layouts, bounds, and typed dormant seams documented in
`docs/abi/pointer-callbacks.md`.

## Consequence

The callback catalogs now reconcile as 61 runtime-observed rows, seven earlier
direct static additions, and 35 recovered owned-thunk contracts.  Of the last
group, all 35 are typed but dormant. Exact consumers close the final three:
InterApplicationDataExchange 19/11 copies the counted transfer synchronously;
VoiceRecognition 25/14 bounds its semicolon list to 26 entries of 0x3ff bytes;
and VoiceRecognition 25/35 accepts 1..7 strings copied to 0x400-byte
destinations. Their implementation symbols and negative tests are guarded by
the manifest checker.

The 59-thunk dense region is now closed without adjacency inference: 56 have
positive CI ownership and contracts, and three are whole-image-unreferenced
orphans.

## Authoritative QR39.6 proxy surface

`evidence/qr39/static/pas-proxy-vtables.csv` is a fresh extraction of every
`PanaNav::IF_Server*Proxy` vtable in the QR39.6 executable.  There are 25
proxy interfaces and 249 true address-point entries: 50 C++ destructor slots
and 199 abstract application slots.  Every application entry in these base
interfaces targets `__cxa_pure_virtual`; that proves that a slot exists, but
does not by itself prove its AAPCS argument contract.

The earlier stop-on-non-code extraction reported 252 rows.  Three were not
vtable entries: RotaryKnob slot 4, Hardkey slot 4, and
InterApplicationDataExchange slot 12 all pointed to the same unrelated
`std::ios_base::Init::~Init` address in adjacent data.  The reproducible
`tools/ghidra/DumpServerProxyVtables.java` now requires the application tail to remain
on the proxy's contiguous pure-virtual target and asserts exactly 25/249.

`contracts/qr39/callbacks/proxy-slot-manifest.csv` joins this surface to the recovered
contract keys without treating provider callsite provenance as a prerequisite
for accounting. Its 199 application rows partition into 199 fixed typed
seams (80 currently emitted surface rows and 119 dormant rows), with no
fail-closed contract. No ABI-unrecovered slot remains
and no unknown slot was assigned a word stub. The five safe scalar
direct-static additions now have fixed dormant seams; the two unsafe
direct-static pointer rows still have none. The additional 14 keys are bounded
QR39.6/QR40 `SI_AudioHandling` concrete-consumer matches documented in
`docs/abi/cross-version.md`; they are dormant because no independent
provider callsite is yet tied to those slots.
The additional 63 keys come from the complete identical QR39.6/QR40 concrete
vtable audit: all 63 have fixed dormant seams.
None of those dormant seams is emitted by the provider. The pointer-contract
promotions and their exact extents are documented in
`docs/abi/pointer-callbacks.md`; the four by-value declarations are
documented in `docs/abi/aggregate-callbacks.md`.

Every one of the 1,740 exact syntactic chains outside that region is now in
`evidence/qr39/static/callback-outside-inventory.csv`.  Classification requires callback
object provenance rather than syntax:

- **9 callback-owned** callsites have an audited path in which the registered
  CI callback field supplies both the vtable and call receiver, one CI owner,
  and a slot inside that exact QR39.6 proxy bound.  Seven reconcile to
  `contracts/qr39/callbacks/dispatch-additions.csv` and two to the runtime matrix; this
  pass exposed no new safe contract.
- **498 excluded** chains have concrete negative evidence: 321 target ABI
  destructor slots 0--1, 59 exceed the maximum slot 35 of all 25
  callback proxies, four exceed their uniquely attributed CI proxy bound, and
  114 have a proven vtable/receiver mismatch.  One is independently hand- and
  machine-audited, 103 are recovered by the p-code pass, and ten more by the
  instruction-level ARM/Thumb pass described below.
- **1,233 unresolved** chains remain fail-closed: 1,221 have no callback-object
  provenance and 12 reach multiple CI classes through a shared caller.  They
  are not callback contracts and no callable seam is generated for them.

This correction is intentionally conservative: slot 2 is the abstract common
`adapterReady` application method on registered adapters, not a reserved slot.
The prior inventory incorrectly excluded 142 slot-2-shaped chains merely
because the base target was pure virtual.  Four have independent receiver
mismatch evidence; the other 138 now remain unresolved until ownership is
proved.

`tools/ghidra/RecoverCallbackReceiverDataflow.java` decompiles every one of the 1,799
raw exact chains, finds the `CALLIND`, extracts its actual `r0` receiver and the
object supplying the loaded vtable, and walks both definition/use graphs
through copies, casts, loads, constant field offsets, `PTRADD`/`PTRSUB`,
`MULTIEQUAL` nodes, and direct-call return roots.  It emits the complete
machine-readable `evidence/qr39/static/callback-receiver-dataflow.csv`.  A chain is excluded
only when the two recovered origin sets are nonempty and disjoint; shared
roots, cycles, or phi ambiguity remain unresolved.  This pass found 107 raw
disjoint-origin chains.  Seven were already excluded by stronger evidence,
leaving exactly 100 newly excluded outside-dense candidates.  It promoted no
callback: receiver equality is necessary but does not prove a registered CI
field or an `IF_Server*Proxy` type.

`tools/ghidra/RecoverArmCallbackReceiverSlices.java` independently slices actual
ARM/Thumb instructions around every chain.  It is guarded by the provider
SHA-256 and Ghidra image base `0x10000` (Ghidra addresses are file/ELF VMAs plus
`0x10000`).  Its CFG walk tracks machine registers through `MOV`, `LDR`, `ADD`,
`SUB`, stack stores/reloads, conditional predecessors and simple wrapper/tail
shapes; a local def/use proof handles loop-carried object values without
guessing their earlier contents.  It validates all nine known owned rows as
receiver-equal and `0x0028bd06` as disjoint.

The pass analyzes all 657 unresolved rows for which the decompiler exposed no
usable `CALLIND` receiver: 646 have machine receiver equality, ten have
disjoint receiver/vtable origins, and one call-clobbered pair remains ambiguous.
Equality is necessary but not callback
ownership, so the 646 remain fail-closed pending a registered CI callback-field
or proxy origin; the ten are safely excluded.  It also caught and corrected
the false `0x00386e64` mismatch noted above.  Across the remaining 1,233
unresolved chains, 1,142 now have instruction-proven receiver equality and 91
retain conditional/loop ambiguity.  The dominant remaining problem is concrete
object ownership, not slot syntax.  In particular, the 12 ambiguous callers
cannot be assigned between multiple CI reachability classes merely because
their local receivers match.

`tools/ghidra/RecoverCallbackFieldOrigins.java` takes those 1,142 equal-receiver rows
and performs a context-sensitive interprocedural pass. It preserves constant
object-field offsets while expanding decompiler `HighParam` values through the
actual arguments at each direct incoming callsite, follows
copy/cast/phi/load/direct-return definitions, and records indirect-entry data
memberships without interpreting them as ownership. A five-call-depth and
96-terminal cap are explicit in the script; a capped, cyclic, indirect, or
decompiler-missing path remains unresolved. The detailed evidence is
`evidence/qr39/static/callback-field-origins.csv`: 494 rows have interprocedurally expanded
field origins, one large-function row uses the independent 120-second local
p-code result after the shorter contextual decompile timed out, and 647 retain
only the independently proven machine receiver because no usable decompiler
receiver varnode exists at the callsite.

The address-insensitive structural summary in
`evidence/qr39/static/callback-field-origin-groups.csv` reduces the 1,142 rows to 132 repeated
origin shapes while retaining exact per-slot counts and an example callsite.
The largest group is the 647 machine-only rows. This pass makes **no callback
ownership promotion**: zero new registered-CI callbacks meet the required
whole-path proof. It does positively identify two slot-2 candidates as
provider-constructed non-callback objects; they are never turned into callback
contracts. During development, a weaker “store through this”
heuristic falsely identified a copy helper as a constructor; the final pass
rejects every stored value transitively derived from a parameter and the
checked artifact contains no such false promotion. This result localizes
the remaining work to indirect factories/vtable entries and the 647
machine-only receiver definitions without turning names, adjacency, or local
equality into ownership.

`evidence/qr39/static/callback-unresolved-next-actions.csv` groups those unresolved chains by
structural category, dataflow relation, and slot, with the exact next proof
required.  Across relation groups, slot 5 remains largest (327 chains), followed
by slot 2 (138) and slot 3 (134).  This makes the remaining work bounded and
prioritizable without turning absence of evidence into an exclusion.

The inventory is reproduced from the raw Ghidra outputs with:

```text
python3 -m tools.analysis.classify_callback_dispatches \
  /tmp/exact-callback-dispatches.csv \
  /tmp/attributed-callback-dispatches-v5.csv \
  evidence/qr39/static/callback-outside-inventory.csv \
  --receiver-dataflow evidence/qr39/static/callback-receiver-dataflow.csv \
  --arm-slices evidence/qr39/static/callback-arm-slices.csv \
  --next-actions evidence/qr39/static/callback-unresolved-next-actions.csv
```

The receiver dataflow input itself is reproduced against the same imported
stock program with:

```text
JAVA_HOME=/home/shish/tools/jdk \
  /home/shish/tools/ghidra_12.1.3_PUBLIC/support/analyzeHeadless \
  /tmp callback-stock -process libNaviCore.stock.so -noanalysis \
  -scriptPath /mnt/d/open_navicore/tools \
  -postScript RecoverCallbackReceiverDataflow.java \
  /tmp/exact-callback-dispatches.csv \
  /mnt/d/open_navicore/evidence/qr39/static/callback-receiver-dataflow.csv
```

The machine-level companion is reproduced in the same project with:

```text
JAVA_HOME=/home/shish/tools/jdk \
  /home/shish/tools/ghidra_12.1.3_PUBLIC/support/analyzeHeadless \
  /tmp callback-stock -process libNaviCore.stock.so -noanalysis \
  -scriptPath /mnt/d/open_navicore/tools \
  -postScript RecoverArmCallbackReceiverSlices.java \
  /tmp/exact-callback-dispatches.csv \
  /mnt/d/open_navicore/evidence/qr39/static/callback-arm-slices.csv
```

The interprocedural field-origin evidence and its repeated-shape summary are
reproduced with:

```text
JAVA_HOME=/home/shish/tools/jdk \
  /home/shish/tools/ghidra_12.1.3_PUBLIC/support/analyzeHeadless \
  /tmp callback-stock -process libNaviCore.stock.so -noanalysis \
  -scriptPath /mnt/d/open_navicore/tools \
  -postScript RecoverCallbackFieldOrigins.java \
  /mnt/d/open_navicore/evidence/qr39/static/callback-outside-inventory.csv \
  /mnt/d/open_navicore/evidence/qr39/static/callback-arm-slices.csv \
  /mnt/d/open_navicore/evidence/qr39/static/callback-receiver-dataflow.csv \
  /mnt/d/open_navicore/evidence/qr39/static/callback-field-origins.csv

python3 -m tools.analysis.summarize_callback_field_origins \
  evidence/qr39/static/callback-field-origins.csv \
  evidence/qr39/static/callback-field-origin-groups.csv
```

The two raw inputs come from `FindExactCallbackDispatches.java` and
`AttributeCallbackDispatches.java` against the stock binary identified above.
`tests/check_static_callback_outside_inventory.py` independently fixes the raw
candidate-key digest, the complete 1,799-row p-code and ARM-slice key digests
and relation totals, the targeted 657-row split, all classification totals,
the nine receiver-proven callsites,
contract-catalog reconciliation, exclusion invariants, unresolved work-group
reconciliation, and nine negative mutations (including forged p-code and ARM
relations).  Provider-wide
callback closure is still not claimed: each unresolved receiver definition-use
chain must be traced to its constructor/field, and any surviving callback then
needs an exact ABI and pointer-consumption proof before emission.
`tests/check_static_callback_field_origins.py` independently fixes the exact
1,142-key and status/group digests, reconciles the target set to the outside
inventory plus ARM evidence, regenerates all 132 groups, forbids invented
callback ownership, and runs five negative mutations.
