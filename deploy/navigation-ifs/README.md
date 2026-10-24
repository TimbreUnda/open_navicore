# Open NaviCore as the default navigation (navigation.ifs)

One image holds the whole navigation code and installs Open NaviCore as the
unit's navigation provider on every boot, with no fallback to the native
NaviCore. A new version is this one file and one reboot.

## Pieces

| Piece | Where on the unit | Built by |
|---|---|---|
| `navigation.ifs`, all code under `/open-navicore`: provider, `nv_navigation.cfg`, `boot.sh`, OsmAnd backend, OsmAnd core and resource bundle | `/datastore/navicore/navigation.ifs` | `CMakeLists.txt` (ifs-build) + `build_navigation_ifs.py` (payload) |
| Boot overlay: mounts the image at boot and runs its `boot.sh` | `/datastore/ifs-overrides/boot_overlay.ifs` | not this repository (see "Boot") |
| Data: maps, recents, vector cache, bench markers | `/datastore/navicore-backend/` | maps by hand; the rest is written by the backend |

The config is the factory `nv_navigation.cfg` with one change,
`NaviDllName=/open-navicore/libNaviCore.so`. The provider loads
`/open-navicore/libOpenNaviBackendOsmAnd.so`, whose RPATH `/open-navicore`
finds the OsmAnd core; the backend opens the resource bundle there too. Qt
and the C++ runtime are linked statically. All entries are stored `+raw`
(uncompressed, page-aligned) so the libraries are mapped in place.

## Boot

**vp4r layer** (sibling checkouts `vp4r-loader` and `ifs-build`, see `ifs-build/docs/LAYOUT.md`): the
image is installed as `$VP4R_HOME/slots/<slot>/ifs/50-navicore.ifs`; the loader
mounts it at `/` after the stock images. The image carries the config twice,
at `/open-navicore/nv_navigation.cfg` and at `/pas/scfg/nv_navigation.cfg`;
the second shadows the factory file, so nothing needs to run at boot. Off:
`$VP4R_HOME/flags/navicore.d/disabled` (native NaviCore returns).

**Older boot overlay** (39.2 bench chain before 2026-10-23):

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
      # build/qnx/ifs/navigation.{build,ifs} (about 100 MiB); remembered
```

The factory config must have SHA-256 `c889c2b4…d680e` (QR39.5); it is not
committed. The OsmAnd core and resource bundle come from the OsmAnd QNX
build (`OSMAND_QNX_ROOT`, `source/binaries/qnx/gcc-armv7/Release`;
`OSMAND_RESOURCES_BUNDLE` overrides the bundle). All entries are mode 0555.
A changed provider, backend, core or bundle rebuilds the image.

## Install (unit)

```sh
deploy/navigation-ifs/deploy_navigation_ifs.sh build/qnx/ifs/navigation.ifs \
    <ifs-build>/bin/ifs-deploy --dry-run          # the plan
deploy/navigation-ifs/deploy_navigation_ifs.sh build/qnx/ifs/navigation.ifs \
    <ifs-build>/bin/ifs-deploy
```

It uploads the image as `/datastore/navicore/navigation.ifs.new` with
ifs-build's `ifs-deploy` (read-back verified), pulls the current image to
`./ifs-deploy-backups/`, then on the unit renames the current image to
`navigation.previous.ifs` (replacing the last one) and the new one into place,
flushes and verifies, renaming the previous image back on a mismatch. The
rename swap needs no copies, so `/datastore` needs one image of free space.
It uses dev-tools' `/usr/bin/bench_rename` and `/usr/bin/vp4r-sync` (the unit
has no `mv` or `sync`). A mounted IFS is never overwritten in place (unit
ledger, 2026-10-13: a truncated in-place copy made `mountifs` crash at boot);
renaming it is safe. Do not use ifs-build's generic `deploy-ifs` target for
this image: it uploads to `/datastore/ifs-overrides`, where the overlay does
not look for navigation.

Then reboot once (the provider is mapped by the running `nv_navigation`; the
new image is mounted at the next boot) and check `/datastore/boot_overlay.log`
for `open_navicore_mount_rc=0` and `open_navicore_config_link_rc=0`, and that
`nv_navigation` maps `/open-navicore/libNaviCore.so`. Record the change in the
unit ledger.

Settings: `RADIO_HOST`, `RADIO_SSH_KEY`, `NAVIGATION_IFS_DIR`, `BACKUP_DIR`,
`RADIO_SYNC` and `RADIO_RENAME`, see the script header.

## Rollback

There is no automatic fallback. To return to the native NaviCore:

- create `/datastore/navicore/disabled` and reboot (the overlay then leaves
  the factory config in place), or
- rename `navigation.previous.ifs` back (`rm navigation.ifs`, then
  `bench_rename navigation.previous.ifs navigation.ifs`) and reboot, or
- from SD recovery, remove `/datastore/navicore/navigation.ifs`.

A provider that crashes at every start makes System Manager relaunch
`nv_navigation` three times and then reset the unit, on every boot. SSH comes
up during boot independently of navigation, so the `disabled` marker can be
created between resets; SD recovery remains the last resort.
