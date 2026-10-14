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
| `navigation.ifs`: provider + `nv_navigation.cfg` under `/open-navicore` | `/datastore/navicore/navigation.ifs` | `build_navigation_ifs.py` |
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

```sh
make                                  # dist/libNaviCore.so (provider)
make osmand-backend                   # dist/libMockNavBackendOsmAnd.so
python3 deploy/navigation-ifs/build_navigation_ifs.py \
    dist/libNaviCore.so <factory nv_navigation.cfg> /tmp/navigation-ifs
python3 deploy/navigation-ifs/patch_boot_overlay.py \
    <copy of the unit's boot_overlay.ifs> /tmp/boot-overlay
```

The factory config must have SHA-256 `c889c2b4…d680e` (QR39.5); it is not
committed. `patch_boot_overlay.py` changes only the navigation section of
the boot script and verifies every other entry byte-identical
(`REPORT`, `boot_overlay_main.diff`). `mkifs` keeps the provider unstripped
(`+raw`), so it is byte-identical to `dist/libNaviCore.so`.

## Install (unit)

Never overwrite a mounted IFS in place (see the unit ledger, 2026-10-13:
a truncated in-place copy made `mountifs` crash at boot). Stage, verify,
then rename:

1. Back up `/datastore/ifs-overrides/boot_overlay.ifs` (for example
   `boot_overlay.pre-navigation-ifs.ifs`) and record its SHA-256.
2. Copy `navigation.ifs` to `/datastore/navicore/navigation.ifs`
   (a new file) and the new boot overlay to
   `/datastore/ifs-overrides/boot_overlay.ifs.new`; read both back and check
   their SHA-256 against `SHA256SUMS` / `REPORT`; sync.
3. Rename `boot_overlay.ifs.new` over `boot_overlay.ifs`, sync, and do the
   one documented reboot.
4. Check `/datastore/boot_overlay.log` for the two `rc=0` lines and that
   `nv_navigation` maps `/open-navicore/libNaviCore.so`.

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
