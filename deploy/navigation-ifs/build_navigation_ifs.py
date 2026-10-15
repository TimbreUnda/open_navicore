#!/usr/bin/env python3
"""Writes the navigation.ifs payload: Open NaviCore as the default provider.

  build_navigation_ifs.py PROVIDER STOCK_CFG PAYLOAD_DIR

PROVIDER     the Open NaviCore provider (dist/libNaviCore.so, `make`).
STOCK_CFG    the unit's factory /pas/scfg/nv_navigation.cfg (not committed;
             take it from the firmware dump or a read-only copy of the unit).
PAYLOAD_DIR  directory for the image contents (created or refreshed).

The image itself is packaged by ifs-build (CMakeLists.txt in this directory,
`cmake --build <dir> --target ifs`). It is mounted at / by the boot hook
(patch_boot_overlay.py) and adds only paths under /open-navicore/:

  /open-navicore/libNaviCore.so      the provider
  /open-navicore/nv_navigation.cfg   the stock config with NaviDllName
                                     pointing at the provider above
  /open-navicore/.open_navicore_navigation_v1   marker checked at boot

The backend plugin, OsmAnd, resources and maps stay in
/datastore/navicore-backend (the provider's fixed plugin path), so they can be
updated without rebuilding this image.
"""
import hashlib
import os
import sys

STOCK_CFG_SHA256 = 'c889c2b4c5d1d5ddb4b32c0c79a46c8482950ee0db783a83b1ed949aef7d680e'
PROVIDER_PATH = '/open-navicore/libNaviCore.so'
EXPORTS = (b'panaVersionCheck', b'panaInitialize', b'panaGetSoftwareInfrastructure',
           b'panaDeinitialize')


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def write_if_changed(path, data):
    # Unchanged files keep their time stamps, so mkifs does not rerun.
    if os.path.exists(path) and open(path, 'rb').read() == data:
        return
    with open(path, 'wb') as f:
        f.write(data)


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    provider, stock_cfg, payload = sys.argv[1:4]

    # Inputs: the reviewed factory config and an ARM provider with the four
    # PAS entry points (the same checks as deploy/prepare_bundle.sh).
    text = open(stock_cfg, 'rb').read()
    if sha256(text) != STOCK_CFG_SHA256:
        raise SystemExit('unexpected factory nv_navigation.cfg revision')
    blob = open(provider, 'rb').read()
    if blob[:4] != b'\x7fELF' or blob[4] != 1 or blob[18] != 40:
        raise SystemExit('provider is not an ELF32 ARM object')
    missing = [e.decode() for e in EXPORTS if e not in blob]
    if missing:
        raise SystemExit('provider lacks exports: ' + ', '.join(missing))

    # The config differs from stock in NaviDllName only; CRLF is kept.
    lines = text.split(b'\r\n')
    hits = [i for i, l in enumerate(lines) if l.startswith(b'NaviDllName=')]
    if len(hits) != 1:
        raise SystemExit('expected exactly one NaviDllName line')
    lines[hits[0]] = b'NaviDllName=' + PROVIDER_PATH.encode()
    cfg = b'\r\n'.join(lines)

    os.makedirs(payload, exist_ok=True)
    marker = ('open-navicore navigation v1\nprovider_sha256=%s\nconfig_sha256=%s\n'
              % (sha256(blob), sha256(cfg))).encode()
    files = {
        'libNaviCore.so': blob,
        'nv_navigation.cfg': cfg,
        'marker.txt': marker,
        # The unit's mountifs rejects very small images; keep it above 16 KiB.
        'padding.txt': b'.' * 16384,
    }
    for name, data in files.items():
        write_if_changed(os.path.join(payload, name), data)
    sums = ''.join('%s  %s\n' % (sha256(data), name) for name, data in files.items())
    write_if_changed(os.path.join(payload, 'SHA256SUMS'), sums.encode())
    sys.stdout.write(sums)


if __name__ == '__main__':
    main()
