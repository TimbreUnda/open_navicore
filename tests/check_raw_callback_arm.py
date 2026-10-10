#!/usr/bin/env python3
from __future__ import annotations

import re
import subprocess
import sys

if len(sys.argv) != 4:
    raise SystemExit("usage: check_raw_callback_arm.py OBJDUMP COMMON.O SLOTS.O")

def dump(path: str) -> str:
    return subprocess.run([sys.argv[1], "-dr", path], check=True,
                          text=True, stdout=subprocess.PIPE).stdout

common = dump(sys.argv[2])
slots = dump(sys.argv[3])
labels = re.findall(r"<mock_raw_cb_\d{3}_[^>]+>:", slots)
if len(labels) != 199:
    raise SystemExit(f"expected 199 disassembled slot stubs, got {len(labels)}")
if slots.count("push\t{ip, lr}") != 199:
    raise SystemExit("not every ARM slot stub preserves ip/LR as two words")
if slots.count("R_ARM_JUMP24\tmock_raw_callback_enter") != 199:
    raise SystemExit("not every slot stub has a direct shared-entry relocation")
for needle in ("push\t{r0, r1, r2, r3}", "mrs\tr2, CPSR", "msr\tCPSR_f, r0",
               "pop\t{r0, r1, r2, r3}", "pop\t{ip, lr}", "bx\tip",
               "push\t{r0, r1, r2, r3, ip, lr}", "bx\tlr"):
    if needle not in common:
        raise SystemExit("common ARM trampoline missing: " + needle)
if "sub\tsp, sp, #8" not in common or "add\tsp, sp, #8" not in common:
    raise SystemExit("entry frame is not balanced/aligned")
if "add\tr0, sp, #24" not in common:
    raise SystemExit("return thunk does not recover exact entry SP")
print("raw callback ARM ABI: 199 stubs; entry/return preservation verified")
