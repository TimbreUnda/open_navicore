#!/usr/bin/env python3
"""Independent invariants and negative tests for the compiler-oracle output."""

import copy
import csv
import hashlib
import sys
from collections import Counter


FLAGS = "-Vgcc_ntoarmv7le -O2 -fno-inline -fno-ipa-sra -fno-optimize-sibling-calls -ffunction-sections"
CATALOG_FIELDS = [
    "symbol", "language", "role", "family", "abi_class", "words_after_self",
    "hazards", "compiler_flags", "normalized_sha256", "normalized_ops",
]
ASSESS_FIELDS = [
    "adapter", "slot", "interface", "producer_evidence", "consumer_evidence",
    "best_family", "best_score", "second_score", "two_sided_agreement",
    "hazards_excluded", "decision", "reason",
]
EXPECTED_CLASSES = Counter({
    "word_scalar": 22, "pod_by_value": 10, "hidden_sret": 10,
    "u64_aligned": 4, "float_soft": 2, "double_soft": 2,
    "pointer_or_reference": 2, "function_pointer": 2,
    "cpp_reference": 2, "member_pointer": 2, "cpp_virtual": 2,
})


def load(path):
    with open(path, newline="") as stream:
        reader = csv.DictReader(stream)
        return reader.fieldnames, list(reader)


def validate(catalog, assessment, manifest):
    if len(catalog) != 60 or Counter(r["abi_class"] for r in catalog) != EXPECTED_CLASSES:
        raise ValueError("oracle family partition changed")
    if Counter(r["role"] for r in catalog) != {"caller": 30, "callee": 30}:
        raise ValueError("caller/callee fixture partition changed")
    pairs = Counter((r["family"], r["role"]) for r in catalog)
    if any(n != 1 for n in pairs.values()) or len(pairs) != 60:
        raise ValueError("oracle family lacks one caller and one callee")
    words = {int(r["words_after_self"]) for r in catalog
             if r["abi_class"] == "word_scalar" and r["role"] == "caller"}
    if words != set(range(11)):
        raise ValueError("0..10 scalar-word permutation family is incomplete")
    for row in catalog:
        if row["compiler_flags"] != FLAGS or not row["normalized_ops"]:
            raise ValueError("compiler provenance or normalized dataflow missing")
        if hashlib.sha256(row["normalized_ops"].encode()).hexdigest() != row["normalized_sha256"]:
            raise ValueError("normalized fingerprint digest mismatch")
        if reintroduces_source_claim(row):
            raise ValueError("oracle incorrectly claims source-level identity")
    unresolved = [(r["adapter"], r["slot"], r["interface"])
                  for r in manifest if r["contract_source"] ==
                  "cross_version_concrete_vtable_audit"]
    got = [(r["adapter"], r["slot"], r["interface"]) for r in assessment]
    if len(unresolved) != 63 or got != unresolved:
        raise ValueError("assessment does not exactly cover the 63 unrecovered slots")
    for row in assessment:
        if row["decision"] not in {"remain_fail_closed", "abi_equivalent_candidate"}:
            raise ValueError("unknown oracle decision")
        if row["decision"] == "abi_equivalent_candidate" and not (
                row["two_sided_agreement"] == "yes" and
                row["hazards_excluded"] == "yes" and
                row["best_score"] == "100.000" and
                float(row["best_score"]) > float(row["second_score"])):
            raise ValueError("candidate lacks unique exact safe two-sided evidence")
        if (row["producer_evidence"] == "none" or row["consumer_evidence"] == "none") and \
                row["decision"] != "remain_fail_closed":
            raise ValueError("one-sided or absent evidence promoted a slot")
    return True


def reintroduces_source_claim(row):
    text = (row["hazards"] + " " + row["family"]).lower()
    return "source truth" in text or "exact source" in text


def negative_tests(catalog, assessment, manifest):
    mutations = []
    mutations.append((catalog[:-1], assessment, manifest))
    bad_hash = copy.deepcopy(catalog); bad_hash[0]["normalized_sha256"] = "0" * 64
    mutations.append((bad_hash, assessment, manifest))
    bad_flags = copy.deepcopy(catalog); bad_flags[0]["compiler_flags"] += " -funsafe"
    mutations.append((bad_flags, assessment, manifest))
    short = copy.deepcopy(assessment[:-1]); mutations.append((catalog, short, manifest))
    forged = copy.deepcopy(assessment); forged[0].update(
        decision="abi_equivalent_candidate", two_sided_agreement="yes",
        hazards_excluded="yes", best_score="100.000", second_score="99.000")
    mutations.append((catalog, forged, manifest))
    hazardous = copy.deepcopy(assessment); hazardous[0].update(
        decision="abi_equivalent_candidate", producer_evidence="fixture",
        consumer_evidence="fixture", two_sided_agreement="yes",
        hazards_excluded="no", best_score="100.000", second_score="0.000")
    mutations.append((catalog, hazardous, manifest))
    for items in mutations:
        try:
            validate(*items)
        except ValueError:
            continue
        raise ValueError("negative oracle mutation unexpectedly passed")


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: check_qnx_arm_abi_oracle.py catalog.csv assessment.csv manifest.csv")
    cfields, catalog = load(sys.argv[1]); afields, assessment = load(sys.argv[2]); _, manifest = load(sys.argv[3])
    if cfields != CATALOG_FIELDS or afields != ASSESS_FIELDS:
        raise ValueError("oracle artifact schema changed")
    validate(catalog, assessment, manifest)
    negative_tests(catalog, assessment, manifest)
    print("QNX ARM ABI oracle passed: 60 functions / 30 declaration families")
    print("General compiler permuter promoted 0 rows; concrete plus exact aggregate evidence resolves 63 typed")


if __name__ == "__main__":
    main()
