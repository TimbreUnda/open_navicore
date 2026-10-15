# Validation tiers

Open NaviCore separates portable checks from verification that requires
licensed tools, proprietary inputs, or physical hardware. A missing higher tier
must be reported as missing; a host-only pass is never presented as target ABI
qualification.

| Command | Requirements | What it establishes |
|---|---|---|
| `make test` | Linux, CMake, C99/C++17 compiler, Python 3 | Host behavior, lifecycle, plugin failures, backend logic and UI, and the contract checks (contracts vs sources, symbol names, portable paths) |
| `make` | QNX SDP 6.6 SDK and the OsmAnd QNX build (GCC 8.5) | Target compilation and, after each link: provider exports, SONAME and Thumb bits, the ARM argument passing of recovered entry points and callback wrappers, the plugins' ELF and dependency profiles |
| `make ifs` | the above plus ifs-build and the factory `nv_navigation.cfg` | The `navigation.ifs` image |
| bench | Authorized stationary bench unit | Operator-controlled loader, HMI, data, rendering, and lifecycle observations (skill `deploy-open-navicore`) |

The research repository `open_navicore-re` adds the evidence tiers: the
evidence-vs-contract checks and manifest regeneration (`make`), QNX compiler
oracles and the capture probe (`make qnx`), and the stock-code snapshot replay
(`make replay`), each run against a checkout of this repository.

Public pull-request CI runs `make test`. Maintainers record QNX, research and
bench results separately when a change affects those boundaries.

The three runtime-observation matrices used by the contract checks are
committed at `contracts/qr39/observations/`. They are sanitized textual
inventories, not firmware binaries. Their provenance and hashes are documented
in `contracts/qr39/observations/README.md`.
