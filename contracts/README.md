# Compatibility contracts

This directory contains the machine-readable interface that Open NaviCore
commits to preserving for the analyzed firmware family.

- `qr39/provider/` describes calls from `nv_navigation` into `libNaviCore.so`.
- `qr39/callbacks/` describes calls from Open NaviCore into stock PAS proxies.
- `qr39/observations/` contains sanitized runtime inputs used alongside static
  and independent expected catalogs.

Contracts record ABI and ownership facts separately from behavior. A typed
dormant callback is callable without claiming that its optional stock feature
has been implemented by the current backend.
