# NaviCore PAS reload helper

`navicore_reload` is a QR39.6 ARM/QNX client for the supported
`CNavigationNaviMapUpdateSessionIf` Stop and Start requests. It links against
the genuine firmware `libns_framework_unified.so` and
`libnv_navigation_if.so` at runtime; it does not inject raw PAS messages. The
firmware DSOs have deliberately minimal/broken section metadata which the SDK
linker rejects, so the helper resolves their genuine exported C++ entry points
with `dlopen`/`dlsym` instead of linking against fabricated import libraries.
The helper also exports the two zero-initialized process globals required by
the framework DSO (`g_PasLogParams` and `g_PasVersion`).

Build and inspect it offline:

```sh
make -C /mnt/d/open_navicore/tools/qnx/navicore_control verify
```

The result is `../dist/navicore_reload`. On the bench, with the normal PAS
runtime/library search paths:

```sh
navicore_reload stop
# change the provider at the configured NaviDllName pathname here
navicore_reload start
```

`navicore_reload probe` performs the same framework/session initialization but
exits cleanly as soon as the `nv_navigation` session becomes active; it sends
no lifecycle request.

Exit status is zero only after the matching response callback reports PAS
success. Queue failure, unavailable session, response failure, early framework
exit, and the 30-second timeout all return nonzero.

## Recovered ABI

The bootstrap follows the stock `nv_navimapupdate` executable: construct a
`CService`, call `CService::Run(argc, argv, NULL)`, and use that service's
zero-offset `CFramework` base for the navigation session. The helper uses the
unique client name `navicore_reload` so it cannot collide with a running stock
`nv_navimapupdate` receiver. The session's
base size (`0x60`) and response vtable slots (`+0x20` Stop, `+0x24` Start) were
recovered from the same QR39.6 binaries. The helper copies the genuine vtable
and replaces only those response callbacks.

Session construction, response registration, and request-thread startup occur
inside the recovered `CService` initialization hook (vtable slot 2), after the
dispatcher exists. All failures request coordinated framework shutdown; after
`Run` returns, the helper joins its worker, restores both genuine vtables, and
calls the vendor destructors.

`CService` is an abstract generated-service base. The helper also copies its
genuine vtable and supplies success/no-op implementations only for the two
hooks that `Run()` invokes (`+0x08` initialize and `+0x1c` started). It does
not publish a generated server interface.

This executable is firmware-build-specific. Its ABI constants must be
revalidated before use on a different release. No live-radio action is part of
the build or verification target.
