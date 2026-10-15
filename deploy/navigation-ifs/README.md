# Open NaviCore as the default navigation (navigation.ifs)

Installs Open NaviCore as the unit's navigation provider on every boot, with
no fallback to the native NaviCore. It replaces the earlier one-shot
activations (`navicore-v12`, `-v13`, `-mock-final-a1`: an arm marker was
consumed and the config redirected for that boot only, so any reboot
returned to the native provider) and the bench `diag_launch.sh` flow, which
switched back to the stock config on the first sign of trouble.

## Pieces

| Piece | Where on the unit | Built by |
|---|---|---|
| `navigation.ifs`: provider + `nv_navigation.cfg` under `/open-navicore` | `/datastore/navicore/navigation.ifs` | `CMakeLists.txt` (ifs-build) + `build_navigation_ifs.py` (payload) |
| Boot hook: `bin/boot_overlay_main.sh` mounts it and links `/pas/scfg/nv_navigation.cfg` | `/datastore/ifs-overrides/boot_overlay.ifs` | `patch_boot_overlay.py` |
| Backend plugin, OsmAnd, resources, maps, vector cache | `/datastore/navicore-backend/` (unchanged) | `make osmand-backend` |

The config is the factory `nv_navigation.cfg` with one change,
`NaviDllName=/open-navicore/libNaviCore.so`. The provider loads the backend
from its fixed path in `/datastore/navicore-backend`, so the backend can be
updated without rebuilding the image.

At boot the hook (after the userland overlay and the optional UI overlay,
before stock startup continues) logs to `/datastore/boot_overlay.log`:
`open_navicore_mount_rc=0` and `open_navicore_config_link_rc=0` when active,
or `open_navicore_disabled_by_marker`, `open_navicore_image_absent`,
`open_navicore_image_incomplete`.

## Build (host)

The image is packaged by [ifs-build](../../../ifs-build), the workspace's
IFS packaging project (a sibling checkout of this repository, like
dev-tools). This directory only declares the contents.

```sh
make                                  # dist/libNaviCore.so (provider)
make osmand-backend                   # dist/libMockNavBackendOsmAnd.so
cmake -S deploy/navigation-ifs -B build/navigation-ifs \
      -DSTOCK_NV_NAVIGATION_CFG=<factory nv_navigation.cfg>
      # from a linked worktree also: -DIFSBUILD_DIR=<ifs-build checkout>
cmake --build build/navigation-ifs --target ifs
      # build/navigation-ifs/ifs/navigation.{build,ifs}, payload/SHA256SUMS
```

The factory config must have SHA-256 `c889c2b4…d680e` (QR39.5); it is not
committed. The image is stored `+raw` (uncompressed), so the provider inside
is byte-identical to `dist/libNaviCore.so`; all entries are mode 0555. The
QNX SDK is found by ifs-build's toolchain (`-DQNX_SDK=` or `$QNX_SDK` to
override).

`patch_boot_overlay.py` (needed once, already applied on the bench unit)
changes only the navigation section of the boot script and verifies every
other entry byte-identical (`REPORT`, `boot_overlay_main.diff`):

```sh
python3 deploy/navigation-ifs/patch_boot_overlay.py \
    <copy of the unit's boot_overlay.ifs> <new output dir>
```

## Install (unit)

```sh
cmake --build build/navigation-ifs --target deploy-navigation-dry-run   # the plan
cmake --build build/navigation-ifs --target deploy-navigation
```

`deploy_navigation_ifs.sh` uploads the image as
`/datastore/navicore/navigation.ifs.new` with ifs-build's `ifs-deploy`
(read-back verified), keeps the current image (pulled to
`./ifs-deploy-backups/` and copied on the unit to `navigation.pre-<stamp>.ifs`),
then swaps by `rm` + `cp -p`, flushes and verifies, restoring the previous
image on a mismatch. A mounted IFS is never overwritten in place (unit
ledger, 2026-10-13: a truncated in-place copy made `mountifs` crash at boot).
Do not use the `deploy-ifs` target that ifs-build also defines: it uploads to
`/datastore/ifs-overrides`, where the boot hook does not look.

Then reboot once (the provider is mapped by the running `nv_navigation`;
the new image is mounted at the next boot) and check
`/datastore/boot_overlay.log` for `open_navicore_mount_rc=0` and
`open_navicore_config_link_rc=0`, and that `nv_navigation` maps
`/open-navicore/libNaviCore.so`. Record the change in the unit ledger.

Settings: `RADIO_HOST`, `RADIO_SSH_KEY`, `NAVIGATION_IFS_DIR`, `BACKUP_DIR`
and `RADIO_SYNC` (the unit's flush helper; it has no `sync`), see the script
header.

The backend files in `/datastore/navicore-backend/` must already be in place
(they are on the bench unit).

## Rollback

There is no automatic fallback. To return to the native NaviCore:

- create `/datastore/navicore/disabled` and reboot (the hook then leaves the
  factory config in place), or
- restore the backed-up `boot_overlay.ifs` and reboot, or
- from SD recovery, remove `/datastore/navicore/navigation.ifs`.

A provider that crashes at every start makes System Manager relaunch
`nv_navigation` three times and then reset the unit, on every boot. SSH comes
up during boot independently of navigation, so the `disabled` marker can be
created between resets; SD recovery remains the last resort.
