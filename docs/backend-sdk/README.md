# Backend SDK

The backend SDK is the stable C boundary between Open NaviCore's stock-facing
provider and a navigation engine such as the reference renderer or a future
OsmAnd integration.

New code should include:

```c
#include <open_navicore/backend.h>
#include <open_navicore/backend_plugin.h>
#include <open_navicore/cluster.h>
```

The historical `mock_*.h` paths remain forwarding headers for source
compatibility. Existing C type names, query/install symbols, structure layouts,
version values, and plugin discovery strings are unchanged. The new include
layout is an organizational boundary, not a new binary ABI.

Backend implementations belong under `backends/` and must not expose C++ STL,
Qt, OsmAnd, or compiler-specific ownership types across this interface. A C++
backend owns those objects internally and implements this C contract at its
edge.

The reference backend in `backends/reference/` is both the current bench
renderer and the conformance example. Deliberately invalid plugins live under
`tests/fixtures/plugins/`; they are not example implementations.
