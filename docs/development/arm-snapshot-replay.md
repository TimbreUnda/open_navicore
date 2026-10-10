# Bounded ARM snapshot replay oracle

`tools/replay/arm_snapshot_replay.py` executes a small original ARM/Thumb fragment
from a SHA-256-guarded ELF under Unicorn. It is an ABI experiment harness, not
a QNX process emulator. Snapshot schema `mock-nav-arm-snapshot/v1` records:

- the exact binary hash, ELF ARM machine, image base, and load bias;
- entry ISA, stop PC, instruction count, and wall-clock budgets;
- every core-register and stack lane used by the experiment;
- named, page-bounded allocations whose pointer fixups can be relocated;
- deterministic external-call stubs, including captured `r0`--`r3` and stack
  words plus a selectable counterfactual return value.

The runner maps all ELF `PT_LOAD` code/data pages, maps only the declared
synthetic graph pages, and traces instructions and data reads/writes. An
unmapped/protected access is a reported fault. It never supplies QNX syscalls,
the dynamic loader, threads, or implicit heap objects; a fragment that needs
one must declare a deterministic stub or a captured memory allocation.

## First executable oracle

`fixtures/arm-replay/hmi-signal-state-v1.json` replays the untouched stock
`libNaviCore.so` Thumb thunk at ELF VMA `0x00288138` (Ghidra
`0x00298138`). Static ownership ties it to `CI_HMIIntegration` callback slot 8,
`signalHMIStateChange`. The eight-instruction thunk loads the registered PAS
proxy from wrapper offset `+4`, dispatches through vtable offset `+0x20`, and
returns the callback status.

Run the baseline:

```sh
python3 tools/replay/arm_snapshot_replay.py \
  fixtures/arm-replay/hmi-signal-state-v1.json
```

Run a falsifiable counterfactual:

```sh
python3 tools/replay/arm_snapshot_replay.py \
  fixtures/arm-replay/hmi-signal-state-v1.json \
  --set-reg r1=0x01020304 --set-reg r2=0xfedcba98 \
  --stub-return hmi_slot8=0x13579bdf
```

The captured call changes in exactly the `r1` and `r2` lanes, and the thunk
returns exactly `0x13579bdf`. This independently falsifies a one-argument or
stack-only interpretation: for this binary, callback self is `r0`, display and
visibility are passed unchanged in `r1`/`r2`, and the 32-bit status is returned
in `r0`. Relocating `hmi` also proves the thunk follows pointers rather than
depending on the fixture addresses:

```sh
python3 tools/replay/arm_snapshot_replay.py \
  fixtures/arm-replay/hmi-signal-state-v1.json --relocate hmi=0x00100000
```

The fixture is constructible from the static contract rather than a whole
process capture. Future live snapshots can use the same schema after copying
only the pages transitively reached by a call. Pointer extents and ownership
must be established with guard pages, relocation, and post-return experiments;
successful replay alone does not prove them.

Regression coverage:

```sh
make arm-snapshot-replay-selftest
```

It checks the baseline memory trace, lane and return counterfactuals, graph
relocation, null-proxy branch, unmapped fault, instruction budget, and binary
hash rejection.
