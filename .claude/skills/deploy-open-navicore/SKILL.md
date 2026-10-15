---
name: deploy-open-navicore
description: Install Open NaviCore on the VP4R bench radio - the backend plugin (deploy/backend/deploy_backend.sh), the provider image navigation.ifs (ifs-build, staged swap, one reboot), verification and rollback. Use when deploying a new backend or provider build, checking what is installed, or rolling back to the native navigation.
---

# Deploy Open NaviCore to the radio

Two independent pieces, two paths:

| Piece | On the unit | Takes effect | How |
|---|---|---|---|
| Backend plugin (OsmAnd, UI, map) | `/datastore/navicore-backend/libOpenNaviBackendOsmAnd.so` | when `nv_navigation` restarts | `deploy/backend/deploy_backend.sh` |
| Provider + config (`navigation.ifs`) | `/datastore/navicore/navigation.ifs`, mounted at boot by the boot overlay | next boot (one reboot) | `deploy/navigation-ifs/deploy_navigation_ifs.sh` |

Most changes are backend-only (UI, map, camera, guidance). Rebuild and
redeploy `navigation.ifs` only when `src/`, `include/` (the provider side of
the ABI) or the config change.

The provider loads the backend by its fixed file name,
`libOpenNaviBackendOsmAnd.so` (renamed from `libMockNavBackendOsmAnd.so`,
2026-10-15): a provider image built after that rename needs the backend under
the new name already on the unit before its first boot. Install the backend
first, then the image, then the one reboot.

## 0. Before touching the unit

- Build first (skill `build-open-navicore`): `make test` and `make` (and
  `make ifs` for a provider change) pass.
- Follow the workspace skill `record-unit-modification`: confirm the unit's
  identity, write the ledger entry first (`pending restart`), update it
  after the check.
- Bench deploy, service restarts and the one documented reboot are covered
  by the standing authorization in the workspace `AGENTS.md`. A second
  reboot is not: ask. Never loop a reboot.
- Settings for both scripts: `RADIO_HOST` (default the bench unit),
  `RADIO_SSH_KEY` (default `$HOME/priv_openssh`), `RADIO_SYNC` (the unit's
  flush helper; the unit has no `sync`). See each script's header.

## 1. Backend

```sh
deploy/backend/deploy_backend.sh build/qnx/libOpenNaviBackendOsmAnd.so <suffix> --running
```

`<suffix>` names the one-time backup (`.pre-<suffix>`), e.g. a short commit
id. It stages, verifies by read-back, backs up, swaps (unlink + copy while
`nv_navigation` has it mapped) and verifies, restoring on a mismatch. It needs
3x the file size free (`RADIO_FSFREE` helper; empty skips the check).

Activate without a reboot: restart the navigation process; System Manager
relaunches it and the provider loads the new backend:

```sh
ssh <unit> '/bin/slay -s TERM -f nv_navigation'
```

## 2. Provider image (navigation.ifs)

```sh
deploy/navigation-ifs/deploy_navigation_ifs.sh build/qnx/ifs/navigation.ifs \
    <ifs-build>/bin/ifs-deploy --dry-run     # plan only
deploy/navigation-ifs/deploy_navigation_ifs.sh build/qnx/ifs/navigation.ifs \
    <ifs-build>/bin/ifs-deploy
```

It uploads with ifs-build's `ifs-deploy` as `navigation.ifs.new` (read-back
verified), keeps the current image (local `ifs-deploy-backups/` and
`navigation.pre-<stamp>.ifs` on the unit), then swaps by `rm` + `cp -p`.
Never copy over a mounted IFS in place (a truncated in-place copy once made
`mountifs` crash at boot). Do **not** use ifs-build's generic `deploy-ifs`
target here: it uploads to `/datastore/ifs-overrides`, which the
navigation hook does not read.

Then one reboot: flush, then the documented software reboot (workspace
skill `dev-tools-on-radio`; the unit's reboot helper). ssh returns in about
a minute.

## 3. Verify

- `/bin/cat /datastore/boot_overlay.log`: the last block has
  `open_navicore_mount_rc=0` and `open_navicore_config_link_rc=0`.
- `nv_navigation` maps `/open-navicore/libNaviCore.so` and the backend:
  `/proc/boot/pidin -p <pid> mem` (pid from `/proc/boot/pidin -f an`).
- Backend trace: `/bin/cat /tmp/open-navicore-osmand.log` (start, attach,
  `surface_created=...`, `input=...` platform inputs,
  `cluster=publish_status`).
- On screen: hold the day/night button 1.5 s in free drive for the
  developer menu (build commit, frame rate, raw car inputs, cluster).
- Hash what you installed by scp read-back (the unit has no sha256sum
  unless dev-tools is mounted).
- Update the ledger entry to `active`, or record what failed.

## 4. Rollback

- Backend: copy the `.pre-<suffix>` backup back with the same script
  (`ACTIVE` unchanged, the backup as LOCAL after pulling it), or by hand:
  stage, `rm`, `cp -p`, restart `nv_navigation`.
- Provider image: `rm navigation.ifs; cp -p navigation.pre-<stamp>.ifs
  navigation.ifs`, flush, reboot.
- Back to the native navigation: `touch /datastore/navicore/disabled` and
  reboot (the boot hook leaves the factory config in place).
- No ssh: recovery SD (physical; ask the operator).

## Radio facts

No `ls`, `head`, `df`, `mv`, `md5sum`, `sha256sum`, `chmod`, `sync` on the
PATH; present: `/bin/cat`, `/bin/cp`, `/bin/rm`, `/proc/boot/pidin`,
`/bin/slay`, shell globs. ssh needs `-oHostKeyAlgorithms=+ssh-dss
-oPubkeyAcceptedAlgorithms=+ssh-rsa -i <key>`, scp needs `-O`; every
connection prints a `welcome` banner. The unit's clock is unset: note host
time around each action in the ledger.
