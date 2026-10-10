# Source layout

Open NaviCore keeps the stock-facing compatibility layer separate from the
backend API and renderer implementation:

- `compat/qr39/` implements the ABI exported to the analyzed QR39 navigation
  host and constructs its callback proxies.
- `runtime/` contains provider state and the typed dispatch paths shared by
  compatibility code and backends.
- `backend/` discovers a backend plugin and supplies the built-in fallback.
- `renderer/` owns platform-neutral render and screen-input coordination.
- `platform/` contains the host and QNX renderer adapters.
- `capture/` is the standalone raw-callback evidence probe; generated sources
  are isolated below `capture/generated/`.

Dependencies should point inward from `compat/` and `platform/` toward the
runtime/backend interfaces. Backend plugins use the public headers under
`include/open_navicore/` and do not include private files from this tree.
