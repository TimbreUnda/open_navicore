# Validation tiers

Open NaviCore separates portable checks from verification that requires
licensed tools, proprietary inputs, or physical hardware. A missing higher tier
must be reported as missing; a host-only pass is never presented as target ABI
qualification.

| Command | Requirements | What it establishes |
|---|---|---|
| `make test-host` | Linux C toolchain and Python 3 | Host behavior, lifecycle, plugin failures, checked contracts, and deterministic generators |
| `make check-contracts` | Python 3 | Consistency of committed contracts, evidence indexes, generated tables, ownership rules, and negative mutations |
| `make verify-qnx` | QNX SDP 6.6 ARMv7 plus target Screen/EGL/GLES libraries | Target compilation, exports, SONAMEs, ARM calling conventions, and compiler oracles |
| `make replay-stock` | Locally supplied stock `libNaviCore.so`, Unicorn, and Capstone | Bounded execution of the identified original-code fragment |
| `make verify-bench` | Authorized stationary bench unit | Operator-controlled loader, HMI, data, rendering, and lifecycle observations |
| `make verify` | QNX and stock-replay requirements | The historical combined offline verification |

`make test` remains an alias for `make test-host`. Public pull-request CI runs
only the self-contained tier. Maintainers record QNX, replay, and bench results
separately when a change affects those boundaries.

The three runtime-observation matrices used by contract checks are committed at
`contracts/qr39/observations/`. They are sanitized textual inventories, not
firmware binaries. Their provenance and hashes are documented in
`contracts/qr39/observations/README.md`.
