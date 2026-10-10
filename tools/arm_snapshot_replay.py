#!/usr/bin/env python3
"""Replay a bounded ARM/Thumb fragment from a guarded stock ELF snapshot.

This is deliberately a fragment oracle, not a QNX process emulator.  Snapshot
v1 describes core-register/stack lanes, a relocatable object graph, and named
external-call stubs.  PT_LOAD pages come from the hash-guarded original ELF.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct
import sys
import time
from typing import Any

try:
    from unicorn import (
        Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_MODE_LITTLE_ENDIAN,
        UC_HOOK_CODE, UC_HOOK_MEM_INVALID, UC_HOOK_MEM_READ,
        UC_HOOK_MEM_WRITE, UC_MEM_WRITE, UC_PROT_ALL, UC_PROT_EXEC, UC_PROT_READ,
        UC_PROT_WRITE,
    )
    from unicorn.arm_const import (
        UC_ARM_REG_CPSR, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0,
        UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4,
        UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
        UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12,
        UC_ARM_REG_SP,
    )
except ImportError as exc:  # pragma: no cover - exercised by CLI environments
    raise SystemExit("arm_snapshot_replay requires the installed 'unicorn' package") from exc


SCHEMA = "mock-nav-arm-snapshot/v1"
PAGE = 0x1000
REGS = {
    "r0": UC_ARM_REG_R0, "r1": UC_ARM_REG_R1, "r2": UC_ARM_REG_R2,
    "r3": UC_ARM_REG_R3, "r4": UC_ARM_REG_R4, "r5": UC_ARM_REG_R5,
    "r6": UC_ARM_REG_R6, "r7": UC_ARM_REG_R7, "r8": UC_ARM_REG_R8,
    "r9": UC_ARM_REG_R9, "r10": UC_ARM_REG_R10, "r11": UC_ARM_REG_R11,
    "r12": UC_ARM_REG_R12, "sp": UC_ARM_REG_SP, "lr": UC_ARM_REG_LR,
    "pc": UC_ARM_REG_PC, "cpsr": UC_ARM_REG_CPSR,
}


def parse_int(value: Any) -> int:
    if isinstance(value, int):
        return value
    return int(value, 0)


def align_down(value: int) -> int:
    return value & ~(PAGE - 1)


def align_up(value: int) -> int:
    return (value + PAGE - 1) & ~(PAGE - 1)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def elf32_loads(data: bytes) -> tuple[int, list[dict[str, int]]]:
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise ValueError("binary is not little-endian ELF32")
    machine = struct.unpack_from("<H", data, 18)[0]
    if machine != 40:
        raise ValueError(f"ELF machine is {machine}, expected ARM (40)")
    phoff = struct.unpack_from("<I", data, 28)[0]
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    loads: list[dict[str, int]] = []
    for index in range(phnum):
        off = phoff + index * phentsize
        p_type, p_offset, p_vaddr, _, p_filesz, p_memsz, p_flags, _ = \
            struct.unpack_from("<IIIIIIII", data, off)
        if p_type == 1:
            loads.append({"offset": p_offset, "vaddr": p_vaddr,
                          "filesz": p_filesz, "memsz": p_memsz,
                          "flags": p_flags})
    if not loads:
        raise ValueError("ELF has no PT_LOAD segments")
    return machine, loads


def prot_from_text(text: str) -> int:
    prot = 0
    if "r" in text:
        prot |= UC_PROT_READ
    if "w" in text:
        prot |= UC_PROT_WRITE
    if "x" in text:
        prot |= UC_PROT_EXEC
    return prot


class Replay:
    def __init__(self, snapshot_path: pathlib.Path, snapshot: dict[str, Any],
                 reg_overrides: dict[str, int], word_overrides: list[tuple[str, int, int]],
                 stub_returns: dict[str, int], relocations: dict[str, int]):
        if snapshot.get("schema") != SCHEMA:
            raise ValueError(f"unsupported snapshot schema {snapshot.get('schema')!r}")
        self.path = snapshot_path
        self.s = snapshot
        self.reg_overrides = reg_overrides
        self.word_overrides = word_overrides
        self.stub_returns = stub_returns
        self.relocations = relocations
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_LITTLE_ENDIAN)
        self.trace: list[dict[str, Any]] = []
        self.calls: list[dict[str, Any]] = []
        self.fault: dict[str, Any] | None = None
        self.instructions = 0
        self.allocs: dict[str, tuple[int, int]] = {}
        self.stubs: dict[int, dict[str, Any]] = {}
        self.stop_pc = parse_int(snapshot["execution"]["stop_pc"]) & ~1

    def resolve(self, spec: Any) -> int:
        if isinstance(spec, (int, str)):
            return parse_int(spec)
        if "pointer" in spec:
            name = spec["pointer"]
            value = self.allocs[name][0] + parse_int(spec.get("offset", 0))
        elif "stub" in spec:
            item = next(x for x in self.s["stubs"] if x["name"] == spec["stub"])
            value = parse_int(item["address"])
        else:
            raise ValueError(f"unknown value specification {spec!r}")
        if spec.get("thumb"):
            value |= 1
        return value

    def load_elf(self) -> None:
        binary = self.s["binary"]
        image_path = pathlib.Path(binary["path"])
        if not image_path.is_absolute():
            image_path = (self.path.parent / image_path).resolve()
        data = image_path.read_bytes()
        actual = sha256(data)
        expected = binary["sha256"].lower()
        if actual != expected:
            raise ValueError(f"binary SHA-256 mismatch: got {actual}, expected {expected}")
        machine, loads = elf32_loads(data)
        expected_machine = parse_int(binary.get("elf_machine", 40))
        if machine != expected_machine:
            raise ValueError(f"ELF machine mismatch: got {machine}, expected {expected_machine}")
        image_base = min(seg["vaddr"] for seg in loads)
        expected_base = parse_int(binary["elf_image_base"])
        if image_base != expected_base:
            raise ValueError(
                f"ELF image-base mismatch: got 0x{image_base:x}, expected 0x{expected_base:x}")
        bias = parse_int(binary.get("load_bias", 0))
        for seg in loads:
            start = align_down(seg["vaddr"] + bias)
            page_delta = (seg["vaddr"] + bias) - start
            size = align_up(page_delta + seg["memsz"])
            self.uc.mem_map(start, size, UC_PROT_ALL)
            self.uc.mem_write(seg["vaddr"] + bias,
                              data[seg["offset"]:seg["offset"] + seg["filesz"]])
            flags = seg["flags"]
            prot = ((UC_PROT_READ if flags & 4 else 0) |
                    (UC_PROT_WRITE if flags & 2 else 0) |
                    (UC_PROT_EXEC if flags & 1 else 0))
            self.uc.mem_protect(start, size, prot)
        self.binary_path = str(image_path)
        self.binary_hash = actual

    def map_graph(self) -> None:
        for item in self.s.get("allocations", []):
            name = item["name"]
            delta = self.relocations.get(item.get("graph", ""), 0)
            address = parse_int(item["address"]) + delta
            size = parse_int(item["size"])
            if address % PAGE or size % PAGE:
                raise ValueError(f"allocation {name} must be page aligned/sized")
            self.uc.mem_map(address, size, UC_PROT_ALL)
            fill = parse_int(item.get("fill", 0)) & 0xff
            if fill:
                self.uc.mem_write(address, bytes([fill]) * size)
            self.allocs[name] = (address, size)
        for item in self.s.get("allocations", []):
            base, size = self.allocs[item["name"]]
            for word in item.get("words", []):
                offset = parse_int(word["offset"])
                if offset < 0 or offset + 4 > size:
                    raise ValueError(f"word outside allocation {item['name']}")
                self.uc.mem_write(base + offset, struct.pack("<I", self.resolve(word["value"]) & 0xffffffff))
            self.uc.mem_protect(base, size, prot_from_text(item.get("permissions", "rw")))
        for name, offset, value in self.word_overrides:
            base, size = self.allocs[name]
            if offset < 0 or offset + 4 > size:
                raise ValueError(f"override outside allocation {name}")
            # Temporarily allow the snapshot preparation write.
            item = next(x for x in self.s["allocations"] if x["name"] == name)
            self.uc.mem_protect(base, size, UC_PROT_ALL)
            self.uc.mem_write(base + offset, struct.pack("<I", value & 0xffffffff))
            self.uc.mem_protect(base, size, prot_from_text(item.get("permissions", "rw")))
        for item in self.s.get("stubs", []):
            address = parse_int(item["address"]) & ~1
            page = align_down(address)
            try:
                self.uc.mem_map(page, PAGE, UC_PROT_READ | UC_PROT_EXEC)
            except UcError:
                pass
            self.stubs[address] = item

    def setup_registers(self) -> None:
        for name, value in self.s["registers"].items():
            if name not in REGS:
                raise ValueError(f"unknown register {name}")
            self.uc.reg_write(REGS[name], self.resolve(value) & 0xffffffff)
        for name, value in self.reg_overrides.items():
            if name not in REGS:
                raise ValueError(f"unknown override register {name}")
            self.uc.reg_write(REGS[name], value & 0xffffffff)

    def install_hooks(self) -> None:
        def code_hook(uc: Uc, address: int, size: int, _data: Any) -> None:
            address &= ~1
            if address == self.stop_pc:
                uc.emu_stop()
                return
            stub = self.stubs.get(address)
            if stub is not None:
                sp = uc.reg_read(UC_ARM_REG_SP)
                stack_words = parse_int(stub.get("capture_stack_words", 4))
                stack = [struct.unpack("<I", bytes(uc.mem_read(sp + 4 * i, 4)))[0]
                         for i in range(stack_words)]
                call = {"stub": stub["name"], "pc": address,
                        "r0": uc.reg_read(UC_ARM_REG_R0),
                        "r1": uc.reg_read(UC_ARM_REG_R1),
                        "r2": uc.reg_read(UC_ARM_REG_R2),
                        "r3": uc.reg_read(UC_ARM_REG_R3),
                        "sp": sp, "stack_words": stack}
                self.calls.append(call)
                result = self.stub_returns.get(stub["name"], parse_int(stub["return"]))
                uc.reg_write(UC_ARM_REG_R0, result & 0xffffffff)
                uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
                return
            self.instructions += 1
            self.trace.append({"kind": "instruction", "address": address, "size": size})

        def mem_hook(_uc: Uc, access: int, address: int, size: int,
                     value: int, _data: Any) -> None:
            kind = "write" if access == UC_MEM_WRITE else "read"
            self.trace.append({"kind": kind, "address": address, "size": size,
                               **({"value": value & ((1 << (size * 8)) - 1)}
                                  if kind == "write" else {})})

        def invalid_hook(_uc: Uc, access: int, address: int, size: int,
                         value: int, _data: Any) -> bool:
            self.fault = {"access": access, "address": address, "size": size,
                          "value": value}
            return False

        self.uc.hook_add(UC_HOOK_CODE, code_hook)
        self.uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, mem_hook)
        self.uc.hook_add(UC_HOOK_MEM_INVALID, invalid_hook)

    def run(self) -> dict[str, Any]:
        self.load_elf()
        self.map_graph()
        self.setup_registers()
        self.install_hooks()
        execution = self.s["execution"]
        entry = parse_int(execution["entry"])
        if execution["isa"] == "thumb":
            entry |= 1
        elif execution["isa"] != "arm":
            raise ValueError("execution.isa must be arm or thumb")
        budget = parse_int(execution.get("instruction_budget", 10000))
        timeout_us = parse_int(execution.get("timeout_us", 100000))
        started = time.monotonic_ns()
        error = None
        try:
            self.uc.emu_start(entry, self.stop_pc, timeout=timeout_us, count=budget)
        except UcError as exc:
            error = str(exc)
        elapsed_us = (time.monotonic_ns() - started) // 1000
        pc = self.uc.reg_read(UC_ARM_REG_PC) & ~1
        status = "stopped" if pc == self.stop_pc and error is None else "fault"
        if error is None and pc != self.stop_pc:
            error = "execution ended before stop_pc (instruction or time budget)"
        return {
            "schema": "mock-nav-arm-replay-result/v1",
            "snapshot": self.s.get("name", self.path.name),
            "binary": {"path": self.binary_path, "sha256": self.binary_hash},
            "status": status, "error": error, "fault": self.fault,
            "instruction_count": self.instructions, "elapsed_us": elapsed_us,
            "final_registers": {name: self.uc.reg_read(reg) & 0xffffffff
                                for name, reg in REGS.items()},
            "external_calls": self.calls, "trace": self.trace,
        }


def assignment(text: str) -> tuple[str, int]:
    name, sep, value = text.partition("=")
    if not sep:
        raise argparse.ArgumentTypeError("expected NAME=VALUE")
    return name, parse_int(value)


def word_assignment(text: str) -> tuple[str, int, int]:
    lhs, sep, value = text.partition("=")
    name, plus, offset = lhs.partition("+")
    if not sep or not plus:
        raise argparse.ArgumentTypeError("expected ALLOCATION+OFFSET=VALUE")
    return name, parse_int(offset), parse_int(value)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot", type=pathlib.Path)
    parser.add_argument("--set-reg", action="append", default=[], type=assignment)
    parser.add_argument("--set-word", action="append", default=[], type=word_assignment)
    parser.add_argument("--stub-return", action="append", default=[], type=assignment)
    parser.add_argument("--relocate", action="append", default=[], type=assignment,
                        help="relocate all allocations in GRAPH by signed DELTA")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    try:
        snapshot = json.loads(args.snapshot.read_text(encoding="utf-8"))
        result = Replay(args.snapshot.resolve(), snapshot, dict(args.set_reg),
                        args.set_word, dict(args.stub_return), dict(args.relocate)).run()
    except (OSError, ValueError, KeyError, struct.error, UcError) as exc:
        result = {"schema": "mock-nav-arm-replay-result/v1", "status": "invalid",
                  "error": str(exc)}
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    else:
        sys.stdout.write(encoded)
    return 0 if result.get("status") == "stopped" else 1


if __name__ == "__main__":
    raise SystemExit(main())
