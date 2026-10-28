---
name: deploy-open-navicore
description: Install Open NaviCore on the VP4R bench radio (vp4r layer, A/B slots) - the OsmAnd backend as a plain file in the slot's data dir (deploy_backend.sh), the small provider image 50-navicore.ifs in the slot's ifs dir; staged upload, rename swap, one reboot; verification and rollback. Use when deploying a new build, checking what is installed, or rolling back.
---

# Deploy Open NaviCore to the radio

Checked on the bench unit 2026-10-28. The unit boots through the vp4r layer
(`/datastore/vp4r`, `vp4r-loader`): it picks the A/B slot in
`/datastore/vp4r/slot` (`a` or `b`) and mounts `slots/<slot>/ifs/NN-*.ifs` at `/`.
The navigation is two kinds of file per slot:

| On the unit | What | Installed by |
|---|---|---|
| `/datastore/vp4r/slots/<slot>/data/navicore/` | plain files: the backend `libOpenNaviBackendOsmAnd.so`, `libOsmAndCore_shared.so`, `libOsmAndCore_ResourcesBundle_shared.so`; the image's `50-navicore.pre` links them into `/open-navicore` at every boot (log `/datastore/vp4r/log/navicore.log`: `link <file> rc=0`) | backend: `deploy/navigation-ifs/deploy_backend.sh`; core and bundle by hand (they rarely change) |
| `/datastore/vp4r/slots/<slot>/ifs/50-navicore.ifs` (~0.8 MB) | `make ifs`'s `navigation.ifs`: provider `libNaviCore.so`, its `nv_navigation.cfg` (also at `/pas/scfg`), `boot.sh`, `/etc/vp4r/init.d/50-navicore.pre`; mounted at `/` | not scripted yet, see 1b |
| `/datastore/navicore-backend/` | data only: `maps/*.obf`, `recents.tsv`, `vectorcache/`, the bench marker files (`osmand-turns`, `disable-ch`, ...) | by hand (maps), or written by the backend |

The OsmAnd libraries are out of the image since 2026-10-24: images are mounted
inside the boot barrier and a 100 MB image made boots fail the RH850's J6 startup
window (`50-navicore.pre` header). **Stale:** `/datastore/navicore/navigation.ifs`
(a 110 MB full image of 2026-10-21) is no longer booted, and
`deploy_navigation_ifs.sh` still installs to that path; do not use it for the slot.

Most changes (UI, map, routing, turn instructions: everything in
`backends/osmand`) are the backend only. The image changes only with the provider
(`src/`), its config, `boot.sh` or `50-navicore.pre`.

## 0. Before touching the unit

- Build first (skill `build-open-navicore`): `make test`, `make` (and `make ifs`
  for an image). `make ifs` also writes `build/qnx/navicore-data/navicore/` (the
  three libraries).
- Follow the workspace skill `record-unit-modification`: confirm the unit's
  identity, write the ledger entry first (`pending restart`), update it
  after the check.
- Bench deploy and the one documented reboot are covered by the standing
  authorization in the workspace `AGENTS.md`. A second reboot is not: ask.
  Never loop a reboot. Other sessions may be using the unit: ask before rebooting.
- The unit needs dev-tools mounted (`/usr/bin/bench_rename`,
  `/usr/bin/vp4r-sync`, `/usr/bin/vp4r-reboot`, `/usr/bin/sha256sum`).
- Space: a backend is ~31 MB and each install keeps the previous one as
  `libOpenNaviBackendOsmAnd.before-<stamp>.so`; delete old ones when space runs low.

## 1a. Install the backend (the usual case)

```sh
deploy/navigation-ifs/deploy_backend.sh build/qnx/navicore-data/navicore/libOpenNaviBackendOsmAnd.so --dry-run
deploy/navigation-ifs/deploy_backend.sh build/qnx/navicore-data/navicore/libOpenNaviBackendOsmAnd.so
```

It reads the booted slot, uploads (`scp -O`) as `libOpenNaviBackendOsmAnd.so.new`,
checks its sha256 on the unit, renames the active file to
`libOpenNaviBackendOsmAnd.before-<stamp>.so` and the new one into place (no copies),
flushes and reads back the hash. Then one reboot: `/usr/bin/vp4r-sync`, then
`/usr/bin/vp4r-reboot` (workspace skill `dev-tools-on-radio`). ssh returns in
about a minute. It installs into the booted slot only; the other slot keeps its
own files.

## 1b. Install the provider image

Not scripted for the slot layout yet. By hand, the same pattern: upload
`build/qnx/ifs/navigation.ifs` as `slots/<slot>/ifs/50-navicore.ifs.new`, check its
sha256 on the unit, `bench_rename` the active `50-navicore.ifs` away (keep it),
`bench_rename` the new one into place, `vp4r-sync`, one reboot. Never copy over a
mounted IFS in place (a truncated in-place copy once made `mountifs` crash at
boot); renaming is safe. Do **not** use ifs-build's generic `deploy-ifs` target.

## 2. Verify

- `/bin/cat /datastore/vp4r/log/navicore.log`: the last three lines are
  `link <library> rc=0`; `/bin/cat /datastore/vp4r/log/boot.log` for the mounts.
- `ls -l /open-navicore/` (dev-tools `ls`): the three libraries are links into
  `slots/<slot>/data/navicore/`; `nv_navigation` maps them:
  `/proc/boot/pidin -p <pid> mem` (pid from `/proc/boot/pidin -f an`).
- Provider trace `/bin/cat /tmp/open-navi.log`; backend trace
  `/bin/cat /tmp/open-navicore-osmand.log` (start, attach,
  `init=resources`, `surface_created=...`, `cluster=publish_status`; per route
  `worker=turns source=organic_maps`, `worker=omaps turns=N ms=M`, or
  `worker=omaps_error why=...`).
- On screen: hold the day/night button 1.5 s in free drive for the
  developer menu (build commit, frame rate, raw car inputs, cluster).
- Hash on the unit: `/usr/bin/sha256sum
  /datastore/vp4r/slots/<slot>/data/navicore/libOpenNaviBackendOsmAnd.so` equals
  the host's.
- Update the ledger entry to `active`, or record what failed.

## 3. Rollback

- Previous backend: on the unit, in `slots/<slot>/data/navicore/`,
  `/usr/bin/bench_rename libOpenNaviBackendOsmAnd.so libOpenNaviBackendOsmAnd.bad.so`
  and the wanted `libOpenNaviBackendOsmAnd.before-<stamp>.so` back to
  `libOpenNaviBackendOsmAnd.so`, flush, reboot.
- Turn instructions from OsmAnd instead of Organic Maps, no reinstall:
  `touch /datastore/navicore-backend/osmand-turns`, restart the navigation.
- Without the three library files the provider keeps its built-in backend
  (`50-navicore.pre`).
- Back to the native navigation: the old marker `touch
  /datastore/navicore/disabled` was read by the pre-vp4r boot hook; not
  checked with the vp4r layer (2026-10-28).
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
