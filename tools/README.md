# Developer tools

These utilities recover, generate, or verify compatibility evidence; they are
not linked into `libNaviCore.so`.

- `ghidra/` contains headless analysis scripts for the stock binaries.
- `analysis/` classifies and summarizes exported Ghidra data.
- `generators/` produces checked-in ABI manifests and callback stubs.
- `abi/` builds compiler oracles for the QNX ARM calling convention.
- `replay/` converts captures and executes bounded stock-code snapshots.
- `qnx/` contains manually invoked service-control programs for the bench unit.

Run Python tools from the repository root so their documented relative input
and output paths remain stable.
