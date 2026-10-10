#!/usr/bin/env python3
"""Classify stock-provider exact virtual-call chains without guessing ownership.

The inputs are the raw outputs of FindExactCallbackDispatches.java and
AttributeCallbackDispatches.java.  CI attribution is only a lead: a call is
callback-owned only when a separately audited callsite proves that the object
loaded from the CI callback field is also the call receiver.
"""

import argparse
import csv
from collections import defaultdict


DENSE_START = 0x00298000
DENSE_END = 0x00299000

# Independent bounds from QR39.6 IF_Server*Proxy vtable address points. Slots
# 0/1 are destructors. Slot 2 is an abstract application method (the common
# adapterReady callback on registered adapters), not a reserved slot.
PROXY_BOUNDS = {
    "CI_AudioHandling_General": 20,
    "CI_Connectivity": 9,
    "CI_Diagnosis": 8,
    "CI_Diagnosis_Navigation": 13,
    "CI_GPSData": 6,
    "CI_HMIControl_Hardkey": 3,
    "CI_HMIControl_RotaryKnob": 3,
    "CI_HMIIntegration": 14,
    "CI_InstrumentCluster": 7,
    "CI_InterApplicationDataExchange": 11,
    "CI_Internationalization": 6,
    "CI_Logging": 4,
    "CI_Phone": 5,
    "CI_SoftwareInfrastructure": 12,
    "CI_TTI_FM_VICS": 5,
    "CI_TTI_LiveTraffic": 5,
    "CI_TTI_RDS": 7,
    "CI_TTI_SXM_AGW": 15,
    "CI_TTI_SXM_Apogee": 6,
    "CI_TTI_SXM_Parking": 4,
    "CI_TTI_TPEG": 6,
    "CI_TTI_TPEG_EWS": 4,
    "CI_TTS": 4,
    "CI_VehicleData": 12,
    "CI_VoiceRecognition": 35,
}

CI_TO_ADAPTER = {
    "CI_HMIIntegration": "1",
    "CI_SoftwareInfrastructure": "root",
    "CI_GPSData": "9",
    "CI_Diagnosis_Navigation": "13",
    "CI_Connectivity": "20",
    "CI_Phone": "26",
    "CI_Internationalization": "7",
    "CI_TTS": "24",
}

# These nine callsites have receiver provenance in addition to a unique CI
# caller and an in-bound IF_Server*Proxy slot.  Seven are named static rows;
# two are already in the runtime callback matrix.
PROVEN = {
    "00262eec", "00262f94", "00263e6a", "002640ce", "002667c8",
    "0026cc50", "0026f3ce", "00279c76", "002b13d2",
}

# Unique CI attribution plus an in-bound slot is not sufficient.  This site
# has an independently instruction-proven receiver mismatch.
PROVEN_NON_CALLBACK_RECEIVER = {"0028bd06"}

FIELDS = [
    "function", "callsite", "slot", "base_register", "classification",
    "structural_category", "ci_classes", "adapter", "proxy_max_slot",
    "evidence",
]


def read_attribution(path):
    result = defaultdict(set)
    with open(path, newline="") as stream:
        for row in csv.DictReader(stream):
            if row["ci_class"] != "unattributed":
                result[(row["function"], row["callsite"], row["slot"])].add(
                    row["ci_class"]
                )
    return result


def read_receiver_dataflow(path):
    result = {}
    with open(path, newline="") as stream:
        reader = csv.DictReader(stream)
        expected = [
            "function", "callsite", "slot", "base_register", "relation",
            "receiver_roots", "vtable_object_roots", "evidence",
        ]
        if reader.fieldnames != expected:
            raise ValueError("unexpected receiver-dataflow header")
        for row in reader:
            key = (row["function"], row["callsite"], row["slot"])
            if key in result:
                raise ValueError("duplicate receiver-dataflow key: " + str(key))
            result[key] = row
    if len(result) != 1799:
        raise ValueError("expected 1,799 receiver-dataflow rows, got %d" % len(result))
    return result


def read_arm_slices(path):
    result = {}
    with open(path, newline="") as stream:
        reader = csv.DictReader(stream)
        expected = [
            "function", "callsite", "slot", "base_register", "relation",
            "receiver_origins", "vtable_object_origins", "evidence",
        ]
        if reader.fieldnames != expected:
            raise ValueError("unexpected ARM-slice header")
        for row in reader:
            key = (row["function"], row["callsite"], row["slot"])
            if key in result:
                raise ValueError("duplicate ARM-slice key: " + str(key))
            result[key] = row
    if len(result) != 1799:
        raise ValueError("expected 1,799 ARM-slice rows, got %d" % len(result))
    return result


def classify(row, classes, dataflow, arm_slice):
    slot = int(row["slot"])
    callsite = row["callsite"]
    owner = next(iter(classes)) if len(classes) == 1 else ""
    bound = PROXY_BOUNDS.get(owner)

    if callsite in PROVEN:
        if not owner or bound is None or not 3 <= slot <= bound:
            raise ValueError("proven callback lost unique in-bound owner: " + callsite)
        return (
            "callback_owned", "receiver_proven", owner,
            CI_TO_ADAPTER[owner], str(bound),
            "audited instruction/decompiler path: CI callback field supplies "
            "both vtable and call receiver; unique CI owner; slot within exact "
            "QR39.6 IF_Server*Proxy bound",
        )
    if callsite in PROVEN_NON_CALLBACK_RECEIVER:
        return (
            "excluded", "vtable_receiver_mismatch", owner, "",
            "" if bound is None else str(bound),
            "audited instructions prove vtable-source object differs from r0 "
            "call receiver; CI caller adjacency is not callback ownership",
        )
    if slot <= 1:
        return (
            "excluded", "reserved_or_destructor_slot", owner, "",
            "" if bound is None else str(bound),
            "slot 0/1 are IF_Server*Proxy destructors; slot 2 is separately "
            "retained as an abstract application-method candidate",
        )
    if slot > max(PROXY_BOUNDS.values()):
        return (
            "excluded", "above_all_proxy_bounds", owner, "", "",
            "slot exceeds global maximum 35 across all 25 QR39.6 "
            "IF_Server*Proxy vtables",
        )
    if owner and bound is not None and slot > bound:
        return (
            "excluded", "outside_owned_proxy_bound", owner, "", str(bound),
            "unique CI attribution but slot exceeds that CI's independent "
            "QR39.6 IF_Server*Proxy vtable bound",
        )
    if len(classes) > 1:
        return (
            "unresolved", "ambiguous_shared_caller", "", "", "",
            "reverse caller graph reaches multiple CI classes; shared helper "
            "does not prove callback-object provenance",
        )
    if dataflow["relation"] == "receiver_vtable_disjoint_origins":
        return (
            "excluded", "receiver_vtable_mismatch_pcode", owner, "",
            "" if bound is None else str(bound),
            "Ghidra decompiler p-code definition/use proves r0 origin '" +
            dataflow["receiver_roots"] + "' is disjoint from vtable-object " +
            "origin '" + dataflow["vtable_object_roots"] + "'",
        )
    if arm_slice["relation"] == "arm_receiver_vtable_disjoint_origins":
        return (
            "excluded", "receiver_vtable_mismatch_arm", owner, "",
            "" if bound is None else str(bound),
            "instruction-level ARM/Thumb backward slice proves machine r0 "
            "origin '" + arm_slice["receiver_origins"] + "' is disjoint from "
            "vtable-object origin '" + arm_slice["vtable_object_origins"] + "'",
        )
    return (
        "unresolved", "no_callback_object_provenance", owner, "",
        "" if bound is None else str(bound),
        "syntax matches object+4/vtable/slot/blx, but no constructor field or "
        "receiver proof ties that object to a registered callback proxy",
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("exact")
    parser.add_argument("attributed")
    parser.add_argument("output")
    parser.add_argument("--next-actions")
    parser.add_argument("--receiver-dataflow", required=True)
    parser.add_argument("--arm-slices", required=True)
    args = parser.parse_args()
    attributed = read_attribution(args.attributed)
    dataflow = read_receiver_dataflow(args.receiver_dataflow)
    arm_slices = read_arm_slices(args.arm_slices)
    output = []
    with open(args.exact, newline="") as stream:
        for row in csv.DictReader(stream):
            function = int(row["function"], 16)
            if DENSE_START <= function < DENSE_END:
                continue
            key = (row["function"], row["callsite"], row["slot"])
            if key not in dataflow:
                raise ValueError("missing receiver dataflow: " + str(key))
            if key not in arm_slices:
                raise ValueError("missing ARM receiver slice: " + str(key))
            classes = attributed.get(key, set())
            classification, category, owner, adapter, bound, evidence = classify(
                row, classes, dataflow[key], arm_slices[key]
            )
            output.append({
                **row,
                "classification": classification,
                "structural_category": category,
                "ci_classes": ";".join(sorted(classes)),
                "adapter": adapter,
                "proxy_max_slot": bound,
                "evidence": evidence,
            })
    if len(output) != 1740:
        raise ValueError("expected 1,740 outside-dense chains, got %d" % len(output))
    with open(args.output, "w", newline="") as stream:
        writer = csv.DictWriter(stream, FIELDS)
        writer.writeheader()
        writer.writerows(output)
    if args.next_actions:
        counts = defaultdict(int)
        for row in output:
            if row["classification"] == "unresolved":
                key = (row["function"], row["callsite"], row["slot"])
                counts[(row["structural_category"],
                        dataflow[key]["relation"], row["slot"])] += 1
        with open(args.next_actions, "w", newline="") as stream:
            fields = ["structural_category", "dataflow_relation", "slot",
                      "candidate_count", "next_action"]
            writer = csv.DictWriter(stream, fields)
            writer.writeheader()
            for category, relation, slot in sorted(
                    counts, key=lambda key: (key[0], key[1], int(key[2]))):
                if category == "ambiguous_shared_caller":
                    action = (
                        "split shared helper by incoming constructor/member-pointer "
                        "path and prove the concrete receiver field"
                    )
                elif relation == "receiver_matches_vtable_object":
                    action = (
                        "trace the matching object through callers/wrappers to its "
                        "constructor field; accept only a registered CI callback "
                        "field and exact proxy bound")
                elif relation == "callind_receiver_unavailable":
                    action = (
                        "machine receiver relation recovered; trace the matching "
                        "object through callers/constructors to a registered CI field")
                else:
                    action = (
                        "split phi/control-flow paths and prove receiver/vtable "
                        "relation before tracing any surviving object origin")
                writer.writerow({
                    "structural_category": category,
                    "dataflow_relation": relation,
                    "slot": slot,
                    "candidate_count": counts[(category, relation, slot)],
                    "next_action": action,
                })


if __name__ == "__main__":
    main()
