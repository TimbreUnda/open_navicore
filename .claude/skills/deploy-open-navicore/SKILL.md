---
name: deploy-open-navicore
description: Install Open NaviCore on the VP4R bench radio - one image, navigation.ifs (provider, OsmAnd backend, OsmAnd core and resources), staged upload, rename swap, one reboot; verification and rollback. Use when deploying a new build, checking what is installed, or rolling back to the native navigation.
---

# Deploy Open NaviCore to the radio

A new version of the navigation is one file, `navigation.ifs`, and one
reboot:

| On the unit | What | Installed by |
|---|---|---|
| `/datastore/navicore/navigation.ifs`, mounted at `/` at boot by the boot overlay | all code under `/open-navicore/`: provider `libNaviCore.so`, its `nv_navigation.cfg` and `boot.sh`, the backend `libOpenNaviBackendOsmAnd.so`, OsmAnd core and resource bundle | `deploy/navigation-ifs/deploy_navigation_ifs.sh` |
| `/datastore/navicore-backend/` | data only: `maps/*.obf`, `recents.tsv`, `vectorcache/`, the bench marker files | by hand (maps), or written by the backend |

Every change (UI, map, routing, provider) is a new image. There is no
backend-only install; the code is read-only in the mounted image.

## 0. Before touching the unit

- Build first (skill `build-open-navicore`): `make test`, `make` and
  `make ifs` pass.
- Follow the workspace skill `record-unit-modification`: confirm the unit's
  identity, write the ledger entry first (`pending restart`), update it
  after the check.
- Bench deploy and the one documented reboot are covered by the standing
  authorization in the workspace `AGENTS.md`. A second reboot is not: ask.
  Never loop a reboot.
- The unit needs dev-tools mounted (`/usr/bin/bench_rename`,
  `/usr/bin/vp4r-sync`, `/usr/bin/vp4r-reboot`); the script checks.
- Space: the image is about 100 MiB and the swap keeps one previous image,
  so `/datastore` needs one image of free space (it had 128 MiB on
  2026-10-15, before the old per-file backend copies were removed).

## 1. Install

```sh
deploy/navigation-ifs/deploy_navigation_ifs.sh build/qnx/ifs/navigation.ifs \
    <ifs-build>/bin/ifs-deploy --dry-run     # plan only
deploy/navigation-ifs/deploy_navigation_ifs.sh build/qnx/ifs/navigation.ifs \
    <ifs-build>/bin/ifs-deploy
```

It uploads with ifs-build's `ifs-deploy` as `navigation.ifs.new` (read-back
verified by scp), pulls the current image to local `ifs-deploy-backups/`,
then on the unit renames `navigation.ifs` to `navigation.previous.ifs` and
`navigation.ifs.new` to `navigation.ifs` (no copies), flushes and checks the
installed file with the unit's `/usr/bin/sha256sum` (dev-tools) against the
host hash, renaming the previous image back on a mismatch. Never copy over a
mounted IFS in place (a truncated in-place copy once made `mountifs` crash
at boot); renaming is safe. Do **not** use ifs-build's generic `deploy-ifs`
target: it uploads to `/datastore/ifs-overrides`, which the navigation hook
does not read.

Settings: `RADIO_HOST` (default the bench unit), `RADIO_SSH_KEY` (default
`$HOME/priv_openssh`), `NAVIGATION_IFS_DIR`, `BACKUP_DIR`, `RADIO_SYNC`,
`RADIO_RENAME` (script header).

Then one reboot: `/usr/bin/vp4r-sync`, then `/usr/bin/vp4r-reboot`
(workspace skill `dev-tools-on-radio`). ssh returns in about a minute.

## 2. Verify

- `/bin/cat /datastore/boot_overlay.log`: the last block has
  `open_navicore_mount_rc=0` and `open_navicore_config_link_rc=0`.
- `for f in /open-navicore/*; do echo $f; done` lists the provider, backend,
  core and bundle; `nv_navigation` maps them:
  `/proc/boot/pidin -p <pid> mem` (pid from `/proc/boot/pidin -f an`).
- Provider trace `/bin/cat /tmp/open-navi.log`; backend trace
  `/bin/cat /tmp/open-navicore-osmand.log` (start, attach,
  `init=resources`, `surface_created=...`, `cluster=publish_status`).
- On screen: hold the day/night button 1.5 s in free drive for the
  developer menu (build commit, frame rate, raw car inputs, cluster).
- Hash on the unit: `/usr/bin/sha256sum /datastore/navicore/navigation.ifs`
  equals the host's `sha256sum build/qnx/ifs/navigation.ifs`.
- Update the ledger entry to `active`, or record what failed.

## 3. Rollback

- Previous version: on the unit `/bin/rm navigation.ifs;
  /usr/bin/bench_rename navigation.previous.ifs navigation.ifs`, flush,
  reboot (or install a local backup from `ifs-deploy-backups/` with the
  script).
- Back to the native navigation: `touch /datastore/navicore/disabled` and
  reboot (the boot hook leaves the factory config in place).
- A provider that crashes at every start makes System Manager reset the unit
  after three relaunches; ssh comes up first, so the marker can be created
  between resets.
- No ssh: recovery SD (physical; ask the operator).

## Radio facts

No `ls`, `head`, `df`, `mv`, `md5sum`, `sha256sum`, `sync` on the stock
PATH; present: `/bin/cat`, `/bin/cp`, `/bin/rm`, `/proc/boot/pidin`,
`/bin/slay`, shell globs. dev-tools (mounted at boot) adds `/usr/bin`
`md5sum`, `sha256sum` (`-c` check mode), `chmod`, `vp4r-sync`, `vp4r-reboot`,
`bench_rename`; skill `dev-tools-on-radio`.
ssh needs `-oHostKeyAlgorithms=+ssh-dss -oPubkeyAcceptedAlgorithms=+ssh-rsa
-i <key>`, scp needs `-O`; every connection prints a `welcome` banner. The
unit's clock is unset: note host time around each action in the ledger.
