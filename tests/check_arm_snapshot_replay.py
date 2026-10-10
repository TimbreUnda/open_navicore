#!/usr/bin/env python3
"""Regression and negative tests for the bounded stock-fragment oracle."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "arm_snapshot_replay.py"
FIXTURE = ROOT / "fixtures" / "arm-replay" / "hmi-signal-state-v1.json"
CALLBACK = 0x20001000


def invoke(*args: str, snapshot: pathlib.Path = FIXTURE,
           want_rc: int = 0) -> dict:
    process = subprocess.run(
        [sys.executable, str(TOOL), str(snapshot), *args],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if process.returncode != want_rc:
        raise AssertionError(
            f"replay rc={process.returncode}, expected {want_rc}\n"
            f"stdout={process.stdout}\nstderr={process.stderr}")
    return json.loads(process.stdout)


def baseline() -> None:
    result = invoke()
    assert result["status"] == "stopped" and result["fault"] is None
    assert result["binary"]["sha256"] == \
        "fd1e931430cca61116c533cdf617dc805548e312a9131e972f55a7d30ab64842"
    assert result["instruction_count"] == 7
    assert len(result["external_calls"]) == 1
    call = result["external_calls"][0]
    assert call["stub"] == "hmi_slot8"
    assert (call["r0"], call["r1"], call["r2"]) == \
        (CALLBACK, 0x11111111, 0x22222222)
    assert call["stack_words"][:2] == [0x33333333, 0x23000001]
    assert result["final_registers"]["r0"] == 0x5a5aa55a
    accesses = {(row["kind"], row["address"], row["size"])
                for row in result["trace"]}
    assert ("read", 0x20000004, 4) in accesses
    assert ("read", 0x20001000, 4) in accesses
    assert ("read", 0x20002020, 4) in accesses
    assert any(row["kind"] == "write" and 0x22000000 <= row["address"] < 0x22010000
               for row in result["trace"])


def counterfactual_lanes_and_return() -> None:
    result = invoke("--set-reg", "r1=0x01020304",
                    "--set-reg", "r2=0xfedcba98",
                    "--stub-return", "hmi_slot8=0x13579bdf")
    call = result["external_calls"][0]
    assert (call["r1"], call["r2"]) == (0x01020304, 0xfedcba98)
    assert result["final_registers"]["r0"] == 0x13579bdf


def relocation_and_null_branch() -> None:
    moved = invoke("--relocate", "hmi=0x00100000")
    call = moved["external_calls"][0]
    assert call["r0"] == CALLBACK + 0x00100000
    addresses = {row["address"] for row in moved["trace"] if row["kind"] == "read"}
    assert 0x20100004 in addresses and 0x20101000 in addresses and 0x20102020 in addresses

    null = invoke("--set-word", "wrapper+0x4=0")
    assert null["external_calls"] == []
    assert null["final_registers"]["r0"] == 1


def fault_budget_and_hash_guards() -> None:
    fault = invoke("--set-reg", "r0=0x24000000", want_rc=1)
    assert fault["status"] == "fault"
    assert fault["fault"] is not None and fault["fault"]["address"] == 0x24000004

    source = json.loads(FIXTURE.read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory(prefix="mock-nav-arm-replay-") as directory:
        temp = pathlib.Path(directory)
        short = dict(source)
        short["execution"] = dict(source["execution"], instruction_budget=1)
        short_path = temp / "short.json"
        short_path.write_text(json.dumps(short), encoding="utf-8")
        # Preserve resolution of the fixture's relative binary path.
        short["binary"] = dict(source["binary"], path=str(
            (FIXTURE.parent / source["binary"]["path"]).resolve()))
        short_path.write_text(json.dumps(short), encoding="utf-8")
        limited = invoke(snapshot=short_path, want_rc=1)
        assert limited["status"] == "fault" and "budget" in limited["error"]

        original = pathlib.Path(short["binary"]["path"]).read_bytes()
        bad_binary = temp / "mutated.so"
        bad_binary.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
        bad = dict(source)
        bad["binary"] = dict(source["binary"], path=str(bad_binary))
        bad_path = temp / "bad-hash.json"
        bad_path.write_text(json.dumps(bad), encoding="utf-8")
        rejected = invoke(snapshot=bad_path, want_rc=1)
        assert rejected["status"] == "invalid" and "SHA-256 mismatch" in rejected["error"]

        bad_base = dict(source)
        bad_base["binary"] = dict(source["binary"], path=short["binary"]["path"],
                                  elf_image_base="0x10000")
        bad_base_path = temp / "bad-base.json"
        bad_base_path.write_text(json.dumps(bad_base), encoding="utf-8")
        base_rejected = invoke(snapshot=bad_base_path, want_rc=1)
        assert base_rejected["status"] == "invalid"
        assert "image-base mismatch" in base_rejected["error"]


def main() -> int:
    baseline()
    counterfactual_lanes_and_return()
    relocation_and_null_branch()
    fault_budget_and_hash_guards()
    print("arm snapshot replay: baseline, counterfactual, relocation, fault, budget, hash, and image-base guards passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
