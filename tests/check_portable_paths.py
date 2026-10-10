#!/usr/bin/env python3
"""Reject workstation-specific absolute paths in tracked project files."""

import pathlib
import subprocess


FORBIDDEN_PREFIXES = ("/" + "mnt/", "/" + "home/")


def main():
    root = pathlib.Path(__file__).resolve().parents[1]
    result = subprocess.run(
        ["git", "ls-files", "-z"], cwd=root, check=True,
        stdout=subprocess.PIPE,
    )
    violations = []
    for raw_name in result.stdout.split(b"\0"):
        if not raw_name:
            continue
        relative = raw_name.decode("utf-8", "surrogateescape")
        data = (root / relative).read_bytes()
        if b"\0" in data:
            continue
        for number, line in enumerate(
                data.decode("utf-8", "replace").splitlines(), 1):
            if any(prefix in line for prefix in FORBIDDEN_PREFIXES):
                violations.append("%s:%d:%s" % (relative, number, line))
    if violations:
        raise SystemExit(
            "workstation-specific absolute paths are forbidden:\n" +
            "\n".join(violations)
        )
    print("portable path audit passed")


if __name__ == "__main__":
    main()
