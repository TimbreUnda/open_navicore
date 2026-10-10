#!/usr/bin/env python3
"""Independent guard for provider-wide outside-dense callback classification."""

import copy
import csv
import hashlib
import pathlib
import sys
from collections import Counter


FIELDS = [
    "function", "callsite", "slot", "base_register", "classification",
    "structural_category", "ci_classes", "adapter", "proxy_max_slot",
    "evidence",
]
KEY_HASH = "3a182132544a9e97c831a82a8671049d63ad8eb713b2dd4aa671efa9969d6048"
EXPECTED_CLASSES = Counter({"unresolved": 1233, "excluded": 498, "callback_owned": 9})
EXPECTED_CATEGORIES = Counter({
    "no_callback_object_provenance": 1221,
    "reserved_or_destructor_slot": 321,
    "receiver_vtable_mismatch_pcode": 103,
    "receiver_vtable_mismatch_arm": 10,
    "above_all_proxy_bounds": 59,
    "ambiguous_shared_caller": 12,
    "receiver_proven": 9,
    "outside_owned_proxy_bound": 4,
    "vtable_receiver_mismatch": 1,
})
PROVEN = {
    ("00262eec", "1", "4"),
    ("00262f94", "1", "5"),
    ("00263e6a", "root", "5"),
    ("002640ce", "9", "6"),
    ("002667c8", "13", "11"),
    ("0026cc50", "20", "6"),
    ("0026f3ce", "26", "4"),
    ("00279c76", "7", "6"),
    ("002b13d2", "24", "4"),
}
MISMATCH = {"0028bd06"}
DATAFLOW_FIELDS = [
    "function", "callsite", "slot", "base_register", "relation",
    "receiver_roots", "vtable_object_roots", "evidence",
]
DATAFLOW_KEY_HASH = "3abe928883c4f746fbff6554b7ab549d17c6b41c80eeb10a66e87227b4f2a54e"
EXPECTED_DATAFLOW = Counter({
    "callind_receiver_unavailable": 835,
    "receiver_matches_vtable_object": 735,
    "target_shape_unrecovered": 121,
    "receiver_vtable_disjoint_origins": 107,
    "receiver_vtable_relation_unresolved": 1,
})
ARM_FIELDS = [
    "function", "callsite", "slot", "base_register", "relation",
    "receiver_origins", "vtable_object_origins", "evidence",
]
EXPECTED_ARM = Counter({
    "arm_receiver_matches_vtable_object": 1514,
    "arm_receiver_vtable_relation_ambiguous": 266,
    "arm_receiver_vtable_disjoint_origins": 15,
    "arm_slice_unavailable": 4,
})


def validate(rows, contract_keys, dataflow, arm_slices):
    if len(rows) != 1740:
        raise ValueError("outside-dense inventory must contain exactly 1,740 chains")
    keys = [(r["function"], r["callsite"], r["slot"], r["base_register"]) for r in rows]
    if len(set(keys)) != 1740:
        raise ValueError("duplicate outside-dense instruction chain")
    digest = hashlib.sha256("".join(
        "%s,%s,%s,%s\n" % key for key in keys
    ).encode()).hexdigest()
    if digest != KEY_HASH:
        raise ValueError("outside-dense raw candidate key set/order changed")
    if Counter(r["classification"] for r in rows) != EXPECTED_CLASSES:
        raise ValueError("outside-dense classification totals changed")
    if Counter(r["structural_category"] for r in rows) != EXPECTED_CATEGORIES:
        raise ValueError("outside-dense structural split changed")

    proven = {
        (r["callsite"], r["adapter"], r["slot"])
        for r in rows if r["classification"] == "callback_owned"
    }
    if proven != PROVEN:
        raise ValueError("receiver-proven callback set changed")
    for row in rows:
        category = row["structural_category"]
        slot = int(row["slot"])
        flow = dataflow[(row["function"], row["callsite"], row["slot"])]
        arm = arm_slices[(row["function"], row["callsite"], row["slot"])]
        if flow["base_register"] != row["base_register"]:
            raise ValueError("receiver-dataflow base register does not reconcile")
        if arm["base_register"] != row["base_register"]:
            raise ValueError("ARM-slice base register does not reconcile")
        if not row["evidence"]:
            raise ValueError("classification without concrete evidence")
        if row["classification"] == "callback_owned":
            if category != "receiver_proven" or not row["ci_classes"]:
                raise ValueError("callback ownership lacks receiver and CI proof")
            if (row["adapter"], row["slot"]) not in contract_keys:
                raise ValueError("receiver-proven callback lacks safe contract row")
            if not 3 <= slot <= int(row["proxy_max_slot"]):
                raise ValueError("callback-owned slot outside proxy application range")
        elif row["adapter"]:
            raise ValueError("non-owned chain assigned a callback adapter")
        if category == "reserved_or_destructor_slot" and slot > 1:
            raise ValueError("destructor-slot exclusion is not slot 0..1")
        if category == "above_all_proxy_bounds" and slot <= 35:
            raise ValueError("global-bound exclusion does not exceed slot 35")
        if category == "outside_owned_proxy_bound":
            if not row["ci_classes"] or slot <= int(row["proxy_max_slot"]):
                raise ValueError("owner-bound exclusion lacks bound proof")
        if category == "vtable_receiver_mismatch" and row["callsite"] not in MISMATCH:
            raise ValueError("unexpected receiver-mismatch exclusion")
        if category == "receiver_vtable_mismatch_pcode":
            if flow["relation"] != "receiver_vtable_disjoint_origins":
                raise ValueError("p-code mismatch exclusion lacks disjoint origins")
            if not flow["receiver_roots"] or not flow["vtable_object_roots"]:
                raise ValueError("p-code mismatch exclusion lacks concrete roots")
        if category == "receiver_vtable_mismatch_arm":
            if arm["relation"] != "arm_receiver_vtable_disjoint_origins":
                raise ValueError("ARM mismatch exclusion lacks disjoint origins")
            if not arm["receiver_origins"] or not arm["vtable_object_origins"]:
                raise ValueError("ARM mismatch exclusion lacks concrete origins")
        if row["classification"] == "unresolved" and \
                flow["relation"] == "receiver_vtable_disjoint_origins":
            raise ValueError("disjoint receiver/vtable origins left unresolved")
        if row["classification"] == "unresolved" and \
                arm["relation"] == "arm_receiver_vtable_disjoint_origins":
            raise ValueError("ARM-disjoint receiver/vtable origins left unresolved")
        if row["classification"] == "callback_owned" and \
                flow["relation"] != "receiver_matches_vtable_object":
            raise ValueError("callback-owned chain lacks p-code receiver equality")
        if row["classification"] == "callback_owned" and \
                arm["relation"] != "arm_receiver_matches_vtable_object":
            raise ValueError("callback-owned chain lacks ARM receiver equality")
        if row["classification"] == "unresolved" and category not in {
            "no_callback_object_provenance", "ambiguous_shared_caller"
        }:
            raise ValueError("unresolved chain lacks a permitted provenance gap")
    corrected = next(r for r in rows if r["callsite"] == "00386e64")
    if corrected["classification"] != "unresolved" or \
            arm_slices[(corrected["function"], corrected["callsite"], corrected["slot"])]["relation"] != \
            "arm_receiver_matches_vtable_object":
        raise ValueError("0x00386e64 false mismatch was not corrected fail-closed")
    target = [r for r in rows
              if dataflow[(r["function"], r["callsite"], r["slot"])]["relation"] ==
              "callind_receiver_unavailable"
              and (r["structural_category"] in {
                  "no_callback_object_provenance", "receiver_vtable_mismatch_arm"
              }) and r["callsite"] != "00386e64"]
    if len(target) != 657:
        raise ValueError("targeted decompiler-gap set changed")
    target_relations = Counter(
        arm_slices[(r["function"], r["callsite"], r["slot"])]["relation"]
        for r in target
    )
    if target_relations != Counter({
            "arm_receiver_matches_vtable_object": 646,
            "arm_receiver_vtable_disjoint_origins": 10,
            "arm_receiver_vtable_relation_ambiguous": 1,
    }):
        raise ValueError("targeted 657-row ARM-slice closure changed")
    return rows


def read_dataflow(path):
    with pathlib.Path(path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != DATAFLOW_FIELDS:
            raise ValueError("unexpected receiver-dataflow header")
        rows = list(reader)
    if len(rows) != 1799:
        raise ValueError("receiver-dataflow inventory must contain 1,799 chains")
    keys = [(r["function"], r["callsite"], r["slot"], r["base_register"])
            for r in rows]
    if len(set(keys)) != 1799:
        raise ValueError("duplicate receiver-dataflow chain")
    digest = hashlib.sha256("".join(
        "%s,%s,%s,%s\n" % key for key in sorted(
            keys, key=lambda k: (k[0], k[1], int(k[2]), k[3]))
    ).encode()).hexdigest()
    if digest != DATAFLOW_KEY_HASH:
        raise ValueError("receiver-dataflow raw candidate key set changed")
    if Counter(r["relation"] for r in rows) != EXPECTED_DATAFLOW:
        raise ValueError("receiver-dataflow relation totals changed")
    for row in rows:
        if not row["evidence"]:
            raise ValueError("receiver-dataflow row lacks evidence")
        if row["relation"] == "receiver_vtable_disjoint_origins" and (
                not row["receiver_roots"] or not row["vtable_object_roots"]):
            raise ValueError("disjoint-origin row lacks both roots")
    return {(r["function"], r["callsite"], r["slot"]): r for r in rows}


def read_arm_slices(path):
    with pathlib.Path(path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ARM_FIELDS:
            raise ValueError("unexpected ARM-slice header")
        rows = list(reader)
    if len(rows) != 1799:
        raise ValueError("ARM-slice inventory must contain 1,799 chains")
    keys = [(r["function"], r["callsite"], r["slot"], r["base_register"])
            for r in rows]
    digest = hashlib.sha256("".join(
        "%s,%s,%s,%s\n" % key for key in sorted(
            keys, key=lambda k: (k[0], k[1], int(k[2]), k[3]))
    ).encode()).hexdigest()
    if digest != DATAFLOW_KEY_HASH:
        raise ValueError("ARM-slice raw candidate key set changed")
    if Counter(r["relation"] for r in rows) != EXPECTED_ARM:
        raise ValueError("ARM-slice relation totals changed")
    for row in rows:
        if not row["evidence"]:
            raise ValueError("ARM-slice row lacks evidence")
        if row["relation"] == "arm_receiver_vtable_disjoint_origins" and (
                not row["receiver_origins"] or not row["vtable_object_origins"]):
            raise ValueError("ARM disjoint-origin row lacks both origins")
    return {(r["function"], r["callsite"], r["slot"]): r for r in rows}


def read_contract_keys(paths):
    result = set()
    for path in paths:
        with pathlib.Path(path).open(newline="") as stream:
            for row in csv.DictReader(stream):
                adapter = row.get("adapter")
                if adapter and adapter != "all":
                    result.add((adapter, row["slot"]))
    return result


def negative_tests(rows, contracts, dataflow, arm_slices):
    mutations = []
    mutations.append(rows[:-1])
    duplicate = copy.deepcopy(rows)
    duplicate[-1] = copy.deepcopy(duplicate[0])
    mutations.append(duplicate)
    promote = copy.deepcopy(rows)
    target = next(r for r in promote if r["classification"] == "unresolved")
    target.update(classification="callback_owned", structural_category="receiver_proven",
                  adapter="1", ci_classes="CI_HMIIntegration", proxy_max_slot="14")
    mutations.append(promote)
    fake_adapter = copy.deepcopy(rows)
    next(r for r in fake_adapter if r["classification"] == "excluded")["adapter"] = "1"
    mutations.append(fake_adapter)
    bad_reserved = copy.deepcopy(rows)
    next(r for r in bad_reserved if r["structural_category"] ==
         "reserved_or_destructor_slot")["slot"] = "3"
    mutations.append(bad_reserved)
    bad_bound = copy.deepcopy(rows)
    next(r for r in bad_bound if r["structural_category"] ==
         "outside_owned_proxy_bound")["proxy_max_slot"] = "99"
    mutations.append(bad_bound)
    guessed = copy.deepcopy(rows)
    target = next(r for r in guessed if r["classification"] == "unresolved")
    target["structural_category"] = "receiver_proven"
    mutations.append(guessed)
    for mutation in mutations:
        try:
            validate(mutation, contracts, dataflow, arm_slices)
        except ValueError:
            continue
        raise ValueError("negative outside-inventory mutation unexpectedly passed")
    broken_flow = copy.deepcopy(dataflow)
    key = next((r["function"], r["callsite"], r["slot"])
               for r in rows if r["structural_category"] ==
               "receiver_vtable_mismatch_pcode")
    broken_flow[key]["relation"] = "receiver_matches_vtable_object"
    try:
        validate(rows, contracts, broken_flow, arm_slices)
    except ValueError:
        pass
    else:
        raise ValueError("negative receiver-dataflow mutation unexpectedly passed")
    broken_arm = copy.deepcopy(arm_slices)
    key = next((r["function"], r["callsite"], r["slot"])
               for r in rows if r["structural_category"] ==
               "receiver_vtable_mismatch_arm")
    broken_arm[key]["relation"] = "arm_receiver_matches_vtable_object"
    try:
        validate(rows, contracts, dataflow, broken_arm)
    except ValueError:
        return
    raise ValueError("negative ARM-slice mutation unexpectedly passed")


def validate_next_actions(path, rows, dataflow):
    with pathlib.Path(path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != [
            "structural_category", "dataflow_relation", "slot",
            "candidate_count", "next_action"
        ]:
            raise ValueError("unexpected unresolved-next-actions header")
        actions = list(reader)
    expected = Counter(
        (r["structural_category"],
         dataflow[(r["function"], r["callsite"], r["slot"])]["relation"],
         r["slot"])
        for r in rows if r["classification"] == "unresolved"
    )
    actual = {}
    for row in actions:
        key = (row["structural_category"], row["dataflow_relation"], row["slot"])
        if key in actual or not row["next_action"]:
            raise ValueError("duplicate or actionless unresolved work group")
        actual[key] = int(row["candidate_count"])
    if actual != dict(expected):
        raise ValueError("unresolved-next-actions does not reconcile inventory")


def main():
    if len(sys.argv) not in (8, 9) or (len(sys.argv) == 9 and sys.argv[8] != "--self-test"):
        raise SystemExit(
            "usage: check_static_callback_outside_inventory.py inventory.csv "
            "runtime.csv additions.csv contracts.csv next-actions.csv "
            "receiver-dataflow.csv arm-slices.csv [--self-test]"
        )
    with pathlib.Path(sys.argv[1]).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != FIELDS:
            raise ValueError("unexpected outside-inventory header")
        rows = list(reader)
    contracts = read_contract_keys(sys.argv[2:5])
    dataflow = read_dataflow(sys.argv[6])
    arm_slices = read_arm_slices(sys.argv[7])
    validate(rows, contracts, dataflow, arm_slices)
    validate_next_actions(sys.argv[5], rows, dataflow)
    if len(sys.argv) == 9:
        negative_tests(rows, contracts, dataflow, arm_slices)
    print("Outside-dense callback audit passed: 9 owned, 498 excluded, 1233 unresolved")
    print("Receiver dataflow audit passed: 1,799 rows, 107 disjoint-origin mismatches")
    print("ARM receiver-slice audit passed: 1,799 rows, 15 disjoint-origin mismatches")
    if len(sys.argv) == 9:
        print("Outside-dense callback negative-mutation self-test passed: 9 cases")


if __name__ == "__main__":
    main()
