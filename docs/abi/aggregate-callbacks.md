# PAS by-value aggregate callback recovery

## Scope and evidence boundary

This closes the four callbacks formerly classified as
`fail_closed_aggregate_contract`. The radio was not used. Each declaration
requires three independent facts: the complete QR39.6 concrete receiver and
message-builder machine code, its byte-identical QR40 receiver, and the QNX
6.6 ARM compiler lowering of the recovered C declaration. Pretty-function
strings supply semantic names but are not ABI evidence by themselves.

The concrete QR39.6 targets are `0x080f9efc`, `0x081331a0`, `0x08134724`,
and `0x08138204`. Their downstream builders are `0x080f7eb4`, `0x0813464c`,
and `0x08138134`; the collection receiver at `0x081331a0` performs the same
48-byte copy inline. The existing cross-version audit proves identical
complete QR39.6/QR40 listings.

## Recovered declarations

| Adapter/slot | Declaration after `self` | ARM/QNX placement |
|---|---|---|
| 31/12 | `UINT32 handle, SxmNaviLocationInfo location, UINT32 radius, SxmRadiusUnitType unit` | handle in `r1`; the 16-byte/two-`f64` location begins in `r2/r3` and finishes in stack words 0–1; radius and unit occupy stack words 2–3; seven ABI words total |
| 30/4 | `UINT32 handle, UINT16 type, SxmMbr mbr, UINT32 collection_index` | handle `r1`, type `r2`, alignment pad `r3`; the 48-byte/six-`f64` MBR starts at stack+0 and index at stack+0x30; 16 ABI words including the pad |
| 30/5 | same layout as 30/4 | same 16-word placement; this corrects the old 13-word lower bound, which covered only values used by logging |
| 29/4 | `UINT32 handle, SxmMbr mbr, UINT16 collection_index` | handle `r1`; MBR begins in `r2/r3` and continues through stack+0x27; index is read at stack+0x28; 14 ABI words total |

`MockPasSxmLocation` is exactly 16 bytes and contains longitude then latitude.
`MockPasSxmMbr` is exactly 48 bytes, eight-byte aligned, and contains
lower-left, upper-right, and center locations in that order. The Apogee log
string independently names all six values. Every receiver copies the input
synchronously into its PAS message; there is no pointer retention or output
storage.

The stock producer also preserves the exact Parking source declaration as a
pretty-function string at file/virtual offset `0x0116fbb8`:
`callRequestParkingSetCollectionMBR(const PanaNav::MBR_SXM&,
PanaNav::UINT16)`. This is independent producer-side evidence for both the MBR
semantic type and the trailing 16-bit collection index. It comes from stock
`libNaviCore.so` SHA-256
`fd1e931430cca61116c533cdf617dc805548e312a9131e972f55a7d30ab64842`.
It is not used as machine-code ABI evidence: `0x0116fbb8` lies in `.rodata`,
and an automatic Ghidra function/xref at that address is a false disassembly
of the string bytes. Placement remains established by the concrete receiver
and the independent QNX compiler oracle.

## Compiler oracle

[`tools/abi/qnx_aggregate_abi_oracle.py`](../../tools/abi/qnx_aggregate_abi_oracle.py)
compiles the four exact caller/callee pairs with QNX 6.6 ARM using the same
flags as the general ABI oracle. Compile-time assertions enforce the 16-byte
location, 48-byte MBR, and eight-byte MBR alignment. Normalized caller and
callee fingerprints are checked into
[`evidence/qr39/oracles/aggregate-callback-abi.csv`](../../evidence/qr39/oracles/aggregate-callback-abi.csv).
`--verify` recompiles and rejects stale output. The compiler evidence confirms
placement and padding; the stock message builders establish semantic field
extent and synchronous ownership.

All four production seams remain dormant (`emitted=no`). They are callable
only when a future backend has meaningful SXM weather/traffic/parking state.
