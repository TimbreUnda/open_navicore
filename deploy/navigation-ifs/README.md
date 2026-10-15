# Open NaviCore as the default navigation (navigation.ifs)

Installs Open NaviCore as the unit's navigation provider on every boot, with
no fallback to the native NaviCore.

## Pieces

| Piece | Where on the unit | Built by |
|---|---|---|
| `navigation.ifs`: provider, `nv_navigation.cfg` and `boot.sh` under `/open-navicore` | `/datastore/navicore/navigation.ifs` | `CMakeLists.txt` (ifs-build) + `build_navigation_ifs.py` (payload) |
| Boot overlay: mounts the image at boot and runs its `boot.sh` | `/datastore/ifs-overrides/boot_overlay.ifs` | not this repository (see "Boot") |
| Backend plugin, OsmAnd, resources, maps, vector cache | `/datastore/navicore-backend/` | `make`, `deploy/backend/deploy_backend.sh` |

The config is the factory `nv_navigation.cfg` with one change,
`NaviDllName=/open-navicore/libNaviCore.so`. The provider loads the backend
from its fixed path in `/datastore/navicore-backend`, so the backend can be
updated without rebuilding the image.

## Boot

The boot overlay is the only code that runs early at boot (the `/bin/cat`
hook starts it before the stock services). For navigation it does two
things, logging to `/datastore/boot_overlay.log`:

1. unless `/datastore/navicore/disabled` exists (`open_navicore_disabled_by_marker`)
   or the image is missing (`open_navicore_image_absent`), mount
   `/datastore/navicore/navigation.ifs` at `/` (`open_navicore_mount_rc=0`);
2. run the image's own `/open-navicore/boot.sh`, which links
   `/pas/scfg/nv_navigation.cfg` to the image's config
   (`open_navicore_config_link_rc=0`, or `open_navicore_image_incomplete`).

Everything navigation-specific is in the image (`boot.sh`), so a new
navigation never needs the overlay changed. The overlay itself is a shared
boot image with no source tree yet; it is to be rebuilt from source with
ifs-build in its own project, mounting images and running their boot
scripts generically. The bench unit's current overlay still carries the
same steps inline (an earlier patch of the live image), so `boot.sh` takes
over when that rebuilt overlay is installed; until then it is shipped but
not run.

## Build (host)

The image is packaged by [ifs-build](../../../ifs-build), the workspace's IFS
packaging project (a sibling checkout of this repository, like dev-tools); this
directory only declares the contents (`CMakeLists.txt`, part of the product
build).

```sh
make ifs STOCK_NV_NAVIGATION_CFG=<factory nv_navigation.cfg>
      # build/qnx/ifs/navigation.{build,ifs}; the setting is remembered
```

The factory config must have SHA-256 `c889c2b4…d680e` (QR39.5); it is not
committed. The image is stored `+raw` (uncompressed), so the provider inside
is byte-identical to `build/qnx/libNaviCore.so`; all entries are mode 0555.
A changed provider rebuilds the image.

## Install (unit)

```sh
deploy/navigation-ifs/deploy_navigation_ifs.sh build/qnx/ifs/navigation.ifs \
    <ifs-build>/bin/ifs-deploy --dry-run          # the plan
deploy/navigation-ifs/deploy_navigation_ifs.sh build/qnx/ifs/navigation.ifs \
    <ifs-build>/bin/ifs-deploy
```

It uploads the image as `/datastore/navicore/navigation.ifs.new` with
ifs-build's `ifs-deploy` (read-back verified), keeps the current image (pulled
to `./ifs-deploy-backups/` and copied on the unit to
`navigation.pre-<stamp>.ifs`), then swaps by `rm` + `cp -p`, flushes and
verifies, restoring the previous image on a mismatch. A mounted IFS is never
overwritten in place (unit ledger, 2026-10-13: a truncated in-place copy made
`mountifs` crash at boot). Do not use ifs-build's generic `deploy-ifs` target
for this image: it uploads to `/datastore/ifs-overrides`, where the overlay does
not look for navigation.

The provider and the backend are installed together: the provider loads
`/datastore/navicore-backend/libOpenNaviBackendOsmAnd.so`, so install that
(`deploy/backend/deploy_backend.sh`) before rebooting into a new image.

Then reboot once (the provider is mapped by the running `nv_navigation`; the
new image is mounted at the next boot) and check `/datastore/boot_overlay.log`
for `open_navicore_mount_rc=0` and `open_navicore_config_link_rc=0`, and that
`nv_navigation` maps `/open-navicore/libNaviCore.so`. Record the change in the
unit ledger.

Settings: `RADIO_HOST`, `RADIO_SSH_KEY`, `NAVIGATION_IFS_DIR`, `BACKUP_DIR` and
`RADIO_SYNC` (the unit's flush helper; it has no `sync`), see the script
header.

## Rollback

There is no automatic fallback. To return to the native NaviCore:

- create `/datastore/navicore/disabled` and reboot (the overlay then leaves
  the factory config in place), or
- copy back `navigation.pre-<stamp>.ifs` (rm + cp -p) and reboot, or
- from SD recovery, remove `/datastore/navicore/navigation.ifs`.

A provider that crashes at every start makes System Manager relaunch
`nv_navigation` three times and then reset the unit, on every boot. SSH comes
up during boot independently of navigation, so the `disabled` marker can be
created between resets; SD recovery remains the last resort.
