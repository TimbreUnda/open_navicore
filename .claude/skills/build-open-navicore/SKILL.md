---
name: build-open-navicore
description: Build and test Open NaviCore - host tests, the QNX provider (libNaviCore.so), the OsmAnd backend plugin, UI renders, and the navigation.ifs image through ifs-build. Use before any deploy, after code changes, or when asked how to build, test or package the navigation.
---

# Build Open NaviCore

Everything builds from the repository root. Run the host tests on every
change; build for QNX before a deploy.

## 1. Host tests (no SDK needed)

```sh
make test-host          # contracts, provider/runtime smoke tests, plugin tests,
                        # osmand session, navigation UI, camera, labels, vector
```

Single suites while iterating: `make navigation-ui-test`,
`make camera-controller-test`, `make osmand-session-test`, `make labels-test`.
`host-smoke` has a timing-sensitive lifecycle test that occasionally fails on
a loaded host; rerun the binary (`<build>/host-smoke`) before suspecting a
change.

UI renders (host LVGL, no GL): `make ui-render-tool`, then
`<build>/lvgl-host/ui-render-tool <out-dir>` writes raw 1024x574 ARGB8888
frames (`free.argb`, `guidance.argb`, `pin.argb`, `dev.argb`, ...). Convert
to PNG with Pillow (`Image.frombuffer('RGBA', (1024, 574), raw, 'raw', 'BGRA', 0, 1)`)
and look at them before claiming a UI change works.

## 2. QNX builds

The QNX targets need the workspace toolchains, passed on the make command
line (paths relative to the workspace root that holds this repository):

| Variable | What |
|---|---|
| `QNX_SDK` | QNX 6.6 SDK (`qnx660-sdk`) |
| `QNX_HOST_LIBS` | host libraries the SDK binaries need (`tools/qnx-host-libs`) |
| `OSMAND_QNX_ROOT` | the OsmAnd QNX build tree (`osmand-qnx-build`) |
| `OSMAND_QT_PREFIX` | its Qt prefix (`osmand-qnx-build/qt-prefix`) |

```sh
make "$PWD/dist/libNaviCore.so" <vars>   # provider
make osmand-backend <vars>               # dist/libOpenNaviBackendOsmAnd.so
```

The provider rule is the absolute `$(CURDIR)/dist/libNaviCore.so`: a
relative `make dist/libNaviCore.so` matches no rule and silently does
nothing when the file exists. (`make all` builds it too, plus the test
plugins.) The
OsmAnd headers produce about a thousand `-Wignored-attributes` /
`-Wsign-compare` warnings; only warnings in `backends/osmand/` or `src/`
are ours. The backend embeds `git describe --dirty` (developer menu,
`build_info.cpp`).

## 3. navigation.ifs (ifs-build)

The provider ships in `navigation.ifs`, packaged by **ifs-build** (the
workspace's IFS packaging project, a sibling checkout `../ifs-build`; its
own skill is `deploy-ifs-image`). This repository only declares the
contents in `deploy/navigation-ifs/CMakeLists.txt`.

```sh
cmake -S deploy/navigation-ifs -B build/navigation-ifs \
      -DSTOCK_NV_NAVIGATION_CFG=<factory nv_navigation.cfg, QR39.5>
      # from a linked worktree add -DIFSBUILD_DIR=<ifs-build checkout>
cmake --build build/navigation-ifs --target ifs
```

The factory config (SHA-256 `c889c2b4…d680e`) is not committed; take it
read-only from the QR39.5 firmware dump (`pas/scfg/nv_navigation.cfg` of the
root partition). The payload step checks it and the provider's exports.
Inspect the result without the radio:

```sh
python3 <workspace>/tools/qnx_ifs.py build/navigation-ifs/ifs/navigation.ifs list
```

The provider inside must be byte-identical to `dist/libNaviCore.so` (the
image is `+raw`). Deploying: skill `deploy-open-navicore`.

## Before committing

- `make test-host` passes, and the QNX build of whatever you touched.
- `tests/check_portable_paths.py` (part of the contract checks) rejects
  workstation paths in tracked files (mount points, home directories) in
  code, docs or skills - use relative paths and variables.
- Contract changes (`include/open_navicore/*.h`, `contracts/`) keep the
  ABI append-only; see `docs/design/api-ui-restrictions.md`.
