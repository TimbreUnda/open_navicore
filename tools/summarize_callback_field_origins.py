#!/usr/bin/env python3
"""Collapse address-specific callback receiver origins into work groups."""

import argparse
import csv
import hashlib
import re
from collections import defaultdict


FIELDS = [
    "origin_status", "shape_group", "candidate_count", "slot_counts",
    "example_callsite", "origin_shape", "next_action",
]


def normalize(origin):
    # Function/callsite addresses are context evidence in the detailed file,
    # but are deliberately removed from the repeated structural signature.
    origin = re.sub(r"(?<![0-9a-f])[0-9a-f]{8}(?![0-9a-f])", "ADDR", origin)
    return re.sub(r"0x(?:ffff)?[0-9a-f]{6,}", "LONG", origin)


def group_id(status, shape):
    return hashlib.sha256((status + "\n" + shape).encode()).hexdigest()[:16]


def action(status, shape):
    if status == "unresolved_machine_only":
        return (
            "recover a CALLIND receiver varnode or continue the ARM value through "
            "the enclosing indirect-call/constructor context"
        )
    if "indirect-return" in shape:
        return (
            "resolve the indirect factory return to a concrete provider vtable or "
            "a registered CI proxy field"
        )
    if "indirect-entry" in shape or "entry-unreferenced" in shape:
        return (
            "resolve the enclosing vtable/member-pointer owner, then trace its "
            "receiver field initialization"
        )
    if "cycle:" in shape:
        return (
            "split recursive/loop-carried contexts and prove each incoming object "
            "constructor or registered CI field"
        )
    if "unrecovered-call" in shape:
        return (
            "recover the direct call argument mapping before making an ownership "
            "classification"
        )
    return (
        "continue the preserved field path to a concrete constructor/vtable or an "
        "exact registered CI callback field"
    )


def summarize(rows):
    groups = defaultdict(list)
    for row in rows:
        shape = normalize(row["origins"])
        groups[(row["origin_status"], shape)].append(row)
    output = []
    for (status, shape), members in sorted(
            groups.items(), key=lambda item: (-len(item[1]), item[0])):
        slots = defaultdict(int)
        for row in members:
            slots[int(row["slot"])] += 1
        output.append({
            "origin_status": status,
            "shape_group": group_id(status, shape),
            "candidate_count": str(len(members)),
            "slot_counts": ";".join(
                "%d:%d" % item for item in sorted(slots.items())
            ),
            "example_callsite": members[0]["callsite"],
            "origin_shape": shape,
            "next_action": action(status, shape),
        })
    return output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("output")
    args = parser.parse_args()
    with open(args.input, newline="") as stream:
        rows = list(csv.DictReader(stream))
    with open(args.output, "w", newline="") as stream:
        writer = csv.DictWriter(stream, FIELDS)
        writer.writeheader()
        writer.writerows(summarize(rows))


if __name__ == "__main__":
    main()
