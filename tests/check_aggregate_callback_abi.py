#!/usr/bin/env python3
"""Guard the four machine/consumer/compiler-qualified aggregate callbacks."""

import copy
import csv
import sys


EXPECTED = {
    ("31", "12"): ("7", "MockPasSxmLocation", "080f9efc"),
    ("30", "4"): ("16", "MockPasSxmMbr", "081331a0"),
    ("30", "5"): ("16", "MockPasSxmMbr", "08134724"),
    ("29", "4"): ("14", "MockPasSxmMbr", "08138204"),
}


def load(path):
    with open(path, newline="") as stream:
        return list(csv.DictReader(stream))


def validate(oracle, audit, manifest):
    if {(r["adapter"], r["slot"]) for r in oracle} != set(EXPECTED):
        raise ValueError("aggregate oracle key mismatch")
    audit_rows = {(r["adapter"], r["slot"]): r for r in audit}
    manifest_rows = {(r["adapter"], r["slot"]): r for r in manifest}
    for row in oracle:
        key = (row["adapter"], row["slot"])
        words, aggregate, target = EXPECTED[key]
        concrete = audit_rows[key]
        live = manifest_rows[key]
        if (row["words_after_self"] != words or
                row["location_size"] != "16" or row["mbr_size"] != "48" or
                row["mbr_alignment"] != "8" or aggregate not in row["signature"] or
                len(row["callee_sha256"]) != 64 or
                len(row["caller_sha256"]) != 64 or
                "-Vgcc_ntoarmv7le" not in row["compiler_flags"]):
            raise ValueError("weak compiler evidence for %r" % (key,))
        if (concrete["target"] != target or
                concrete["qr40_target"] != target or
                concrete["body_match"] != "identical" or
                concrete["contract_class"] != "typed_dormant" or
                concrete["abi_words_after_self"] != words or
                "QNX aggregate oracle" not in concrete["evidence"]):
            raise ValueError("weak concrete consumer evidence for %r" % (key,))
        if (live["callability"] != "callable_typed_dormant" or
                live["abi_words_after_self"] != words or
                aggregate not in live["abi_prototype"] or
                not live["implementation_seam"] or live["emitted"] != "no"):
            raise ValueError("aggregate production seam mismatch for %r" % (key,))


def negative_tests(oracle, audit, manifest):
    mutations = []
    missing = copy.deepcopy(oracle); missing.pop(); mutations.append((missing, audit, manifest))
    size = copy.deepcopy(oracle); size[0]["mbr_size"] = "40"; mutations.append((size, audit, manifest))
    target = copy.deepcopy(audit)
    next(r for r in target if (r["adapter"], r["slot"]) ==
         ("31", "12"))["qr40_target"] = "deadbeef"
    mutations.append((oracle, target, manifest))
    callable_rows = copy.deepcopy(manifest)
    callable_rows[next(i for i, r in enumerate(callable_rows)
                       if (r["adapter"], r["slot"]) == ("31", "12"))]["emitted"] = "yes"
    mutations.append((oracle, audit, callable_rows))
    for candidate in mutations:
        try:
            validate(*candidate)
        except ValueError:
            continue
        raise ValueError("aggregate ABI negative mutation passed")


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: checker oracle.csv audit.csv manifest.csv")
    oracle, audit, manifest = map(load, sys.argv[1:])
    validate(oracle, audit, manifest)
    negative_tests(oracle, audit, manifest)
    print("Aggregate callback ABI passed: 4/4 typed dormant seams")


if __name__ == "__main__":
    main()
