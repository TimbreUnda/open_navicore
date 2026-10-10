#!/usr/bin/env python3
"""Guard production integration of the complete concrete-vtable audit."""

import copy
import csv
import pathlib
import re
import sys
from collections import Counter


POINTER_SAFE = {
    ("24", "3"): ("0x404", True),
    ("32", "4"): ("MOCK_PAS_EWS_AREA_SIZE", True),
    ("20", "7"): ("borrowed", False),
    ("root", "6"): ("borrowed", False),
    ("21", "4"): ("borrowed", False),
    ("19", "8"): ("borrowed", False),
    ("19", "9"): ("borrowed", False),
    ("13", "10"): ("borrowed", False),
    ("32", "3"): ("MOCK_PAS_EWS_AREA_SIZE", True),
    ("14", "7"): ("MOCK_PAS_RDS_FILTER_RECORD_SIZE", True),
    ("27", "5"): ("MOCK_PAS_TPEG_STATION_SIZE", True),
    ("27", "6"): ("MOCK_PAS_TPEG_STATION_SIZE", True),
    ("9", "5"): ("MOCK_PAS_MAP_MATCHED_POSITION_SIZE", True),
    ("root", "11"): ("opaque", False),
    ("28", "5"): ("MOCK_PAS_VICS_WARNING_SIZE", True),
    ("8", "10"): ("MOCK_PAS_CAN_MESSAGE_SIZE", True),
    ("7", "5"): ("MOCK_PAS_TIMEZONE_SIZE", True),
}

AGGREGATE_SAFE = {
    ("31", "12"): ("MockPasSxmLocation", "7"),
    ("30", "4"): ("MockPasSxmMbr", "16"),
    ("30", "5"): ("MockPasSxmMbr", "16"),
    ("29", "4"): ("MockPasSxmMbr", "14"),
}


def load(path):
    with pathlib.Path(path).open(newline="") as stream:
        return list(csv.DictReader(stream))


def symbol(row):
    api = re.sub(r"[^A-Za-z0-9_]", "_", row["api_name"])
    return "mock_pas_%s_%s_%s" % (row["adapter"], row["slot"], api)


def function_body(source, name):
    start = source.find("uint32_t " + name + "(")
    if start < 0:
        raise ValueError("missing concrete pointer definition: " + name)
    opening = source.find("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise ValueError("unterminated definition: " + name)


def validate(audit, manifest, header, source, smoke, provider):
    if len(audit) != 63 or Counter(r["contract_class"] for r in audit) != {
            "typed_dormant": 63}:
        raise ValueError("audit partition mismatch")
    integrated = {(r["adapter"], r["slot"]): r for r in manifest
                  if r["contract_source"] ==
                  "cross_version_concrete_vtable_audit"}
    if set(integrated) != {(r["adapter"], r["slot"]) for r in audit}:
        raise ValueError("production/audit key mismatch")
    if sum(r["callability"] == "fail_closed_unrecovered_abi"
           for r in manifest) != 0:
        raise ValueError("ABI-unrecovered production row remains")
    for row in audit:
        key = (row["adapter"], row["slot"])
        live = integrated[key]
        if row["contract_class"] == "typed_dormant":
            name = symbol(row)
            if (live["callability"] != "callable_typed_dormant" or
                    live["implementation_seam"] != name or
                    live["emitted"] != "no" or name not in header or
                    name not in source or name not in smoke or name in provider or
                    "..." in live["abi_prototype"]):
                raise ValueError("typed dormant seam mismatch: %r" % (key,))
            if key in POINTER_SAFE:
                extent, copied = POINTER_SAFE[key]
                body = function_body(source, name)
                if copied:
                    if ("memcpy(" not in body or extent not in body or
                            "g_mutate_bytes[0] = 0u" not in smoke or
                            "g_expected_copy_size" not in smoke):
                        raise ValueError("bounded-copy proof missing: %r" % (key,))
                elif ("callback" not in body and
                      "mock_pas_data_exchange_transfer" not in body):
                    raise ValueError("borrowed synchronous seam is not forwarded")
            if key in AGGREGATE_SAFE:
                aggregate, words = AGGREGATE_SAFE[key]
                if (aggregate not in live["abi_prototype"] or
                        live["abi_words_after_self"] != words or
                        "by-value" not in live[
                            "pointer_extent_ownership_consumer"]):
                    raise ValueError("aggregate ABI proof missing: %r" % (key,))
        else:
            expected = ("fail_closed_pointer_contract"
                        if row["contract_class"] == "pointer_fail_closed"
                        else "fail_closed_aggregate_contract")
            guessed = symbol(row)
            if (live["callability"] != expected or
                    live["implementation_seam"] or live["emitted"] != "no" or
                    guessed in header or guessed in source):
                raise ValueError("unsafe row gained implementation: %r" % (key,))


def negative_tests(audit, manifest, header, source, smoke, provider):
    safe = next(r for r in manifest if r["contract_source"] ==
                "cross_version_concrete_vtable_audit" and
                r["callability"] == "callable_typed_dormant")
    mutations = []
    missing = copy.deepcopy(manifest)
    missing.remove(next(r for r in missing if r["adapter"] == safe["adapter"] and
                        r["slot"] == safe["slot"]))
    mutations.append((missing, header, source, smoke, provider))
    aggregate = copy.deepcopy(manifest)
    row = next(r for r in aggregate if (r["adapter"], r["slot"]) ==
               ("31", "12"))
    row["abi_words_after_self"] = "99"
    mutations.append((aggregate, header, source, smoke, provider))
    mutations.append((manifest, header, source,
                      smoke.replace(safe["implementation_seam"], "removed"),
                      provider))
    mutations.append((manifest, header, source, smoke,
                      provider + "\n" + safe["implementation_seam"] + "();\n"))
    bounded = next(r for r in audit
                   if (r["adapter"], r["slot"]) == ("32", "3"))
    mutations.append((manifest, header,
                      source.replace("memcpy(owned, area_10, sizeof(owned));",
                                     "owned[0] = 0u;", 1), smoke, provider))
    del bounded
    for candidate in mutations:
        try:
            validate(audit, *candidate)
        except ValueError:
            continue
        raise ValueError("negative concrete-integration mutation passed")


def main():
    if len(sys.argv) != 7:
        raise SystemExit(
            "usage: checker audit.csv manifest.csv header source smoke provider")
    audit = load(sys.argv[1])
    manifest = load(sys.argv[2])
    header, source, smoke, provider = (
        pathlib.Path(path).read_text() for path in sys.argv[3:])
    validate(audit, manifest, header, source, smoke, provider)
    negative_tests(audit, manifest, header, source, smoke, provider)
    print("Concrete callback integration passed: 63 typed, 0 fail-closed, 0 ABI-unrecovered")
    print("Seventeen pointer-safe seams: bounded copies and synchronous borrowed forwards")


if __name__ == "__main__":
    main()
