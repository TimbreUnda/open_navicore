#!/usr/bin/env python3
"""Builds navigation.ifs: Open NaviCore as the default navigation provider.

  build_navigation_ifs.py PROVIDER STOCK_CFG OUT_DIR [MKIFS]

PROVIDER   the Open NaviCore provider (dist/libNaviCore.so, `make`).
STOCK_CFG  the unit's factory /pas/scfg/nv_navigation.cfg (not committed;
           take it from the firmware dump or a read-only copy of the unit).
OUT_DIR    new directory for navigation.ifs, its build inputs and
           SHA256SUMS.
MKIFS      the QNX 6.6 mkifs wrapper (default: $QNX_MKIFS_TOOLKIT/mkifs.sh).

The image is mounted at / by the boot hook (patch_boot_overlay.py) and adds
only paths under /open-navicore/:

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
import subprocess
import sys

STOCK_CFG_SHA256 = 'c889c2b4c5d1d5ddb4b32c0c79a46c8482950ee0db783a83b1ed949aef7d680e'
MOUNT = '/open-navicore'
PROVIDER_PATH = MOUNT + '/libNaviCore.so'
EXPORTS = (b'panaVersionCheck', b'panaInitialize', b'panaGetSoftwareInfrastructure',
           b'panaDeinitialize')


def sha256(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    provider, stock_cfg, out = sys.argv[1:4]
    toolkit = os.environ.get('QNX_MKIFS_TOOLKIT', '')
    mkifs = sys.argv[4] if len(sys.argv) > 4 else os.path.join(toolkit, 'mkifs.sh')
    if not os.path.exists(mkifs):
        raise SystemExit('set QNX_MKIFS_TOOLKIT to the mkifs toolkit (or pass MKIFS)')
    if os.path.exists(out):
        raise SystemExit('refusing to overwrite existing ' + out)

    # Inputs: the reviewed factory config and an ARM provider with the four
    # PAS entry points (the same checks as deploy/prepare_bundle.sh).
    if sha256(stock_cfg) != STOCK_CFG_SHA256:
        raise SystemExit('unexpected factory nv_navigation.cfg revision')
    blob = open(provider, 'rb').read()
    if blob[:4] != b'\x7fELF' or blob[4] != 1 or blob[18] != 40:
        raise SystemExit('provider is not an ELF32 ARM object')
    missing = [e.decode() for e in EXPORTS if e not in blob]
    if missing:
        raise SystemExit('provider lacks exports: ' + ', '.join(missing))

    # The config differs from stock in NaviDllName only; CRLF is kept.
    text = open(stock_cfg, 'rb').read()
    lines = text.split(b'\r\n')
    hits = [i for i, l in enumerate(lines) if l.startswith(b'NaviDllName=')]
    if len(hits) != 1:
        raise SystemExit('expected exactly one NaviDllName line')
    lines[hits[0]] = b'NaviDllName=' + PROVIDER_PATH.encode()
    cfg = b'\r\n'.join(lines)

    os.makedirs(out)
    payload = os.path.join(out, 'payload')
    os.makedirs(payload)
    open(os.path.join(payload, 'nv_navigation.cfg'), 'wb').write(cfg)
    open(os.path.join(payload, 'libNaviCore.so'), 'wb').write(blob)
    marker = ('open-navicore navigation v1\nprovider_sha256=%s\nconfig_sha256=%s\n'
              % (hashlib.sha256(blob).hexdigest(), hashlib.sha256(cfg).hexdigest()))
    open(os.path.join(payload, 'marker.txt'), 'w').write(marker)
    # The unit's mountifs rejects very small images; keep it above 16 KiB.
    open(os.path.join(payload, 'padding.txt'), 'w').write('.' * 16384)

    build = os.path.join(out, 'navigation.build')
    open(build, 'w').write(
        '# Open NaviCore navigation provider: nonbootable secondary IFS,\n'
        '# mounted at / by the boot hook; every path is under %s.\n'
        '[-bigendian -compress -autolink -followlink -optional]\n'
        '[uid=0 gid=0]\n\n'
        '[perms=0555 +raw] %s/libNaviCore.so=payload/libNaviCore.so\n'
        '[perms=0444] %s/nv_navigation.cfg=payload/nv_navigation.cfg\n'
        '[perms=0444] %s/.open_navicore_navigation_v1=payload/marker.txt\n'
        '[perms=0400] %s/.ifs_padding.txt=payload/padding.txt\n'
        % (MOUNT, MOUNT, MOUNT, MOUNT, MOUNT))
    image = os.path.join(out, 'navigation.ifs')
    subprocess.run([mkifs, '-nn', 'navigation.build', 'navigation.ifs'], cwd=out, check=True)

    sums = ['%s  %s' % (sha256(os.path.join(out, name)), name)
            for name in ('navigation.ifs', 'navigation.build')]
    sums += ['%s  payload/%s' % (sha256(os.path.join(payload, name)), name)
             for name in ('libNaviCore.so', 'nv_navigation.cfg', 'marker.txt')]
    open(os.path.join(out, 'SHA256SUMS'), 'w').write('\n'.join(sums) + '\n')
    print('\n'.join(sums))
    print('image bytes', os.path.getsize(image))


if __name__ == '__main__':
    main()
