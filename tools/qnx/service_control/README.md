# System Manager test-control probe/start helper

`sm_reload_probe` has two narrowly scoped modes. `probe` (also the no-argument
default) is non-mutating. `start nv_navigation` sends exactly one generated
AppStart request for that fixed System Manager service name. It requires
immediate enqueue status zero and a matching asynchronous response with
transport status zero, operation status zero, and service name
`nv_navigation`. Its overall deadline is 30 seconds.

The request is the recovered 0x108-byte `_SS_ServiceStartInfo`; both trailing
fields are zero. A unique transaction ID is generated from PID and wall time
and printed. There is no stop, launch, terminate, direct-exec, or
arbitrary-service mode.

The proven generated AppStartResponse contract is vptr slot 9 with signature
`(this, ePasStatus, _SS_StartResponseInfo&)`. The helper copies the genuine
vtable, replaces only that slot, then restores the genuine vtable before the
vendor destructor runs. In the selected QR39.6 unified library's dynamic
relocations the vtable address point is `0xa49f8` and AppStartResponse is at
`0xa4a1c`, exactly `0x24` bytes (nine 32-bit slots) later. The inherited
SessionCreationResponse entry is at `0xa4a00`, object slot 2.

The corrected helper loads the QR39.6 factory aggregate
`libss_service_if_unified.so` and constructs TestControl instance 1, matching
the recovered factory generated-session convention. Instance 0 is not used:
the earlier bench build using it never became active. SessionCreationResponse
(vptr slot 2) is chained through the genuine implementation so its normal
state transition is preserved while its target, status, and transaction are
reported. Constructor return, request/response registration, callback
registration, and activation are logged separately.

The PAS client identity is exactly `ss_service_lifecycle_test_client`. Both
QR39.5 and QR39.6 package the same `ns_mq.cfg` (SHA-256
`6bc343da57552df88c509b01d839524eb5b119a610b19a9bb8f6d84f2a71e586`),
which registers this reserved diagnostic client with depth 64. The name is
absent from both generations' `sm_config_launch.cfg`, has no packaged binary,
and is not a worker/thread queue name. The live preflight additionally found
its configured `/dev/mq` queue but no matching process or open descriptor.

Do not substitute a supervised service identity such as `ss_plm`; the real
`ss_plm` process is running. Neither `sm_reload_probe` nor
`ss_plm_non_prod_cmd` is registered. The two earlier custom/unregistered
identities could construct local framework objects but received no session
creation response because they had no configured PAS receiver queue.

Build and verify with `make test verify`. The resulting executable is
`../dist/sm_reload_probe`.

```
LD_LIBRARY_PATH=/pas/lib/dll:/lib:/usr/lib /datastore/navicore-mock/bin/sm_reload_probe probe
LD_LIBRARY_PATH=/pas/lib/dll:/lib:/usr/lib /datastore/navicore-mock/bin/sm_reload_probe start nv_navigation
```

## Standalone-path status

The reserved-identity bench probe completed the constructor and both generated
registration calls, but received no SessionCreationResponse and never became
active. It therefore sent no AppStart request. This artifact is retained for
diagnostic reproducibility, not approved as a working restart tool.

Missing `-r`/`-m` arguments do not provide a proven correction. The framework
option table identifies `-m` as the PAS logging mask; production launch records
place values such as `0xDC000308` in the logging-mask column. `-r output=0x2`
also configures logging. This helper passes only its executable name to
`CService::Run`, so `start nv_navigation` is never exposed to the vendor option
parser, and the observed initialization proves argument parsing already
succeeded. There is no launch record or authoritative logging mask for
`ss_service_lifecycle_test_client`, so one must not be guessed.

Static framework analysis shows that the generated-session constructor
registers the client object with its parent framework; it does not directly
send the open-session request. Opening is driven later by the framework's
server-availability state reaction. With no creation callback after three
standalone trials, the standalone CService bootstrap lacks another proven
factory step. Further command-line experiments are not justified without an
exact engineering test-client launch contract.
