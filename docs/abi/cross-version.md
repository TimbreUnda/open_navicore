# Cross-version PAS callback ABI recovery

## Bounded inputs

Only the three explicitly selected update archives were listed and decrypted:

- `UCONNECT_12.1_UCX_PR40.1_DT,D2,DD,DP,DJ_MY19-21.zip`
- `UCONNECT_UAQ_8.4_QR40.1_MY18-21.zip`
- `UCONNECT_UCQ_8.4_QR40.1_MY18-21.zip`

| Image | SHA-256 | Size / identity |
|---|---|---|
| UCX `libNaviCore.so` | `80e15eb7a173d9ab82a5328a3c02fde9012968c7498d7f3a55267848fbef8de7` | 19,361,484 bytes; 9.35.2.239823; Build ID begins `2be1c899` |
| UAQ/UCQ `libNaviCore.so` | `fd1e931430cca61116c533cdf617dc805548e312a9131e972f55a7d30ab64842` | 19,935,860 bytes; 9.35.2.241410; byte-identical to QR39.6 baseline |
| UAQ/UCQ/UCX `nv_navigation` | `4f340be48b64fa0009ee93b1cd6867f7b757eda5fef849cfe2eb200ac26e34db` | all three QR40 images are identical |
| QR39.6 `nv_navigation` | `8c8b5010d3db9062f2c970a8c932d707615eb7c40e1d4e9ad8f8eb9060b796ae` | comparison baseline |

The selected navigation configs are byte-identical (SHA-256 begins
`c889c2b4`) and retain `NaviDllName=/navi/libNaviCore.so`.

## Concrete Audio receiver alignment

QR39.6 and QR40 place the concrete `SI_AudioHandling::vtable` at
`0x081f06f0`. Its first 26 words (metadata plus receiver entries) are
byte-for-byte identical. Code range `0x0812d320..0x081301a8` is also identical:
11,912 bytes, SHA-256
`c5e6336e6716fcc822dc246491a4ca7af722e06b06ca763c6c309808eaad0941`.
The broader `0x0812d320..0x08130b00` range is 14,304 identical bytes, SHA-256
`1cbd4d296f7eef9f1b2569d0901d51ec3e3a5de232d05dbd08f3bd5da1b4a6be`.

Concrete vtable index equals proxy application slot plus two. The exact
machine evidence is in `evidence/qr39/cross-version/callback-contracts.csv`:

| Slot | Target | Receiver method | Scalar words after `self` |
|---:|---:|---|---:|
| 4 | `0812e978` | `requestAudioOutSourceDescriptor` | 2 |
| 5 | `0812e458` | `requestAudioInSourceDescriptor` | 2 |
| 6 | `0812fee8` | `requestAudioSourceAllocation` | 2 |
| 7 | `0812e868` | `requestAudioSourcePreparation` | 2 |
| 8 | `0812db5c` | `signalDeallocateAudioSource` | 1 |
| 9 | `0812d8e4` | `replyAudioSourceDeactivation` | 3 |
| 11 | `0812e3d4` | `requestAudioSourceStatus` | 2 |
| 12 | `0812e06c` | `signalEnableServerAudioSource` | 1 |
| 13 | `0812dfec` | `signalDisableServerAudioSource` | 1 |
| 14 | `0812e350` | `requestAudioSourceEnableStatus` | 2 |
| 15 | `0812e2cc` | `signalAudioSourceComfortVolume` | 2 |
| 16 | `0812e24c` | `signalEnableAudioSourceVolumeSeparation` | 1 |
| 17 | `0812e148` | `signalDisableAudioSourceVolumeSeparation` | 1 |
| 18 | `0812e1c8` | `requestAudioSourceVolumeSeparationStatus` | 2 |

Slots 5 and 11–18 have exact receiver declaration strings. Slots 4 and 6–9
are additionally grounded in machine operands: scalar register words are
copied to fixed PAS messages, logged, or used in control flow. No body
dereferences an argument or exhibits hidden-sret, aligned 64-bit, or by-value
aggregate lowering. This supports fixed outbound C seams but does not prove
that stock NaviCore invokes them. All 14 are therefore typed, tested, and
dormant (`emitted=no`). The compiler oracle was not used to invent a missing
producer side.

## Rejected similarity and next probe

UCX exposes readable `PanaNav::CI_AudioHandling::*` strings, but their machine
bodies are provider-facing wrappers. For example, UCX
`replyAudioSourceVolumeChange(handle, source, volume)` invokes its own virtual
at byte offset `0x4c` and drops `handle`; it is not an
`IF_ServerAudioHandlingProxy` callback consumer. Those strings did not qualify
any slot.

## Complete concrete-vtable audit

The follow-up pass located all 25 concrete `SI_*` implementation vtables in
both Ghidra projects. For every one of the 63 formerly ABI-unrecovered rows, abstract proxy
slot *N* aligns with concrete vtable word *N+2*. All 63 QR39.6 targets equal
their same-slot QR40 targets. More importantly, bounded
`ExportSelectedContracts` output proves that all 63 complete instruction
listings are identical after the provenance line; this is not a name/order
match.

[`evidence/qr39/cross-version/concrete-vtable-audit.csv`](../../evidence/qr39/cross-version/concrete-vtable-audit.csv)
records the concrete table, target, recovered method name, incoming ABI width,
consumer access, and ownership/layout class for each row. Its independent
checker fixes the 63-row identity digest, reconciles it exactly to the
integrated production keys, requires same-target/same-body evidence, checks the
63/0/0 partition, and runs negative mutations.

The evidence partition is 63 fixed typed-capable dormant contracts. A bounded
machine-level follow-up recovered exact signatures, input extents, synchronous
lifetime, and AAPCS alignment for all eight formerly fail-closed pointer rows;
[`pointer-callbacks.md`](pointer-callbacks.md)
records those proofs and the five related static rows. The typed set
includes bounded synchronous pointer inputs whose consumers prove their
extent: 10-byte EWS area, 0x30-byte TPEG station, 0x180-byte VICS alert,
8-byte CAN payload, 0x38-byte map-match storage, and 0x22c-byte timezone
storage.

Production integration preserves the prior 14 Audio contracts and adds fixed
dormant seams for all 63 rows. Fixed-extent pointer seams copy the exact
proven extent synchronously (including the count-bounded RDS and TPEG lists),
and the one opaque pointer row forwards only during the call and never retains
or dereferences it. Host smoke covers each newly typed pointer ABI, including
the aligned 64-bit transfers and rejected invalid extents. The four by-value
aggregate rows are independently confirmed by exact message-builder copies
and the QNX compiler oracle in `docs/abi/aggregate-callbacks.md`. The
resulting 199-slot application surface contains 199 typed, zero fail-closed,
and zero ABI-unrecovered rows.
