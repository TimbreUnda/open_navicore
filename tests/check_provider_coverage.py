#!/usr/bin/env python3
"""Fail-closed coverage gate for recovered provider-facing vtable methods."""

import argparse
import copy
import csv
import pathlib
import re
import sys


FIELDS = [
    "adapter", "slot", "si_name", "recovered_prototype", "argument_shape",
    "implementation_symbol", "vtable", "c_type", "behavior",
    "test_evidence", "lifecycle_rejection", "backend_routing",
]
GPS_KEY = (9, 4)
GPS_PROTOTYPE = "status(self,const void *gps_record_0x120)"
SEMANTIC_KEYS = {
    (1, 9), (8, 3), (8, 4), (8, 5), (8, 6), (8, 8),
    (9, 6), (10, 17), (10, 19), (25, 19),
    (7, 3), (17, 6),
    (13, 3), (13, 4),
}
EVIDENCE_ALIASES = {
    "scalar_telemetry": "assert_telemetry",
    "pointer_owned_copy": "pointer_state",
    "provider_v2_bridge": "test_provider_v2_bridge",
    "gps_owned_copy": "gps_snapshot_after_mutation",
    "hmi_screen_change": "screen_change_reply_count",
    "voice_reply": "voice_reply_count",
}


class CoverageError(Exception):
    pass


def fail(message):
    raise CoverageError(message)


def key(row):
    try:
        return int(row["adapter"]), int(row["slot"])
    except (KeyError, ValueError):
        fail("adapter and slot must be decimal integers")


def read_csv(path, expected_fields=None):
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if expected_fields is not None and reader.fieldnames != expected_fields:
            fail(f"{path}: unexpected header {reader.fieldnames!r}")
        rows = list(reader)
    return rows


def unique_by_key(rows, label):
    result = {}
    for row in rows:
        row_key = key(row)
        if row_key in result:
            fail(f"{label}: duplicate row {row_key[0]}/{row_key[1]}")
        result[row_key] = row
    return result


def authoritative_rows(provider_path, pointer_path):
    provider = unique_by_key(read_csv(provider_path), "provider matrix")
    if len(provider) != 36:
        fail(f"provider matrix: expected 36 rows, found {len(provider)}")

    pointer_rows = unique_by_key(read_csv(pointer_path), "pointer matrix")
    pointer_expected = {
        row_key for row_key, row in provider.items()
        if row["abi_class"] in ("input_pointer", "input_pointers")
    }
    if set(pointer_rows) != pointer_expected:
        fail("pointer matrix keys do not exactly match provider pointer rows")
    for row_key, row in pointer_rows.items():
        if not row.get("typed_prototype") or "..." in row["typed_prototype"]:
            fail(f"pointer matrix: untyped/variadic prototype at {row_key}")
        if not row.get("lifetime_and_copy_rule") or not row.get("max_synchronous_read"):
            fail(f"pointer matrix: missing ownership/bounds evidence at {row_key}")
    return provider, pointer_expected


def parse_vtables(source):
    tables = {}
    pattern = re.compile(
        r"static const PanaGenericVirtual\s+(g_[A-Za-z0-9_]+)"
        r"\[MOCK_DUMMY_VTABLE_SLOTS\]\s*=\s*\{(.*?)\};", re.S)
    for match in pattern.finditer(source):
        body = re.sub(r"/\*.*?\*/", "", match.group(2), flags=re.S)
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
        if len(entries) != 128:
            fail(f"source: {match.group(1)} expands to {len(entries)} slots, not 128")
        tables[match.group(1)] = entries
    return tables


def parse_typechecks(source):
    pattern = re.compile(
        r"MOCK_PROVIDER_TYPECHECK\(\s*(\d+)\s*,\s*(\d+)\s*,\s*"
        r"([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\);", re.S)
    result = {}
    for adapter, slot, symbol, c_type in pattern.findall(source):
        row_key = int(adapter), int(slot)
        if row_key in result:
            fail(f"source: duplicate typecheck {adapter}/{slot}")
        result[row_key] = (symbol, c_type)
    return result


def has_lifecycle_gate(source, symbol):
    macro = re.search(r"DEFINE_TELEM_[A-Z0-9_]+\(\s*" + re.escape(symbol) + r"\s*,", source)
    if macro:
        return True
    definition = re.search(
        r"static\s+uint32_t\s+" + re.escape(symbol) +
        r"\s*\([^;]*?\)\s*\{", source, re.S)
    if not definition:
        return False
    return "mock_lifecycle_acquire" in source[definition.end():definition.end() + 1400]


def expected_behavior(row_key, provider, pointer_keys):
    if row_key in SEMANTIC_KEYS:
        return "semantic"
    if row_key == GPS_KEY or row_key in pointer_keys:
        return "owned_copy"
    return "typed_telemetry"


def validate(rows, provider, pointer_keys, source, project_root):
    manifest = unique_by_key(rows, "provider manifest")
    expected_keys = set(provider) | {GPS_KEY}
    if set(manifest) != expected_keys:
        missing = sorted(expected_keys - set(manifest))
        extra = sorted(set(manifest) - expected_keys)
        fail(f"provider manifest key mismatch; missing={missing}, extra={extra}")
    if len(manifest) != 37:
        fail(f"provider manifest: expected 37 rows, found {len(manifest)}")

    tables = parse_vtables(source)
    typechecks = parse_typechecks(source)
    if set(typechecks) != expected_keys:
        fail("source typecheck catalog does not exactly match authoritative keys")

    symbols = set()
    for row_key, row in manifest.items():
        if any(not row.get(field, "") for field in FIELDS):
            fail(f"provider manifest: empty field at {row_key}")
        expected_name = "SI_GPSData" if row_key == GPS_KEY else provider[row_key]["si_name"]
        expected_prototype = GPS_PROTOTYPE if row_key == GPS_KEY else provider[row_key]["prototype"]
        if row["si_name"] != expected_name:
            fail(f"provider manifest: SI name mismatch at {row_key}")
        if row["recovered_prototype"] != expected_prototype:
            fail(f"provider manifest: prototype mismatch at {row_key}")
        if not row["argument_shape"].startswith("self") or "," in row["argument_shape"]:
            fail(f"provider manifest: invalid canonical argument shape at {row_key}")
        if row["behavior"] != expected_behavior(row_key, provider, pointer_keys):
            fail(f"provider manifest: behavior mismatch at {row_key}")
        if row["lifecycle_rejection"] != "yes":
            fail(f"provider manifest: lifecycle rejection not asserted at {row_key}")

        symbol = row["implementation_symbol"]
        if symbol in symbols:
            fail(f"provider manifest: duplicate implementation symbol {symbol}")
        symbols.add(symbol)
        if symbol == "mock_generic_virtual" or "..." in row["recovered_prototype"]:
            fail(f"provider manifest: generic/variadic implementation at {row_key}")
        if typechecks[row_key] != (symbol, row["c_type"]):
            fail(f"provider manifest: compiler typecheck mismatch at {row_key}")
        table = tables.get(row["vtable"])
        if table is None or row_key[1] >= len(table):
            fail(f"provider manifest: missing vtable at {row_key}")
        if table[row_key[1]] != symbol:
            fail(f"provider manifest: vtable binding mismatch at {row_key}")
        if not has_lifecycle_gate(source, symbol):
            fail(f"provider manifest: no ACTIVE lifecycle gate at {row_key}")

        evidence = row["test_evidence"].split(":", 1)
        if len(evidence) != 2:
            fail(f"provider manifest: malformed test evidence at {row_key}")
        evidence_path = project_root / evidence[0]
        if not evidence_path.is_file():
            fail(f"provider manifest: missing test file at {row_key}")
        anchor = EVIDENCE_ALIASES.get(evidence[1], evidence[1])
        if anchor not in evidence_path.read_text(encoding="utf-8"):
            fail(f"provider manifest: missing test anchor {anchor!r} at {row_key}")


def negative_self_test(rows, provider, pointer_keys, source, project_root):
    cases = []
    duplicate = copy.deepcopy(rows)
    duplicate.append(copy.deepcopy(duplicate[0]))
    cases.append(("duplicate", duplicate))
    missing = copy.deepcopy(rows[:-1])
    cases.append(("missing", missing))
    prototype = copy.deepcopy(rows)
    prototype[0]["recovered_prototype"] = "status(self,wrong)"
    cases.append(("prototype mismatch", prototype))
    generic = copy.deepcopy(rows)
    generic[0]["implementation_symbol"] = "mock_generic_virtual"
    cases.append(("generic implementation", generic))
    for label, mutated in cases:
        try:
            validate(mutated, provider, pointer_keys, source, project_root)
        except CoverageError:
            continue
        fail(f"negative self-test did not reject {label}")
    print(f"Provider coverage negative self-test passed: {len(cases)} corruptions rejected")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("provider_matrix", type=pathlib.Path)
    parser.add_argument("pointer_matrix", type=pathlib.Path)
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        provider, pointer_keys = authoritative_rows(
            args.provider_matrix, args.pointer_matrix)
        rows = read_csv(args.manifest, FIELDS)
        source = args.source.read_text(encoding="utf-8")
        # Evidence paths in the manifest are repository-relative. Keeping the
        # root independent of the manifest's organizational directory lets the
        # checked contract move without rewriting its evidence identities.
        project_root = pathlib.Path(__file__).resolve().parents[1]
        validate(rows, provider, pointer_keys, source, project_root)
        if args.self_test:
            negative_self_test(rows, provider, pointer_keys, source, project_root)
        else:
            print("Provider coverage passed: 36 observed rows + GPS 9/4; all typed and lifecycle-gated")
    except (CoverageError, OSError, csv.Error) as error:
        print(f"provider coverage failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
