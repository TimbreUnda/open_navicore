# Mock NaviCore provider: reversible bench deployment

This directory contains a deliberately narrow first-test harness. It redirects
only `/pas/scfg/nv_navigation.cfg` through QNX's in-memory process-manager
namespace. The redirected config points to
`/datastore/navicore-mock/provider/libNaviCore_mock.so`.

It does **not** overwrite, rename, or delete `/navi/libNaviCore.so`, and it does
not alter the IFS-backed factory config. It also does not install a persistent
boot hook. A reboot clears the redirect and restores ordinary factory pathname
resolution.

## Safety model

The controller refuses to install unless all of these are true:

- `/` is the reviewed normal-firmware eMMC root, `/dev/sd1t177.1`;
- the SD-recovery root `/dev/sd0t12` is not active;
- the resolved factory config has the reviewed SHA-256
  `c889c2b4c5d1d5ddb4b32c0c79a46c8482950ee0db783a83b1ed949aef7d680e`;
- provider and override config match the hashes in `deploy.sha256`;
- the override has exactly one `NaviDllName`, with the fixed staged path;
- an operator has created the one-line bench-consent marker containing the
  provider hash they reviewed.

The normal-firmware check is intentional. A process-manager link created from
SD recovery changes the recovery namespace, not the next eMMC boot, and could
give a false impression that an installed-system override exists.

This remains a bench-only experiment. A mocked provider can fail API version
negotiation, return an incomplete C++ interface graph, crash `nv_navigation`,
or stop its heartbeat. The stock config allows a 2,000 ms heartbeat interval
and 15 missed messages; service recovery or a whole-unit reset is possible.
Do not test while driving. Keep independent SD recovery and serial/SSH access.

## Prepare a pinned bundle

Run on the development host:

```sh
./prepare_bundle.sh \
  ../dist/libNaviCore.so \
  ../../dumps/USA-ASO-8.4-QR-39.5.0-20210201-Q0H0/extracted/p02_root_a/pas/scfg/nv_navigation.cfg \
  /tmp/navicore-mock-bundle \
  /path/to/arm-qnx/openssl
```

The provider must be an ELF32 ARM shared object exporting:

- `panaVersionCheck`
- `panaInitialize`
- `panaGetSoftwareInfrastructure`
- `panaDeinitialize`

The preparation step rejects the wrong stock-config revision, normalizes the
copied override to LF endings, changes only `NaviDllName`, verifies the basic
ELF ABI and exports, and writes SHA-256 pins. It refuses to overwrite an
existing output directory.

Copy the *contents* of the resulting directory to the radio's fixed staging
directory `/datastore/navicore-mock`. If OpenSSL was not bundled, provide the
already staged ARM/QNX executable at `bin/openssl`, or set `OPENSSL` explicitly.
No command in this repository has been run against the live radio.

### Sparse-shell audit

A read-only check of the normal-firmware bench shell found
`PATH=/usr/bin:/bin:/usr/sbin:/sbin`, but scripts must not rely on that value.
The required utilities were verified at these absolute paths:

```text
/bin/awk
/bin/cp
/bin/mkdir
/bin/slay
/usr/bin/grep
/proc/boot/ln
/proc/boot/mount
/proc/boot/on
/proc/boot/pidin
/proc/boot/sloginfo
/proc/boot/waitfor
```

There is no executable `ls` in `/bin`, `/usr/bin`, or `/proc/boot`; no `sync`
in those locations; and no `/bin/grep` or `/proc/boot/grep` on the tested
image (`grep` is `/usr/bin/grep`). The radio-side controller uses only absolute
paths for external commands and now preflights all utilities it needs. Shell
builtins such as `echo`, `test`, `case`, and `exit` remain safe.

## Bench commands

First inspect without changing namespace state:

```sh
/datastore/navicore-mock/navicore_provider_ctl.sh status
```

Review and record `deploy.sha256`, then deliberately arm this exact bundle:

```sh
PROVIDER_SHA256=replace_with_the_reviewed_64_hex_digest
echo "I_ACCEPT_NAVIGATION_WATCHDOG_RESET_RISK $PROVIDER_SHA256" > \
  /datastore/navicore-mock/ALLOW_BENCH_PROVIDER_TEST
/datastore/navicore-mock/navicore_provider_ctl.sh install
/datastore/navicore-mock/navicore_provider_ctl.sh status
```

Do not populate `PROVIDER_SHA256` by blindly reading the radio-side manifest;
compare it with the retained host build evidence first. The override config's
hash is independently pinned in the controller, rather than trusted only from
the adjacent manifest.

`install` only changes future pathname resolution. It intentionally does not
kill, stop, or restart the already-running service. Use the separately proven
PAS service lifecycle procedure to reload `nv_navigation`; do not substitute
an unreviewed `kill` loop. Capture logs before that action.

### Reload investigation

The deterministic lifecycle route is System Manager's test-control interface,
not direct process execution and not `slay`:

1. send `AppStopRequest` for service name `nv_navigation` and require a
   successful `AppStopResponse`;
2. verify with `/proc/boot/pidin ar` that the old PID is absent and capture
   System Manager logs;
3. send `AppStartRequest` for `nv_navigation` and require a successful
   `AppStartResponse`;
4. require a new PID, session connection/readiness, successful provider logs,
   and heartbeat acknowledgements.

Static and live evidence support this route:

- `ss_system_manager` implements `OnAppStopRequestHandler`,
  `OnAppStartRequestHandler`, `StopModule`, and `StartModule`;
- `libss_system_manager_if.so` exports the matching test-control requests;
- `libss_rcs_test_framework_plugin.so` calls `SendAppStopRequest` and
  `SendAppStartRequest`, naming `ss_system_manager` as its target;
- the live launch row is `Launch110`, with binary
  `/pas/bin/nv_navigation`, arguments `-r output=0x2`, priority 15, CPU mask
  `0x02`, framework mode enabled, runtime-critical enabled, three relaunches,
  a 30-second shutdown allowance, 5-second heartbeat period, and ten missed
  heartbeat retries;
- live `sm_config.cfg` enables module heartbeats and sets the module start
  response timeout to 180 seconds.

No existing shell CLI for that interface was found. The RCS plugin proves an
intended control path, but its external command framing and response semantics
have not yet been validated. Therefore the harness deliberately has no
`restart` command yet. Build and bench-prove a small PAS test-control client,
including stop/start response parsing and timeouts, before adding one.

Do not use `/bin/slay nv_navigation` as a reload shortcut. System Manager's
own diagnostics say a shell-issued SIGKILL is treated as a developer action
with no further exit processing. Other crashes/exits take the runtime-critical
relaunch path; after the configured three relaunches are exhausted, System
Manager says it will reset the unit. Launching `/pas/bin/nv_navigation`
directly would bypass System Manager's module state, ownership, readiness,
heartbeat, priority, affinity, and relaunch accounting.

For an in-session rollback before a controlled service reload:

```sh
/datastore/navicore-mock/navicore_provider_ctl.sh rollback
/datastore/navicore-mock/navicore_provider_ctl.sh status
```

Rollback repoints the namespace entry to a hash-verified stock-config copy.
It preserves the rejected provider and all evidence. A reboot is the simpler
and stronger rollback: process-manager links are memory-only, so the factory
IFS config becomes visible again without depending on the copied file.

## Required evidence for the first activation

Do not call the provider usable until one test run records all of the following:

1. Pre-change `mount`, `pidin ar`, resolved config SHA-256, provider SHA-256,
   `deploy.sha256`, and a `sloginfo` snapshot.
2. `install` output and a post-install `status` showing
   `config_state=mock-provider-redirect` while the old process is still alive.
3. The exact controlled service stop/start commands and timestamps.
4. Loader diagnostics proving the expected provider path and all four entry
   points were accepted.
5. Version-check, initialize, interface-acquisition, and deinitialize call logs
   from the mock, including thread IDs and return values.
6. Heartbeat/watchdog, Service Manager, HMI readiness, render-start/stop, and
   crash/reset evidence for at least two minutes after launch.
7. A rollback or reboot followed by the known stock config hash, stock provider
   startup, Navigation-button behavior, and absence of a reset loop.

If the service hangs or the HMI becomes unavailable, stop experimenting and
reboot. If normal firmware does not recover, boot the already-proven SD image;
do not add persistence while diagnosing the first mocked-provider failure.
