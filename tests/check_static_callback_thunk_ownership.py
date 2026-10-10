#!/usr/bin/env python3
"""Guard the unresolved stripped callback-thunk ownership boundary."""

import csv
import copy
import pathlib
import sys

FIELDS = [
    "function", "slot", "ownership_state", "ci_class", "adapter",
    "contract_state", "evidence",
]

EXPECTED_UNIQUE = {
    ("0x00298034", "19", "10"), ("0x002980ac", "6", "7"),
    ("0x00298048", "20", "10"), ("0x00298070", "8", "20"),
    ("0x00298084", "9", "20"), ("0x00298138", "8", "1"),
    ("0x0029814c", "9", "1"), ("0x00298160", "10", "1"),
    ("0x00298174", "11", "1"), ("0x00298190", "13", "1"),
    ("0x002981f8", "3", "19"), ("0x0029820c", "4", "19"),
    ("0x00298248", "7", "19"), ("0x00298284", "11", "19"),
    ("0x00298318", "9", "root"), ("0x00298368", "8", "31"),
    ("0x002983c8", "15", "31"), ("0x00298508", "4", "24"),
    ("0x00298594", "7", "25"), ("0x0029867c", "18", "25"),
    ("0x002985a8", "8", "25"), ("0x002985bc", "9", "25"),
    ("0x002985d0", "10", "25"), ("0x002985e4", "11", "25"),
    ("0x002985f8", "12", "25"), ("0x0029860c", "13", "25"),
    ("0x00298620", "14", "25"), ("0x00298640", "15", "25"),
    ("0x00298654", "16", "25"), ("0x00298668", "17", "25"),
    ("0x00298690", "19", "25"), ("0x002986b8", "20", "25"),
    ("0x002986dc", "21", "25"), ("0x00298720", "24", "25"),
    ("0x002986f0", "22", "25"), ("0x0029870c", "23", "25"),
    ("0x00298734", "25", "25"), ("0x00298748", "26", "25"),
    ("0x0029875c", "27", "25"), ("0x00298770", "28", "25"),
    ("0x00298784", "29", "25"), ("0x00298798", "30", "25"),
    ("0x002987ac", "35", "25"), ("0x002987cc", "34", "25"),
    ("0x002981d0", "4", "17"), ("0x00298220", "5", "19"),
    ("0x00298234", "6", "19"), ("0x00298354", "6", "31"),
    ("0x0029837c", "10", "31"), ("0x002983b4", "13", "31"),
    ("0x0029847c", "3", "7"), ("0x00298530", "3", "8"),
    ("0x00298544", "5", "8"), ("0x00298558", "6", "8"),
    ("0x0029856c", "11", "8"), ("0x00298a28", "3", "17"),
}

EXPECTED_EXCLUDED = {
    ("0x002980e8", "5"), ("0x0029825c", "10"),
    ("0x00298304", "6"),
}


def validate(rows):
    keys = {(r["function"], r["slot"]) for r in rows}
    if len(rows) != 59 or len(keys) != 59:
        raise ValueError("expected exactly 59 dense forwarding thunks")
    unique = {
        (r["function"], r["slot"], r["adapter"])
        for r in rows if r["ownership_state"] == "unique"
    }
    if unique != EXPECTED_UNIQUE:
        raise ValueError("unique thunk ownership catalog changed")
    excluded = [r for r in rows
                if r["ownership_state"] == "excluded_unreachable_orphan"]
    if {(r["function"], r["slot"]) for r in excluded} != EXPECTED_EXCLUDED:
        raise ValueError("expected exactly three proven unreachable orphan thunks")
    for row in excluded:
        if row["ci_class"] or row["adapter"]:
            raise ValueError("excluded orphan was assigned an owner")
        if row["contract_state"] != "excluded_no_contract":
            raise ValueError("excluded orphan acquired a callback contract")
        if "whole-image" not in row["evidence"] or "no code data vtable" not in row["evidence"]:
            raise ValueError("excluded orphan lacks concrete whole-image xref evidence")
    for row in rows:
        if row["ownership_state"] not in ("unique", "excluded_unreachable_orphan"):
            raise ValueError("invalid ownership state")
        if row["contract_state"] not in (
            "runtime_contract", "static_contract", "arity_unresolved_no_emit",
            "excluded_no_contract",
        ):
            raise ValueError("invalid contract state")
        if (row["ownership_state"] == "unique" and
                row["contract_state"] == "excluded_no_contract"):
            raise ValueError("owned thunk uses exclusion state")
        if not row["evidence"]:
            raise ValueError("missing thunk evidence")
    return rows


def read(path):
    with pathlib.Path(path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != FIELDS:
            raise ValueError("unexpected thunk ownership header")
        rows = list(reader)
    return validate(rows)


def contract_keys(runtime_path, additions_path, contracts_path):
    runtime = {
        (r["adapter"], r["slot"])
        for r in csv.DictReader(pathlib.Path(runtime_path).open(newline=""))
        if r["adapter"] != "all"
    }
    additions = {
        (r["adapter"], r["slot"])
        for r in csv.DictReader(pathlib.Path(additions_path).open(newline=""))
    }
    contracts = {
        (r["adapter"], r["slot"])
        for r in csv.DictReader(pathlib.Path(contracts_path).open(newline=""))
    }
    return runtime, additions, contracts


def reconcile(rows, runtime, additions, contracts):
    for row in rows:
        if row["ownership_state"] != "unique":
            continue
        key = (row["adapter"], row["slot"])
        state = row["contract_state"]
        if state == "runtime_contract" and key not in runtime:
            raise ValueError("missing runtime contract for %r" % (key,))
        if state == "static_contract" and key not in additions | contracts:
            raise ValueError("missing static contract for %r" % (key,))
        if state == "arity_unresolved_no_emit" and key in runtime | additions | contracts:
            raise ValueError("unresolved arity falsely overlaps a contract: %r" % (key,))


def negative_tests(rows, runtime, additions, contracts):
    mutations = []

    mutations.append(rows[:-1])

    duplicate = copy.deepcopy(rows)
    duplicate.append(copy.deepcopy(duplicate[0]))
    mutations.append(duplicate)

    ownership_drift = copy.deepcopy(rows)
    unique = next(r for r in ownership_drift if r["ownership_state"] == "unique")
    unique["adapter"] = "999"
    mutations.append(ownership_drift)

    assigned_excluded = copy.deepcopy(rows)
    excluded = next(r for r in assigned_excluded
                    if r["ownership_state"] == "excluded_unreachable_orphan")
    excluded["ci_class"] = "CI_Guessed"
    excluded["adapter"] = "999"
    mutations.append(assigned_excluded)

    unsafe_state = copy.deepcopy(rows)
    excluded = next(r for r in unsafe_state
                    if r["ownership_state"] == "excluded_unreachable_orphan")
    excluded["contract_state"] = "arity_unresolved_no_emit"
    mutations.append(unsafe_state)

    for mutation in mutations:
        try:
            validate(mutation)
        except ValueError:
            continue
        raise ValueError("negative ownership mutation unexpectedly passed")

    contract_mismatch = copy.deepcopy(rows)
    contracted = next(
        r for r in contract_mismatch if r["contract_state"] == "runtime_contract"
    )
    contracted["contract_state"] = "static_contract"
    try:
        validate(contract_mismatch)
        reconcile(contract_mismatch, runtime, additions, contracts)
    except ValueError:
        pass
    else:
        raise ValueError("negative contract mutation unexpectedly passed")


def main():
    if len(sys.argv) not in (5, 6) or (len(sys.argv) == 6 and sys.argv[5] != "--self-test"):
        raise SystemExit(
            "usage: check_static_callback_thunk_ownership.py "
            "ownership.csv runtime.csv additions.csv contracts.csv [--self-test]"
        )
    rows = read(sys.argv[1])
    runtime, additions, contracts = contract_keys(sys.argv[2], sys.argv[3], sys.argv[4])
    reconcile(rows, runtime, additions, contracts)
    if len(sys.argv) == 6:
        negative_tests(rows, runtime, additions, contracts)
    print("Static callback thunk guard passed: 56 unique, 3 unreachable-orphan exclusions")
    print("All uniquely owned dense thunks now have runtime or static contracts")
    if len(sys.argv) == 6:
        print("Static callback thunk negative-mutation self-test passed: 6 cases")


if __name__ == "__main__":
    main()
