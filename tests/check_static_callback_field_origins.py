#!/usr/bin/env python3
"""Guard the context-sensitive receiver-field origin evidence and grouping."""

import copy
import csv
import hashlib
import pathlib
import sys
from collections import Counter

sys.path.insert(0, str(
    pathlib.Path(__file__).resolve().parents[1] / "tools" / "analysis"
))
from summarize_callback_field_origins import FIELDS as GROUP_FIELDS, summarize


ORIGIN_FIELDS = [
    "function", "callsite", "slot", "origin_status", "origin_group",
    "terminal_count", "origins", "evidence",
]
KEY_HASH = "fa45caa28236e5094b5704efce30b4e9697ea1680f6232a99ab2b24248b172a1"
ROW_HASH = "b146c5a92a5dea381517a73afd4931f5dd626e47dce21c91d7c82a9b9e8d50cb"
EXPECTED_STATUS = Counter({
    "unresolved_machine_only": 647,
    "traced_no_registered_ci_owner": 492,
    "proven_provider_constructed_noncallback": 2,
    "traced_local_dataflow_only": 1,
})


def validate(rows, inventory, arm_rows, groups):
    if len(rows) != 1142:
        raise ValueError("field-origin inventory must contain 1,142 equal-origin rows")
    keys = [(r["function"], r["callsite"], r["slot"]) for r in rows]
    if len(set(keys)) != len(keys):
        raise ValueError("duplicate field-origin callsite")
    digest = hashlib.sha256("".join("%s,%s,%s\n" % k for k in keys).encode()).hexdigest()
    if digest != KEY_HASH:
        raise ValueError("field-origin target key set/order changed")
    row_digest = hashlib.sha256("".join(
        "%s,%s,%s,%s,%s\n" % (
            r["function"], r["callsite"], r["slot"],
            r["origin_status"], r["origin_group"]
        ) for r in rows
    ).encode()).hexdigest()
    if row_digest != ROW_HASH:
        raise ValueError("field-origin status/group evidence changed")
    if Counter(r["origin_status"] for r in rows) != EXPECTED_STATUS:
        raise ValueError("field-origin status split changed")
    expected = {
        (r["function"], r["callsite"], r["slot"])
        for r in inventory if r["classification"] == "unresolved" and
        arm_rows[(r["function"], r["callsite"], r["slot"])] ==
        "arm_receiver_matches_vtable_object"
    }
    if set(keys) != expected:
        raise ValueError("field-origin rows do not equal the 1,142 ARM-equal targets")
    for row in rows:
        if not row["origins"] or not row["evidence"] or int(row["terminal_count"]) < 1:
            raise ValueError("field-origin row lacks terminal evidence")
        if row["origin_status"] == "unresolved_machine_only" and not \
                row["origins"].startswith("machine-equal:"):
            raise ValueError("machine-only row lacks machine equality origin")
        if row["origin_status"].startswith("callback_") or \
                "registered_ci_callback" in row["origin_status"]:
            raise ValueError("origin analysis invented callback ownership")
        if row["origin_status"].startswith("proven_provider_constructed"):
            terminals = row["origins"].split(";")
            if not all("provider-constructor:" in terminal for terminal in terminals):
                raise ValueError("constructed-object exclusion has a mixed terminal")
    expected_groups = summarize(rows)
    if groups != expected_groups:
        raise ValueError("field-origin group summary does not reproduce")
    if len(groups) != 132 or sum(int(g["candidate_count"]) for g in groups) != 1142:
        raise ValueError("field-origin group totals changed")


def negative_tests(rows, inventory, arm_rows, groups):
    mutations = [rows[:-1]]
    duplicate = copy.deepcopy(rows); duplicate[-1] = copy.deepcopy(duplicate[0]); mutations.append(duplicate)
    forged = copy.deepcopy(rows); forged[0]["origin_status"] = "callback_owned"; mutations.append(forged)
    empty = copy.deepcopy(rows); empty[0]["origins"] = ""; mutations.append(empty)
    for mutation in mutations:
        try:
            validate(mutation, inventory, arm_rows, groups)
        except ValueError:
            continue
        raise ValueError("negative field-origin mutation unexpectedly passed")
    bad_groups = copy.deepcopy(groups); bad_groups[0]["candidate_count"] = "9999"
    try:
        validate(rows, inventory, arm_rows, bad_groups)
    except ValueError:
        return
    raise ValueError("negative field-origin group mutation unexpectedly passed")


def main():
    if len(sys.argv) not in (5, 6) or (len(sys.argv)==6 and sys.argv[5]!="--self-test"):
        raise SystemExit("usage: checker origins.csv groups.csv inventory.csv arm.csv [--self-test]")
    with open(sys.argv[1], newline="") as stream:
        reader=csv.DictReader(stream)
        if reader.fieldnames != ORIGIN_FIELDS: raise ValueError("unexpected origin header")
        rows=list(reader)
    with open(sys.argv[2], newline="") as stream:
        reader=csv.DictReader(stream)
        if reader.fieldnames != GROUP_FIELDS: raise ValueError("unexpected group header")
        groups=list(reader)
    with open(sys.argv[3], newline="") as stream: inventory=list(csv.DictReader(stream))
    with open(sys.argv[4], newline="") as stream:
        arm_rows={(r["function"],r["callsite"],r["slot"]):r["relation"] for r in csv.DictReader(stream)}
    validate(rows,inventory,arm_rows,groups)
    if len(sys.argv)==6: negative_tests(rows,inventory,arm_rows,groups)
    print("Callback field-origin audit passed: 1,142 equal rows in 132 structural groups")
    print("Field-origin split: 492 traced-without-owner, 2 provider-constructed noncallbacks, 1 local p-code fallback, 647 machine-only; 0 callback promotions")
    if len(sys.argv)==6: print("Callback field-origin negative-mutation self-test passed: 5 cases")


if __name__ == "__main__": main()
