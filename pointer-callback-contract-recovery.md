# Pointer-bearing PAS callback contract recovery

## Scope and evidence boundary

This audit closes the 13 application slots formerly classified
`fail_closed_pointer_contract`. It uses exact ARM/Thumb consumer instructions,
concrete SI vtable alignment, declaration strings, and provider wrappers; names
or decompiler types alone were not promotion evidence. The radio was not used.

The bounded binaries are QR39.6 `nv_navigation` SHA-256
`8c8b5010d3db9062f2c970a8c932d707615eb7c40e1d4e9ad8f8eb9060b796ae`,
QR40 `nv_navigation` SHA-256
`4f340be48b64fa0009ee93b1cd6867f7b757eda5fef849cfe2eb200ac26e34db`,
and the stock QR39.6/QR40-compatible `libNaviCore.so` SHA-256
`fd1e931430cca61116c533cdf617dc805548e312a9131e972f55a7d30ab64842`.
The exact vtable/target facts are retained in
`cross-version-concrete-vtable-audit.csv`,
`static-callback-dispatch-additions.csv`, and
`static-callback-contract-manifest.csv`.

## Proven contracts

| Adapter/slot | Exact concrete consumer | ABI and pointer contract |
|---|---:|---|
| 24/3 | `0x080de278` | `handle, source, text_type, CSTR8`; 1..1027 bytes, synchronously copied into a 0x404-byte message |
| 32/4 | `0x080df99c` | two borrowed 10-byte areas; both read synchronously and the second sent as exactly 10 bytes |
| 20/7 | `0x080e3f90` | `handle, service, CSTR16 url, method, request_size/request, response_size/response`; counted buffers copied before return |
| root/6 | `0x081132f8` | borrowed NUL-terminated CSTR16 path, converted and consumed before return |
| 21/4 | `0x08119b40` | `handle, CSTR16 url, method, request_size/request, response_size/response`; seven ABI words after `self` (four stack words in the entry prologue) |
| 19/8 | `0x08121d0c` | `handle, object_id, type, CSTR16, UINT64 size, reply_type`; eight ABI words including AAPCS alignment padding |
| 19/9 | `0x0812171c` | `handle, object_id, UINT64 size, bytes, UINT64 total, boolean`; ten ABI words, exact-size synchronous copy, consumer capacity 0xfc0 |
| 13/10 | `0x0813cc78` | `handle, CSTR16 versions`; synchronous conversion capped at 0x4d9 bytes in a 0x4e0-byte message |
| 9/6 | `0x0810c6cc` | `handle, const 0x50-byte ARM record *`; six optional CSTR16 pointers followed by scalar reads through offset 0x4e |
| 26/4 | `0x081320a4` | `handle, phone CSTR16, display CSTR16`; both nonempty, maxima 511 and 20 code units |
| 19/11 | `0x0811f05c` | same aligned ten-word transfer ABI as 19/9; exact-size synchronous copy, no caller-pointer retention |
| 25/14 | `0x080f5afc` | `handle, result, CSTR8 language-list, file_type, entries`; at most 26 semicolon tokens, each copied to a 0x400-byte destination |
| 25/35 | `0x080e8cb0` | `handle, result, count, uint32 types *, CSTR8 * array`; count 1..7 and each string copied to a 0x400-byte destination |

The first eight rows have identical QR39.6/QR40 concrete SI targets and bodies.
They are consumer-reachable in the stock SI surface, but no independently
owned provider callsite has been tied to them, so actual stock invocation is
not claimed. The last five also have concrete stock provider producer paths in
the two static manifests; that proves a callable stock path, not that a given
bench session triggers it. Every replacement seam remains `emitted=no`.

All pointers are input-only and borrowed for the duration of the call. Fixed
records are copied by the wrapper where an exact extent is proven. Counted and
string inputs are synchronously consumed by the concrete receiver; no receiver
store-through or post-return retention was found. The wrappers reject missing
buffers for nonzero counts and enforce each proven finite bound.

## Exclusions and correction

The old 21/4 eight-word annotation was rejected by the entry prologue: it reads
`r1`..`r3` and four stack words, hence seven words after `self`. Older static
addresses attached to 19/11, 25/14, and 25/35 remain producer-provenance
addresses only; exact Thumb disassembly showed that some labels are interior
addresses and they are not cited as consumer function entries. No pointer row
was promoted from a compiler-oracle equivalence or declaration string alone.

The four remaining fail-closed application rows are a different evidence
class: unresolved by-value aggregate layouts. There are no remaining
pointer-contract or ABI-unrecovered rows.
