# QR39 runtime observation inputs

These CSV files are the reviewed inputs used by Open NaviCore's contract
checkers. They were captured from the QR39.6 `nv_navigation` integration trace
identified as `stage1-v11-20261008-attempt1` and were previously referenced
through a workstation-local `/mnt/d/analysis` path.

| File | SHA-256 | Role |
|---|---|---|
| `callback-api-matrix.csv` | `d59f4fe3945d33658a72a684dff1b447f940aa406aef2d06a79ec884acdb3d44` | Observed PAS callback surface |
| `provider-api-matrix.csv` | `7f5cdf732947395e9f81423830d5d0d31aa36a3e3e012ee0452bb7f2cdb09889` | Observed provider-call surface |
| `pointer-api-matrix.csv` | `3b313b03233bc377233e35db96678ee2b811dd87cfd517cb1d9640f0c347efc0` | Reviewed pointer extents and ownership |

These observations do not replace the independently maintained reachable-slot
catalog or prove calls that were not observed. Contract checks deliberately
combine them with static evidence and independent expected inventories.
