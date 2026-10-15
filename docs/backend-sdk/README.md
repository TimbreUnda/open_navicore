# Backend SDK

The backend SDK is the stable C boundary between Open NaviCore's stock-facing
provider and a navigation engine such as the reference renderer or the optional
[OsmAnd map backend](../../backends/osmand/README.md).

New code should include:

```c
#include <open_navicore/backend.h>
#include <open_navicore/backend_plugin.h>
#include <open_navicore/cluster.h>
```

The historical `open_navi_*.h` paths remain forwarding headers for source
compatibility. Existing C type names, query/install symbols, structure layouts,
version values, and plugin discovery strings are unchanged. The new include
layout is an organizational boundary, not a new binary ABI.

Backend implementations belong under `backends/` and must not expose C++ STL,
Qt, OsmAnd, or compiler-specific ownership types across this interface. A C++
backend owns those objects internally and implements this C contract at its
edge.

The C boundary does not isolate C++ runtimes in the process. The OsmAnd plugin,
core, Qt, compiler runtime and assets need qualification as one binary set.
Query-V1 rendering has no pending/no-swap result: zero swaps, and a nonzero
plugin render status makes the loader select its fallback for that lifecycle.
Validate payload version/size before accessing fields and contain all C++
exceptions inside every C entry point, including cleanup and query callbacks.

The reference backend in `backends/reference/` remains the small conformance
example. Deliberately invalid plugins live under
`tests/fixtures/plugins/`; they are not example implementations.
