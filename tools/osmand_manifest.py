#!/usr/bin/env python3
"""Inventory an external OsmAnd binary set; not a source reconstruction recipe."""

import argparse
import hashlib
import json
import pathlib
import subprocess


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            result.update(chunk)
    return {"path": str(path.resolve()), "sha256": result.hexdigest(),
            "bytes": path.stat().st_size}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("root", "core-lib-dir", "qt-prefix", "compiler", "plugin", "output",
                   "qnx-host", "qnx-target", "bundle-id", "config-key"):
        parser.add_argument("--" + option, required=True)
    args = parser.parse_args()
    root = pathlib.Path(args.root)
    core = pathlib.Path(args.core_lib_dir)
    qt = pathlib.Path(args.qt_prefix)

    def compiler(*options):
        return subprocess.check_output([args.compiler, *options], text=True).strip()

    files = {
        "plugin": pathlib.Path(args.plugin),
        "core": core / "libOsmAndCore_shared.so",
        "resources": core / "libOsmAndCore_ResourcesBundle_shared.so",
        "compiler": pathlib.Path(args.compiler),
        "cc1plus": pathlib.Path(compiler("-print-prog-name=cc1plus")),
        "libstdc++": pathlib.Path(compiler("-print-file-name=libstdc++.a")),
        "libgcc": pathlib.Path(compiler("-print-libgcc-file-name")),
    }
    # Bounded, explicit inventory: installed Qt archives and source files whose
    # semantics this adapter relies on. No recursive external-workspace search.
    for path in sorted((qt / "lib").glob("*.a")):
        files["qt/" + path.name] = path
    for name in ("src/OsmAndCore.cpp", "src/Map/MapRenderer.cpp",
                 "src/Map/AtlasMapRenderer.cpp",
                 "src/Map/OpenGL/AtlasMapRenderer_OpenGL.cpp"):
        files["core-source/" + name] = root / "source/core" / name
    for name in ("as", "ld"):
        files["qnx/" + name] = pathlib.Path(args.qnx_host) / "usr/bin" / (
            "arm-unknown-nto-qnx6.6.0eabi-" + name)
    for name in ("libc.so", "libEGL.so", "libGLESv2.so", "libm.so"):
        directory = "armle-v7/lib" if name in ("libc.so", "libm.so") else "armle-v7/usr/lib"
        files["qnx/" + name] = pathlib.Path(args.qnx_target) / directory / name
    manifest = {
        "schema": 1, "bundle_id": args.bundle_id, "config_key": args.config_key,
        "compiler_version": compiler("--version"),
        "compiler_target": compiler("-dumpmachine"),
        "artifacts": {name: digest(path) for name, path in files.items()},
        "qualification": "offline inventory only; no compatibility or bench qualification implied",
        "required_companion_evidence": [
            "source revisions or source archives, all local patches and build scripts",
            "Qt configuration, SDK version, compiler build recipe and complete link flags",
            "map region/date/license/hash, resource bundle provenance, fonts/licenses/hashes",
            "provider/plugin/core deployment set and stationary bench qualification record",
        ],
    }
    output = pathlib.Path(args.output)
    output.write_text(json.dumps(manifest, indent=2) + "\n")
    print("OsmAnd bundle manifest:", output)


if __name__ == "__main__":
    main()
