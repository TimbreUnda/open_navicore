# QNX ARM compiler oracle and bounded declaration permuter

## Purpose and boundary

This pipeline asks the installed QNX 6.6 compiler how candidate declarations
and call wrappers are lowered for the radio's ARM target. It establishes ABI
equivalence classes; it does **not** reconstruct an original C++ declaration,
name, ownership rule, or semantic meaning. In particular, a 32-bit integer,
pointer, reference, enum, and soft-float value can be register-equivalent at a
call boundary. Pointer extent and ownership require separate consumer evidence.

No reviewed local `decomp-permuter` installation was available. Rather than
install unreviewed software, the
repository contains a bounded deterministic declaration/wrapper permuter in
[`tools/abi/qnx_arm_abi_oracle.py`](../../tools/abi/qnx_arm_abi_oracle.py).

## Reproduction

The exact compiler flags recorded in every catalog row are:

```text
-Vgcc_ntoarmv7le -O2 -fno-inline -fno-ipa-sra -fno-optimize-sibling-calls -ffunction-sections
```

The script additionally uses `-std=c99` for C and `-lang-c++` for C++. Run:

```sh
python3 -m tools.abi.qnx_arm_abi_oracle
make qnx-arm-abi-oracle-selftest
```

The generated catalog has paired caller/callee examples for:

- zero through ten 32-bit words after `self`;
- both relevant placements of an aligned 64-bit argument;
- float and double under the toolchain's ARM soft-float convention;
- one- through five-word POD aggregates passed by value;
- one- through five-word aggregate returns, including hidden-sret lowering;
- pointer, function-pointer, C++ reference, member-pointer, and virtual-call
  forms.

Disassembly is normalized into an instruction/dataflow string and SHA-256
fingerprint. Immediate addresses and relocation destinations are erased, while
register identities, stack accesses, load/store widths, and branch forms are
retained. Caller fingerprints are bounded to the 20 instructions immediately
preceding the selected `bl`/`blx`, the call itself, and its relocation. Normalize
an independently extracted objdump function/call window with:

```sh
python3 -m tools.abi.qnx_arm_abi_oracle \
  --normalize-objdump target.objdump --function FUNCTION --call-index 0
```

The bounded matcher scores each supplied producer and consumer
against every caller permutation and records both the winning and runner-up
scores. A candidate requires a unique 100% match on both independent sides,
agreement on one family, and a family with all hidden-return, 64-bit,
by-value-aggregate, and pointer/reference hazards excluded.

The evidence input is
[`evidence/qr39/oracles/qnx-arm-evidence.csv`](../../evidence/qr39/oracles/qnx-arm-evidence.csv). Each row
is keyed by adapter and slot and carries independently normalized producer and
consumer call-setup instruction strings plus provenance. Evidence must refer
to the exact callback object/slot; an unattributed object+vtable call is not a
producer. A pure-virtual base-vtable entry is not a concrete consumer.

## Current result

[`evidence/qr39/oracles/qnx-arm-catalog.csv`](../../evidence/qr39/oracles/qnx-arm-catalog.csv) contains 60
compiled functions in 30 paired families. The historical 63-row ABI-gap set
continues to be assessed after its independent QR39.6/QR40 concrete-consumer
audit was integrated. The empty evidence file remains deliberate: none of
these rows has an independently owned provider callsite on both sides of the
compiler matcher, so the oracle did not promote any row by itself.
[`evidence/qr39/oracles/qnx-arm-assessment.csv`](../../evidence/qr39/oracles/qnx-arm-assessment.csv)
therefore records **0 compiler-oracle promotions**. Identical concrete
consumer bodies plus a separate bounded pointer-contract audit establish
fixed ABI prototypes for 59 dormant seams. The final four by-value declarations
are compiled and fingerprinted by the exact-purpose aggregate oracle described
in `docs/abi/aggregate-callbacks.md`; the general declaration permuter
itself still promoted zero rows. The production manifest has zero
`fail_closed_unrecovered_abi` rows without attributing that result to the
compiler oracle. The existing fourteen AudioHandling seams remain a separate,
unchanged concrete-consumer catalog.

This is a useful negative result: compiler permutation cannot recover argument
count from an abstract `__cxa_pure_virtual` entry. The next admissible input is
a producer callsite whose callback-field ownership is proven plus a concrete
receiver/wrapper in another PAS build or implementation. At that point the
same command deterministically ranks the declarations. Even a unique safe
match is labelled `abi_equivalent_candidate`; promotion still requires a
separate pointer/ownership and semantic audit.

## Guards

`tests/check_qnx_arm_abi_oracle.py` independently checks the 60-function
family partition, 0–10-word coverage, paired roles, flags, fingerprints, and
the exact historical 63-slot assessment surface selected by concrete-audit
provenance. Its negative mutations reject a missing
family, altered fingerprint, altered flags, missing slot, evidence-free
promotion, and promotion retaining a hazard. The generator also runs matcher
self-tests for a safe exact match, an exact 64-bit hazardous match, and a
producer/consumer disagreement. `--verify` recompiles everything and rejects
stale checked-in artifacts.
