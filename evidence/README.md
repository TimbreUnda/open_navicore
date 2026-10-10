# Evidence index

Open NaviCore keeps observations, reviewed contracts, generated summaries, and
unresolved analysis distinct. Moving a file into this tree does not promote its
certainty or turn an inference into a supported semantic claim.

| Area | Classification | Primary consumers |
|---|---|---|
| `qr39/static/` | Static machine/decompiler inventories and reviewer classifications | Contract consistency checks and follow-up analysis |
| `qr39/cross-version/` | QR39.6/QR40 comparisons of callback contracts and concrete vtables | Cross-version and callback integration checks |
| `qr39/oracles/` | Checked QNX compiler-oracle outputs | ARM/QNX ABI verification |

The authoritative callable boundary is under `contracts/qr39/`. Evidence files
support those contracts but do not replace the independent expected catalogs in
the verification code.

## Reproduction policy

- Text outputs are UTF-8/LF, deterministically ordered where order is not part
  of the interface, and reviewed in ordinary Git diffs.
- Generator output is written to a build directory and compared with committed
  output before it replaces an artifact.
- Firmware binaries, QNX SDK files, device keys, personal/location data, and
  unsanitized live logs are never committed.
- Historical firmware addresses and vendor symbol names remain exact evidence;
  friendly names are stored separately.
- A public host check validates checked evidence. QNX compiler-oracle
  regeneration and original-code replay are reported as separate tiers.
