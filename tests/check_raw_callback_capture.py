#!/usr/bin/env python3
from __future__ import annotations

import csv
import json
import pathlib
import re
import struct
import subprocess
import sys
import tempfile


def fail(message: str) -> None:
    raise SystemExit("raw callback capture check: " + message)


root = pathlib.Path(__file__).resolve().parents[1]
manifest = root / "contracts/qr39/callbacks/proxy-slot-manifest.csv"
rows = [r for r in csv.DictReader(manifest.open(newline="", encoding="utf-8"))
        if r["slot_kind"] == "pure_virtual_application"]
if len(rows) != 199 or len({r["interface"] for r in rows}) != 25:
    fail("manifest is not 199 application slots across 25 interfaces")

with tempfile.TemporaryDirectory() as directory:
    temp = pathlib.Path(directory)
    subprocess.run([sys.executable, str(root / "tools/generate_raw_callback_trampolines.py"),
                    str(manifest), str(temp / "slots.S"), str(temp / "table.c")],
                   check=True, stdout=subprocess.DEVNULL)
    for generated, checked in ((temp / "slots.S", root / "src/mock_raw_callback_slots_arm.S"),
                               (temp / "table.c", root / "src/mock_raw_callback_slot_table.c")):
        if generated.read_bytes() != checked.read_bytes():
            fail(f"stale generated file {checked.name}")

    binary_id = bytes.fromhex("11" * 32)
    raw = bytearray(136)
    struct.pack_into("<IHHI", raw, 0, 0x4D524342, 1, 136, 7)
    raw[12:44] = binary_id
    struct.pack_into("<IHHI", raw, 44, 25, 19, 4, 7)
    struct.pack_into("<4I", raw, 56, 1, 2, 3, 4)
    struct.pack_into("<5I", raw, 72, 0x70000800, 0x1234, 0x600001D3,
                     0x80001001, 1)
    struct.pack_into("<I", raw, 92, 8)
    struct.pack_into("<8I", raw, 96, *range(8))
    struct.pack_into("<2I", raw, 128, 9, 10)
    (temp / "capture.raw").write_bytes(raw)
    template = {
        "schema": "mock-nav-arm-snapshot/v1",
        "binary": {"path": "pas.so", "sha256": binary_id.hex(),
                   "elf_machine": 40, "elf_image_base": "0x0"},
        "execution": {"entry": "0x0", "isa": "arm",
                      "stop_pc": "0x23000000"},
        "registers": {}, "allocations": [], "stubs": [],
    }
    (temp / "template.json").write_text(json.dumps(template), encoding="utf-8")
    subprocess.run([sys.executable, str(root / "tools/raw_callback_to_replay.py"),
                    str(temp / "capture.raw"), str(temp / "template.json"),
                    str(temp / "replay.json"), "--manifest", str(manifest)],
                   check=True)
    replay = json.loads((temp / "replay.json").read_text(encoding="utf-8"))
    if replay["execution"] != {"entry": "0x80001000", "isa": "thumb",
                               "stop_pc": "0x23000000"}:
        fail("raw record converter did not preserve entry ISA")
    if replay["registers"]["r3"] != "0x4" or \
            replay["raw_callback_capture"]["interface"] != "SI_VoiceRecognition":
        fail("raw record converter lost register/interface identity")

assembly = (root / "src/mock_raw_callback_slots_arm.S").read_text(encoding="utf-8")
if len(re.findall(r"^mock_raw_cb_\d{3}_.*:$", assembly, re.M)) != 199:
    fail("generated assembly does not contain 199 labels")
if assembly.count("stmdb sp!, {r12, lr}") != 199:
    fail("a per-slot stub does not preserve incoming r12/LR")
if assembly.count("b mock_raw_callback_enter") != 199:
    fail("a per-slot stub does not branch to the shared entry")

common = (root / "src/mock_raw_callback_trampolines_arm.S").read_text(encoding="utf-8")
for required in ("stmdb sp!, {r0-r3}", "mrs r2, cpsr", "msr cpsr_f, r0",
                 "ldmia sp!, {r0-r3}", "ldmia sp!, {r12, lr}", "bx r12",
                 "stmdb sp!, {r0-r3, r12, lr}", "bx lr"):
    if required not in common:
        fail(f"missing preservation instruction: {required}")

capture = (root / "src/mock_raw_callback_capture.c").read_text(encoding="utf-8")
for forbidden in ("malloc(", "calloc(", "realloc(", "free(", "syslog(",
                  "printf(", "fprintf(", "pthread_mutex", "clock_gettime("):
    if forbidden in capture:
        fail(f"non-capture-safe operation present: {forbidden}")

print("raw callback capture: 199 slots/25 interfaces, generated and structurally safe")
