#!/usr/bin/env python3
"""Turn a drained raw callback record into an ARM replay starting fixture.

The raw capture intentionally contains no pointee bytes.  The resulting
fixture therefore faults at the first uncaptured object access until the user
adds the transitive object graph; that fault is evidence, not emulation magic.
"""

from __future__ import annotations

import argparse
import copy
import csv
import json
import pathlib
import struct

MAGIC = 0x4D524342
VERSION = 1
SIZE = 136


def u16(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def records(path: pathlib.Path) -> list[dict[str, object]]:
    data = path.read_bytes()
    if len(data) % SIZE:
        raise ValueError(f"raw file size {len(data)} is not a multiple of {SIZE}")
    result = []
    for off in range(0, len(data), SIZE):
        chunk = data[off:off + SIZE]
        if u32(chunk, 0) != MAGIC or u16(chunk, 4) != VERSION or u16(chunk, 6) != SIZE:
            raise ValueError(f"invalid record header at file offset {off}")
        result.append({
            "sequence": u32(chunk, 8), "binary_id": chunk[12:44].hex(),
            "adapter": u32(chunk, 44), "slot": u16(chunk, 48),
            "interface_id": u16(chunk, 50), "flags": u32(chunk, 52),
            "r": [u32(chunk, 56 + i * 4) for i in range(4)],
            "sp": u32(chunk, 72), "lr": u32(chunk, 76),
            "cpsr": u32(chunk, 80), "target_pc": u32(chunk, 84),
            "original_self": u32(chunk, 88), "stack_count": u32(chunk, 92),
            "stack": [u32(chunk, 96 + i * 4) for i in range(8)],
            "return_r0": u32(chunk, 128), "return_r1": u32(chunk, 132),
        })
    return result


def interface_names(path: pathlib.Path | None) -> dict[int, str]:
    if path is None:
        return {}
    names: dict[str, int] = {}
    for row in csv.DictReader(path.open(newline="", encoding="utf-8")):
        if row["slot_kind"] == "pure_virtual_application":
            names.setdefault(row["interface"], len(names))
    return {number: name for name, number in names.items()}


def convert(record: dict[str, object], template: dict[str, object], name: str) -> dict[str, object]:
    result = copy.deepcopy(template)
    expected = str(result["binary"]["sha256"]).lower()
    if expected != record["binary_id"]:
        raise ValueError("template binary SHA-256 does not match record binary_id")
    sp = int(record["sp"])
    page = sp & ~0xFFF
    stack = list(record["stack"])
    alloc = {
        "name": "raw_callback_stack", "graph": "raw_callback_stack",
        "address": hex(page), "size": "0x2000", "permissions": "rw",
        "words": [{"offset": hex(sp - page + i * 4), "value": hex(value)}
                  for i, value in enumerate(stack[:int(record["stack_count"])])],
    }
    result["allocations"] = [x for x in result.get("allocations", [])
                             if x.get("name") != "raw_callback_stack"] + [alloc]
    regs = result.setdefault("registers", {})
    for index, value in enumerate(record["r"]):
        regs[f"r{index}"] = hex(int(value))
    regs["sp"] = hex(sp)
    regs["lr"] = result["execution"]["stop_pc"]
    regs["cpsr"] = hex(int(record["cpsr"]))
    target = int(record["target_pc"])
    result["execution"]["entry"] = hex(target & ~1)
    result["execution"]["isa"] = "thumb" if target & 1 else "arm"
    result["raw_callback_capture"] = {
        "schema": "mock-nav-raw-callback/v1", "sequence": record["sequence"],
        "adapter": "root" if record["adapter"] == 0xFFFFFFFF else record["adapter"],
        "slot": record["slot"], "interface_id": record["interface_id"],
        "interface": name, "captured_lr": hex(int(record["lr"])),
        "captured_return_r0": hex(int(record["return_r0"])),
        "captured_return_r1": hex(int(record["return_r1"])),
        "note": "Pointee graph is intentionally absent; add guarded allocations from a snapshot.",
    }
    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("raw", type=pathlib.Path)
    ap.add_argument("template", type=pathlib.Path)
    ap.add_argument("output", type=pathlib.Path)
    ap.add_argument("--sequence", type=lambda x: int(x, 0))
    ap.add_argument("--manifest", type=pathlib.Path)
    ns = ap.parse_args()
    found = records(ns.raw)
    if ns.sequence is None:
        if len(found) != 1:
            raise SystemExit("select one record with --sequence")
        record = found[0]
    else:
        matches = [x for x in found if x["sequence"] == ns.sequence]
        if len(matches) != 1:
            raise SystemExit("sequence is missing or not unique")
        record = matches[0]
    template = json.loads(ns.template.read_text(encoding="utf-8"))
    names = interface_names(ns.manifest)
    output = convert(record, template, names.get(int(record["interface_id"]), "unknown"))
    ns.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
