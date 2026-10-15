#!/usr/bin/env python3
"""Keep recovered semantic seams descriptive while preserving raw identities."""

import csv
import pathlib
import re

from tests.callback_symbols import callback_symbol


ALLOWED_RAW_CALLBACKS = {
    "mock_pas_17_4_raw",
    "mock_pas_19_6_raw",
    "mock_pas_31_6_raw",
    "mock_pas_31_10_raw",
    "mock_pas_31_13_raw",
}
NUMBERED_CALLBACK = re.compile(
    r"\bmock_pas_(?:root|all|[0-9]+)_[0-9]+(?:_[A-Za-z0-9_]+)?\b"
)


def rows(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def main():
    root = pathlib.Path(__file__).resolve().parents[1]
    callback_files = [
        root / "src/include/host_callbacks.h",
        root / "src/compat/qr39/host_callbacks.c",
    ]
    numbered = set()
    for path in callback_files:
        numbered.update(NUMBERED_CALLBACK.findall(path.read_text()))
    if numbered != ALLOWED_RAW_CALLBACKS:
        raise SystemExit(
            "numbered callback seams differ from the unresolved raw allowlist: " +
            repr(sorted(numbered))
        )

    matrix = {
        (row["adapter"], row["slot"]): row
        for row in rows(
            root / "contracts/qr39/observations/callback-api-matrix.csv"
        )
    }
    for covered in rows(
            root / "contracts/qr39/callbacks/runtime-coverage.csv"):
        key = (covered["adapter"], covered["slot"])
        expected = callback_symbol(
            covered["adapter"], covered["slot"], matrix[key]["name"]
        )
        if covered["symbol"] != expected:
            raise SystemExit(
                "noncanonical runtime callback symbol for %s/%s: %s" %
                (key[0], key[1], covered["symbol"])
            )
    print("semantic symbol audit passed; five unresolved raw slots preserved")


if __name__ == "__main__":
    main()
