#!/usr/bin/env python3
"""Independently guard the 30 contracts recovered from owned dense thunks."""

import copy
import csv
import pathlib
import sys

FIELDS = [
    "adapter", "slot", "ci_name", "state", "abi_words_after_self",
    "abi_prototype", "pointer_contract", "producer_evidence",
    "consumer_evidence", "implementation_symbol", "emitted",
]

WORDS = {
    ("10", "20"): 3, ("1", "8"): 2, ("1", "11"): 4,
    ("19", "7"): 3, ("19", "11"): 10, ("root", "9"): 2,
    ("31", "8"): 2, ("31", "15"): 3,
    ("25", "7"): 3, ("25", "8"): 2, ("25", "9"): 2,
    ("25", "10"): 2, ("25", "11"): 3, ("25", "12"): 3,
    ("25", "13"): 3, ("25", "14"): 5, ("25", "15"): 2,
    ("25", "16"): 2, ("25", "17"): 2, ("25", "18"): 3,
    ("25", "20"): 6, ("25", "21"): 3, ("25", "22"): 4,
    ("25", "26"): 2, ("25", "27"): 2, ("25", "28"): 2,
    ("25", "29"): 2, ("25", "30"): 2, ("25", "35"): 5,
    ("25", "34"): 3,
    ("17", "4"): 1, ("19", "6"): 3,
    ("31", "6"): 1, ("31", "10"): 5, ("31", "13"): 3,
}

FAIL_CLOSED = set()


def validate(rows, header_text, source_text, provider_text=""):
    keyed = {(r["adapter"], r["slot"]): r for r in rows}
    if len(rows) != 35 or len(keyed) != 35 or set(keyed) != set(WORDS):
        raise ValueError("expected exactly the independent 35-row contract catalog")
    for key, words in WORDS.items():
        row = keyed[key]
        if int(row["abi_words_after_self"]) != words:
            raise ValueError("AAPCS word mismatch for %r" % (key,))
        if not row["producer_evidence"] or not row["consumer_evidence"]:
            raise ValueError("missing two-sided provenance for %r" % (key,))
        if row["emitted"] != "no":
            raise ValueError("static contract unexpectedly emits for %r" % (key,))
        if key in FAIL_CLOSED:
            if row["state"] != "static_pointer_fail_closed":
                raise ValueError("unsafe pointer row enabled for %r" % (key,))
            if row["implementation_symbol"] or "do not call" not in row["pointer_contract"]:
                raise ValueError("fail-closed row has a callable seam for %r" % (key,))
        else:
            symbol = row["implementation_symbol"]
            if row["state"] != "static_typed_not_emitted" or not symbol:
                raise ValueError("safe row lacks a typed dormant seam for %r" % (key,))
            if symbol not in header_text or symbol not in source_text:
                raise ValueError("typed seam is not declared and defined: %s" % symbol)
            if symbol in provider_text:
                raise ValueError("dormant seam has a provider-side emitter: %s" % symbol)
    return keyed


def read(path, header_path, source_path):
    with pathlib.Path(path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != FIELDS:
            raise ValueError("unexpected callback contract header")
        rows = list(reader)
    return rows, pathlib.Path(header_path).read_text(), pathlib.Path(source_path).read_text()


def reconcile(rows, runtime_path, additions_path):
    recovered = {(r["adapter"], r["slot"]) for r in rows}
    runtime = {(r["adapter"], r["slot"])
               for r in csv.DictReader(pathlib.Path(runtime_path).open(newline=""))}
    additions = {(r["adapter"], r["slot"])
                 for r in csv.DictReader(pathlib.Path(additions_path).open(newline=""))}
    if len(runtime) != 61 or len(additions) != 7:
        raise ValueError("61 runtime + 7 prior-static baselines changed")
    if runtime & additions or runtime & recovered or additions & recovered:
        raise ValueError("callback catalogs overlap")
    if len(runtime | additions | recovered) != 103:
        raise ValueError("callback reconciliation is not 61 + 7 + 35")


def negative_tests(rows, header_text, source_text, provider_text):
    mutations = []
    mutations.append(rows[:-1])
    duplicate = copy.deepcopy(rows)
    duplicate.append(copy.deepcopy(duplicate[0]))
    mutations.append(duplicate)
    wrong_arity = copy.deepcopy(rows)
    wrong_arity[0]["abi_words_after_self"] = "99"
    mutations.append(wrong_arity)
    if FAIL_CLOSED:
        enabled_pointer = copy.deepcopy(rows)
        row = next(r for r in enabled_pointer
                   if (r["adapter"], r["slot"]) in FAIL_CLOSED)
        row["state"] = "static_typed_not_emitted"
        mutations.append(enabled_pointer)
    emitted = copy.deepcopy(rows)
    emitted[0]["emitted"] = "yes"
    mutations.append(emitted)
    missing_symbol = copy.deepcopy(rows)
    row = next(r for r in missing_symbol if r["implementation_symbol"])
    row["implementation_symbol"] = "open_navi_pas_missing_contract"
    mutations.append(missing_symbol)
    for mutation in mutations:
        try:
            validate(mutation, header_text, source_text)
        except ValueError:
            continue
        raise ValueError("negative callback-contract mutation unexpectedly passed")
    symbol = next(r["implementation_symbol"] for r in rows
                  if r["implementation_symbol"])
    try:
        validate(rows, header_text, source_text,
                 provider_text + "\n" + symbol + "();\n")
    except ValueError:
        return
    raise ValueError("provider-emitter negative mutation unexpectedly passed")


def main():
    if len(sys.argv) != 7:
        raise SystemExit("usage: checker contracts runtime additions header source provider")
    rows, header_text, source_text = read(sys.argv[1], sys.argv[4], sys.argv[5])
    provider_text = pathlib.Path(sys.argv[6]).read_text()
    validate(rows, header_text, source_text, provider_text)
    reconcile(rows, sys.argv[2], sys.argv[3])
    negative_tests(rows, header_text, source_text, provider_text)
    print("Static callback contracts passed: 35 typed dormant + 0 pointer fail-closed")
    print("Callback reconciliation passed: 61 runtime + 7 prior static + 35 recovered")
    print("Static callback contract negative-mutation self-test passed")


if __name__ == "__main__":
    main()
