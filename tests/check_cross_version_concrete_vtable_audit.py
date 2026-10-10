#!/usr/bin/env python3
"""Guard the bounded QR39.6/QR40 concrete SI vtable audit."""

import copy
import csv
import hashlib
import sys
from collections import Counter


IDENTITY_DIGEST = "bcdb2ec40064b28c4f1dce974555128281fa23dc09032e3cc49c31b9c8ffd3e2"
EXPECTED_CLASSES = {
    "typed_dormant": 63,
}
FIELDS = [
    "adapter", "slot", "interface", "concrete_vtable", "target",
    "qr40_target", "body_match", "api_name", "contract_class",
    "abi_words_after_self", "consumer_access", "ownership", "evidence",
]


def load(path):
    with open(path, newline="") as stream:
        return list(csv.DictReader(stream))


def digest(rows):
    fields = ("adapter", "slot", "interface", "concrete_vtable", "target",
              "qr40_target")
    text = "\n".join("|".join(row[field] for field in fields) for row in rows)
    return hashlib.sha256(text.encode()).hexdigest()


def validate(audit, proxy_manifest):
    if len(audit) != 63 or list(audit[0]) != FIELDS:
        raise ValueError("audit shape mismatch")
    if digest(audit) != IDENTITY_DIGEST:
        raise ValueError("concrete slot identity mismatch")
    if Counter(row["contract_class"] for row in audit) != EXPECTED_CLASSES:
        raise ValueError("contract partition mismatch")
    keys = [(row["adapter"], row["slot"]) for row in audit]
    if len(set(keys)) != len(keys):
        raise ValueError("duplicate audit key")
    integrated = {(row["adapter"], row["slot"]): row for row in proxy_manifest
                  if row["contract_source"] ==
                  "cross_version_concrete_vtable_audit"}
    if set(keys) != set(integrated):
        raise ValueError("audit does not exactly cover integrated surface")
    for row in audit:
        if (row["target"] != row["qr40_target"] or
                row["body_match"] != "identical" or
                not row["api_name"] or not row["consumer_access"] or
                not row["ownership"] or "q396/q40 entry-" not in row["evidence"]):
            raise ValueError("weak cross-version evidence row")
        unsafe = row["contract_class"].endswith("fail_closed")
        if unsafe != ("fail_closed" in row["contract_class"]):
            raise ValueError("malformed unsafe class")
        if unsafe and row["ownership"] not in ("synchronous_unbounded", "by_value"):
            raise ValueError("unsafe row lacks ownership/layout hazard")
        if not unsafe and row["abi_words_after_self"] == "unknown":
            raise ValueError("typed row has unknown ABI width")
        manifest_row = integrated[(row["adapter"], row["slot"])]
        expected = {
            "typed_dormant": "callable_typed_dormant",
            "pointer_fail_closed": "fail_closed_pointer_contract",
            "aggregate_fail_closed": "fail_closed_aggregate_contract",
        }[row["contract_class"]]
        if manifest_row["callability"] != expected:
            raise ValueError("audit class not preserved in production manifest")
        if row["contract_class"] == "typed_dormant":
            if (not manifest_row["implementation_seam"] or
                    manifest_row["emitted"] != "no" or
                    manifest_row["abi_words_after_self"] !=
                    row["abi_words_after_self"]):
                raise ValueError("typed concrete seam is incomplete")
        elif manifest_row["implementation_seam"] or manifest_row["emitted"] != "no":
            raise ValueError("unsafe concrete row became callable or emitted")


def negative_tests(audit, proxy_manifest):
    mutations = []
    mutations.append(copy.deepcopy(audit[:-1]))
    duplicate = copy.deepcopy(audit); duplicate[-1] = copy.deepcopy(duplicate[0]); mutations.append(duplicate)
    target = copy.deepcopy(audit); target[0]["qr40_target"] = "deadbeef"; mutations.append(target)
    body = copy.deepcopy(audit); body[1]["body_match"] = "name_only"; mutations.append(body)
    demoted = copy.deepcopy(audit)
    next(row for row in demoted if (row["adapter"], row["slot"]) ==
         ("31", "12"))["contract_class"] = "aggregate_fail_closed"
    mutations.append(demoted)
    access = copy.deepcopy(audit); access[2]["consumer_access"] = ""; mutations.append(access)
    for mutation in mutations:
        try:
            validate(mutation, proxy_manifest)
        except ValueError:
            continue
        raise ValueError("negative audit mutation unexpectedly passed")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: check_cross_version_concrete_vtable_audit.py audit.csv proxy-manifest.csv")
    audit = load(sys.argv[1])
    proxy_manifest = load(sys.argv[2])
    validate(audit, proxy_manifest)
    negative_tests(audit, proxy_manifest)
    print("Concrete SI vtable audit passed: 63/63 prior ABI gaps")
    print("63 typed-capable; 0 pointer-fail-closed; 0 aggregate-fail-closed")


if __name__ == "__main__":
    main()
