#!/usr/bin/env python3
"""Fail closed when a statically reachable provider slot is still generic."""

import argparse
import copy
import csv
import pathlib
import re
import sys


FIELDS = [
    "adapter", "slot", "si_name", "reachability", "abi_words_after_self",
    "abi_prototype", "implementation_symbol", "vtable", "static_evidence",
    "status_behavior", "pointer_semantics",
]

# Independent transcription of the QR39.6 nv_navigation call graph.  Keeping
# this catalog in the verifier means deleting a manifest row cannot silently
# redefine what "complete" means.
REACHABLE_CATALOG = """
root:3/2,4/0,5/1,6/1,7/2,8/1,10/2
1:3/5,5/5,6/2,7/2,8/2,9/5,10/2,12/0
4:3/1,4/1
6:3/4
7:3/2,4/1,5/1
8:3/2,4/1,5/2,6/1,7/2,8/1,9/2,12/2,13/2,14/1
9:3/2,4/1,5/1,6/1
10:3/3,5/3,7/3,8/3,17/3,18/3,19/2
12:3/2,4/1,5/1,6/1,7/3
13:3/2,4/1,5/2,6/2,7/2,8/1,9/2,10/2,11/1,12/1,13/1,14/1
14:3/2,4/1,5/2,6/3,7/2,8/1,9/1
17:4/1,6/2
19:3/3,4/4,5/6,6/4,7/3,8/3,9/3
20:3/4,4/3,5/7,6/3,7/3,8/3
23:3/2
24:3/2
25:3/1,4/2,5/1,7/2,8/5,9/7,10/5,11/1,12/2,13/1,14/3,15/2,16/4,17/3,18/3,19/1,20/1,21/3,22/2,23/2,24/2,26/1,27/0,28/2,29/4,30/4
26:3/1,4/2,5/2
29:3/1,4/1,5/2,6/2
30:3/1,4/1,5/3,6/3,7/3,8/5,9/5,10/4,11/4
31:3/1,4/3,5/1,6/3,7/1,8/1,9/1,10/1,11/1,12/1,13/1,14/1,15/2,16/1
32:3/1
"""
UNREACHABLE = {
    ("21", 3), ("21", 4),
    ("27", 3), ("27", 4), ("27", 5), ("27", 6), ("27", 7),
    ("28", 4),
}


class CheckError(Exception):
    pass


def fail(message):
    raise CheckError(message)


def catalog():
    result = {}
    for raw in REACHABLE_CATALOG.strip().splitlines():
        adapter, values = raw.split(":", 1)
        for item in values.split(","):
            slot, words = item.split("/")
            result[(adapter, int(slot))] = int(words)
    return result


def row_key(row):
    try:
        return row["adapter"], int(row["slot"])
    except (KeyError, ValueError):
        fail("adapter/slot must be a string and decimal integer")


def expand_entries(body):
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    entries = []
    for raw in body.split(","):
        token = raw.strip().replace("(PanaGenericVirtual)", "").strip()
        if not token:
            continue
        shorthand = re.fullmatch(r"V(1|2|4|8|16|32|64)", token)
        if shorthand:
            entries.extend(["mock_generic_virtual"] * int(shorthand.group(1)))
        else:
            entries.append(token)
    return entries


def parse_vtables(source):
    tables = {}
    pattern = re.compile(
        r"static const PanaGenericVirtual\s+(g_[A-Za-z0-9_]+)"
        r"\[MOCK_DUMMY_VTABLE_SLOTS\]\s*=\s*\{(.*?)\};", re.S)
    for match in pattern.finditer(source):
        entries = expand_entries(match.group(2))
        if len(entries) != 128:
            fail(f"{match.group(1)} expands to {len(entries)} slots, not 128")
        tables[match.group(1)] = entries
    root = re.search(
        r"static const PanaRootVtable\s+g_root_vtable\s*=\s*\{\s*\{(.*?)\}\s*\};",
        source, re.S)
    if root is None:
        fail("g_root_vtable not found")
    tables["g_root_vtable"] = expand_entries(root.group(1))
    return tables


def parse_static_definitions(definition):
    result = {}
    for adapter, slot, words in re.findall(
            r"MOCK_STATIC_SLOT\(\s*([A-Za-z0-9_]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)",
            definition):
        key = (adapter, int(slot))
        if key in result:
            fail(f"duplicate static definition {adapter}/{slot}")
        result[key] = int(words)
    return result


def parse_old_typechecks(source):
    return set(re.findall(
        r"MOCK_PROVIDER_TYPECHECK\(\s*\d+\s*,\s*\d+\s*,\s*"
        r"([A-Za-z0-9_]+)\s*,", source))


def has_direct_lifecycle_gate(source, symbol):
    definition = re.search(
        r"static\s+uint32_t\s+" + re.escape(symbol) +
        r"\s*\([^;]*?\)\s*\{", source, re.S)
    return definition is not None and "mock_lifecycle_acquire" in source[
        definition.end():definition.end() + 1800]


def validate(rows, source, static_definition):
    reachable_expected = catalog()
    manifest = {}
    for row in rows:
        key = row_key(row)
        if key in manifest:
            fail(f"duplicate manifest row {key[0]}/{key[1]}")
        if any(not row.get(field, "") for field in FIELDS):
            fail(f"empty manifest field at {key[0]}/{key[1]}")
        manifest[key] = row
    expected = set(reachable_expected) | UNREACHABLE
    if set(manifest) != expected:
        fail(f"manifest key mismatch; missing={sorted(expected-set(manifest))}, "
             f"extra={sorted(set(manifest)-expected)}")

    tables = parse_vtables(source)
    static_defs = parse_static_definitions(static_definition)
    old_typechecks = parse_old_typechecks(source)
    seen_static = set()
    for key, words in reachable_expected.items():
        row = manifest[key]
        if row["reachability"] != "reachable_typed":
            fail(f"reachable slot marked unavailable at {key[0]}/{key[1]}")
        try:
            manifest_words = int(row["abi_words_after_self"])
        except ValueError:
            fail(f"non-numeric ABI word count at {key[0]}/{key[1]}")
        if manifest_words != words:
            fail(f"ABI word-count mismatch at {key[0]}/{key[1]}")
        if "..." in row["abi_prototype"]:
            fail(f"variadic prototype at {key[0]}/{key[1]}")
        if not re.fullmatch(r"nv_navigation:0x[0-9a-f]{8}", row["static_evidence"]):
            fail(f"missing static callsite evidence at {key[0]}/{key[1]}")
        symbol = row["implementation_symbol"]
        if symbol == "mock_generic_virtual" or "generic" in symbol:
            fail(f"reachable slot is generic at {key[0]}/{key[1]}")
        table = tables.get(row["vtable"])
        if table is None or key[1] >= len(table) or table[key[1]] != symbol:
            fail(f"vtable binding mismatch at {key[0]}/{key[1]}")
        if symbol.startswith("mock_static_"):
            if static_defs.get(key) != words:
                fail(f"typed static definition mismatch at {key[0]}/{key[1]}")
            seen_static.add(key)
        elif symbol not in old_typechecks and not has_direct_lifecycle_gate(source, symbol):
            fail(f"no typed/lifecycle-gated implementation for {symbol}")
    if seen_static != set(static_defs):
        fail("static definition file has extra or unused slot entries")
    if "mock_lifecycle_acquire(&lease)" not in source[
            source.find("mock_static_provider_accept"):source.find("#define DEFINE_STATIC_0")]:
        fail("generated static methods do not share an ACTIVE lifecycle gate")

    for key in UNREACHABLE:
        row = manifest[key]
        if row["reachability"] != "unreachable_stock_adapter":
            fail(f"unreachable row is not classified as such at {key}")
        if row["abi_words_after_self"] != "unknown":
            fail(f"unreachable ABI must remain explicitly unknown at {key}")
        if row["implementation_symbol"] != "mock_generic_virtual" or row["vtable"] != "g_dummy_vtable":
            fail(f"unreachable row unexpectedly claims a typed binding at {key}")
    for adapter in (21, 27, 28):
        if f"adapter == MOCK_UNAVAILABLE_ADAPTER_{adapter}" not in source:
            fail(f"getAdapter no longer rejects unreachable adapter {adapter}")
    get_adapter_match = re.search(
        r"static uint32_t mock_get_adapter\s*\([^;]*?\)\s*\{(.*?)\n\}",
        source, re.S)
    if get_adapter_match is None or "*interface_out = NULL" not in get_adapter_match.group(1):
        fail("getAdapter no longer initializes its output before rejection")


def negative_self_test(rows, source, static_definition):
    mutations = []
    mutations.append(("missing row", copy.deepcopy(rows[:-1]), source, static_definition))
    duplicate = copy.deepcopy(rows)
    duplicate.append(copy.deepcopy(duplicate[0]))
    mutations.append(("duplicate row", duplicate, source, static_definition))
    generic = copy.deepcopy(rows)
    generic[0]["implementation_symbol"] = "mock_generic_virtual"
    mutations.append(("reachable generic", generic, source, static_definition))
    unavailable = copy.deepcopy(rows)
    unavailable[0]["reachability"] = "unreachable_stock_adapter"
    mutations.append(("reachable marked unavailable", unavailable, source, static_definition))
    wrong_arity = copy.deepcopy(rows)
    wrong_arity[0]["abi_words_after_self"] = "99"
    mutations.append(("wrong ABI arity", wrong_arity, source, static_definition))
    variadic = copy.deepcopy(rows)
    variadic[0]["abi_prototype"] = "uint32_t(self;...)"
    mutations.append(("variadic ABI", variadic, source, static_definition))
    bad_source = source.replace(
        "(PanaGenericVirtual)mock_static_1_3",
        "(PanaGenericVirtual)mock_generic_virtual", 1)
    mutations.append(("generic vtable binding", copy.deepcopy(rows), bad_source, static_definition))
    for label, changed_rows, changed_source, changed_def in mutations:
        try:
            validate(changed_rows, changed_source, changed_def)
        except CheckError:
            continue
        fail(f"negative self-test did not reject {label}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("static_definition", type=pathlib.Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        with args.manifest.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != FIELDS:
                fail(f"unexpected manifest header {reader.fieldnames!r}")
            rows = list(reader)
        source = args.source.read_text(encoding="utf-8")
        definition = args.static_definition.read_text(encoding="utf-8")
        validate(rows, source, definition)
        if args.self_test:
            negative_self_test(rows, source, definition)
            print("Static provider-slot negative tests passed: 7 corruptions rejected")
        else:
            print("Static provider-slot coverage passed: 140 reachable typed, 8 stock-unreachable explicit")
    except (CheckError, OSError, csv.Error) as error:
        print(f"static provider-slot coverage failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
