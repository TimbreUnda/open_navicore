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
- `include/` holds the provider's private headers (the public plugin ABI is
  `include/open_navicore/` at the repository root).

The raw-callback capture probe that used to live here is in the research
repository `open_navicore-re`.

Dependencies should point inward from `compat/` and `platform/` toward the
runtime/backend interfaces. Backend plugins use the public headers under
`include/open_navicore/` and do not include private files from this tree.
