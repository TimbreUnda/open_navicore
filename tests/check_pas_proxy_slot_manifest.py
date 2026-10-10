#!/usr/bin/env python3
"""Independent guard for the complete QR39.6 PAS callback proxy surface."""

import copy
import csv
import hashlib
import pathlib
import re
import sys
from collections import Counter, defaultdict


RAW_KEY_DIGEST = "da0aa737f0f6b9ed9692af9422f606dd11b6af409bb05cd2c1d5394704e81d44"
PURE_VIRTUAL = "080848a8"
EXPECTED_PROXY_LAYOUT = {
    "IF_ServerTTSProxy": ("24", "081ef238", "081ef240", 4),
    "IF_ServerTTI_TPEG_EWSProxy": ("32", "081ef320", "081ef328", 4),
    "IF_ServerInstrumentClusterProxy": ("17", "081ef3e0", "081ef3e8", 7),
    "IF_ServerConnectivityProxy": ("20", "081ef4b0", "081ef4b8", 9),
    "IF_ServerVoiceRecognitionProxy": ("25", "081ef590", "081ef598", 35),
    "IF_ServerTTI_SXM_AGWProxy": ("31", "081ef750", "081ef758", 15),
    "IF_ServerTTI_RDSProxy": ("14", "081ef860", "081ef868", 7),
    "IF_ServerHMIIntegrationProxy": ("1", "081ef940", "081ef948", 14),
    "IF_ServerTTI_TPEGProxy": ("27", "081efa50", "081efa58", 6),
    "IF_ServerGPSDataProxy": ("9", "081efb08", "081efb10", 6),
    "IF_ServerSoftwareInfrastructureProxy": ("root", "081efbd8", "081efbe0", 12),
    "IF_ServerLoggingProxy": ("6", "081efea8", "081efeb0", 4),
    "IF_ServerDiagnosisProxy": ("12", "081eff68", "081eff70", 8),
    "IF_ServerHMIControl_RotaryKnobProxy": ("23", "081f0068", "081f0070", 3),
    "IF_ServerHMIControl_HardkeyProxy": ("4", "081f0118", "081f0120", 3),
    "IF_ServerTTI_LiveTrafficProxy": ("21", "081f01e0", "081f01e8", 5),
    "IF_ServerTTI_FM_VICSProxy": ("28", "081f02a0", "081f02a8", 5),
    "IF_ServerInterApplicationDataExchangeProxy": ("19", "081f0348", "081f0350", 11),
    "IF_ServerVehicleDataProxy": ("8", "081f0538", "081f0540", 12),
    "IF_ServerAudioHandlingProxy": ("10", "081f0638", "081f0640", 20),
    "IF_ServerPhoneProxy": ("26", "081f0788", "081f0790", 5),
    "IF_ServerTTI_SXM_ApogeeProxy": ("30", "081f0848", "081f0850", 6),
    "IF_ServerTTI_SXM_ParkingProxy": ("29", "081f0928", "081f0930", 4),
    "IF_ServerDiagnosis_NavigationProxy": ("13", "081f0ae8", "081f0af0", 13),
    "IF_ServerInternationalizationProxy": ("7", "081f0c60", "081f0c68", 6),
}
COMMON_READY = {
    "4", "6", "7", "8", "9", "10", "12", "13", "14", "17", "19",
    "20", "23", "24", "25", "26", "29", "30", "31", "32",
}
EXPECTED_COUNTS = {
    "non_application_destructor": 50,
    "callable_typed_emitted": 80,
    "callable_typed_dormant": 119,
}
EXPECTED_CROSS_VERSION = {
    ("10", "4"): ("requestAudioOutSourceDescriptor", "2", "0812e978"),
    ("10", "5"): ("requestAudioInSourceDescriptor", "2", "0812e458"),
    ("10", "6"): ("requestAudioSourceAllocation", "2", "0812fee8"),
    ("10", "7"): ("requestAudioSourcePreparation", "2", "0812e868"),
    ("10", "8"): ("signalDeallocateAudioSource", "1", "0812db5c"),
    ("10", "9"): ("replyAudioSourceDeactivation", "3", "0812d8e4"),
    ("10", "11"): ("requestAudioSourceStatus", "2", "0812e3d4"),
    ("10", "12"): ("signalEnableServerAudioSource", "1", "0812e06c"),
    ("10", "13"): ("signalDisableServerAudioSource", "1", "0812dfec"),
    ("10", "14"): ("requestAudioSourceEnableStatus", "2", "0812e350"),
    ("10", "15"): ("signalAudioSourceComfortVolume", "2", "0812e2cc"),
    ("10", "16"): ("signalEnableAudioSourceVolumeSeparation", "1", "0812e24c"),
    ("10", "17"): ("signalDisableAudioSourceVolumeSeparation", "1", "0812e148"),
    ("10", "18"): ("requestAudioSourceVolumeSeparationStatus", "2", "0812e1c8"),
}
EXPECTED_CONCRETE_POINTER_ARGS = {
    ("24", "3"): ("uint32_t handle, uint32_t source, uint32_t text_type, "
                     "const char *tts_text"),
    ("32", "4"): "const void *old_area_10, const void *new_area_10",
    ("20", "7"): ("uint32_t handle, uint32_t service, const wchar_t *url, "
                     "uint32_t method, uint32_t request_size, const void *request_data, "
                     "uint32_t response_size, const void *response_data"),
    ("root", "6"): "const wchar_t *path",
    ("21", "4"): ("uint32_t handle, const wchar_t *url, uint32_t method, "
                     "uint32_t request_size, const void *request_data, "
                     "uint32_t response_size, const void *response_data"),
    ("19", "8"): ("uint32_t handle, uint32_t object_id, uint32_t object_type, "
                     "const wchar_t *description, uint64_t size, uint32_t reply_type"),
    ("19", "9"): ("uint32_t handle, uint32_t object_id, uint64_t size, "
                     "const void *data, uint64_t total_size, uint32_t abort"),
    ("13", "10"): "int32_t handle, const wchar_t *versions",
    ("32", "3"): "uint32_t handle, const void *area_10",
    ("14", "7"): "uint32_t count, const void *filters_6x20",
    ("27", "5"): "const void *station_0x30",
    ("27", "6"): "uint32_t count, const void *stations_0x30x4",
    ("9", "5"): "const void *position_0x38",
    ("root", "11"): "uint32_t word0, uint32_t word1, const void *opaque",
    ("28", "5"): ("uint32_t word0, uint32_t word1, uint32_t word2, "
                      "uint32_t minute, const void *warning_0x180"),
    ("8", "10"): "uint32_t word0, uint32_t word1, const void *payload_8",
    ("7", "5"): "uint32_t handle, const void *timezone_0x22c",
}
EXPECTED_CONCRETE_AGGREGATE_ARGS = {
    ("31", "12"): (
        "uint32_t handle, MockPasSxmLocation location, uint32_t radius, "
        "uint32_t radius_unit",
        "16-byte by-value location is two f64 values; synchronous input-only "
        "copy; AAPCS splits it across r2/r3 and two stack words"),
    ("30", "4"): (
        "uint32_t handle, uint16_t type, MockPasSxmMbr mbr, "
        "uint32_t collection_index",
        "48-byte by-value MBR is six f64 values; synchronous input-only copy; "
        "8-byte alignment leaves r3 unused and places MBR at stack+0"),
    ("30", "5"): (
        "uint32_t handle, uint16_t type, MockPasSxmMbr mbr, "
        "uint32_t collection_index",
        "48-byte by-value MBR is six f64 values; synchronous input-only copy; "
        "8-byte alignment leaves r3 unused and places MBR at stack+0"),
    ("29", "4"): (
        "uint32_t handle, MockPasSxmMbr mbr, uint16_t collection_index",
        "48-byte by-value MBR is six f64 values; synchronous input-only copy; "
        "AAPCS splits it across r2/r3 and stack+0..0x27"),
}
FIELDS = [
    "proxy", "adapter", "interface", "vtable", "address_point", "slot",
    "entry", "target", "target_name", "slot_kind", "contract_key",
    "api_name", "contract_source", "callability", "abi_words_after_self",
    "abi_prototype", "pointer_extent_ownership_consumer",
    "return_status_behavior", "implementation_seam", "emitted",
    "fail_closed_reason", "evidence",
]


def load(path):
    with open(path, newline="") as stream:
        return list(csv.DictReader(stream))


def canonical_raw_digest(rows):
    fields = ["proxy", "vtable", "address_point", "slot", "entry", "target"]
    data = "\n".join("|".join(row[field] for field in fields) for row in rows)
    return hashlib.sha256(data.encode()).hexdigest()


def proxy_name(qualified):
    match = re.fullmatch(r"PanaNav::([^:]+)::vtable", qualified)
    if not match:
        raise ValueError("malformed proxy name")
    return match.group(1)


def prototype_words(prototype):
    match = re.search(r"\((.*)\)", prototype)
    if not match:
        raise ValueError("malformed prototype")
    args = [arg.strip() for arg in match.group(1).split(",")]
    if not args or "self" not in args[0]:
        raise ValueError("prototype lacks self")
    return str(len(args) - 1)


def catalog(matrix_path, coverage_path, additions_path, contracts_path,
            cross_version_path, concrete_path):
    matrix_rows = load(matrix_path)
    coverage_rows = load(coverage_path)
    if len(matrix_rows) != 61 or len(coverage_rows) != 61:
        raise ValueError("runtime catalog is not 61 rows")
    matrix = {(r["adapter"], r["slot"]): r for r in matrix_rows}
    result = {}
    for row in coverage_rows:
        key = (row["adapter"], row["slot"])
        if key not in matrix or key in result:
            raise ValueError("runtime catalog key mismatch")
        result[key] = {
            "source": "runtime_matrix",
            "words": prototype_words(matrix[key]["prototype"]),
            "prototype": matrix[key]["prototype"],
            "pointer": matrix[key]["pointer_contract"],
            "symbol": row["symbol"],
            "callability": "callable_typed_emitted",
        }
    for source_name, path in (
        ("direct_static_dispatch", additions_path),
        ("owned_thunk_static_contract", contracts_path),
    ):
        for row in load(path):
            key = (row["adapter"], row["slot"])
            if key in result:
                raise ValueError("contract catalogs overlap")
            unsafe = row["state"] == "static_pointer_fail_closed"
            result[key] = {
                "source": source_name,
                "words": row["abi_words_after_self"],
                "prototype": row["abi_prototype"],
                "pointer": row["pointer_contract"],
                "symbol": row["implementation_symbol"],
                "callability": (
                    "fail_closed_pointer_contract" if unsafe
                    else "callable_typed_dormant"
                ),
            }
    cross_rows = load(cross_version_path)
    if {(r["adapter"], r["slot"]) for r in cross_rows} != set(EXPECTED_CROSS_VERSION):
        raise ValueError("cross-version Audio slot set mismatch")
    for row in cross_rows:
        key = (row["adapter"], row["slot"])
        name, words, target = EXPECTED_CROSS_VERSION[key]
        if key in result:
            raise ValueError("cross-version contract overlaps prior catalog")
        if (row["api_name"] != name or row["abi_words_after_self"] != words or
                prototype_words(row["abi_prototype"]) != words or
                row["qr396_vtable"] != "081f06f0" or
                row["qr40_vtable"] != "081f06f0" or
                row["qr396_target"] != target or row["qr40_target"] != target or
                row["emitted"] != "no" or
                not row["implementation_symbol"].startswith("mock_pas_10_") or
                "*" in row["abi_prototype"].split(",", 1)[-1] or
                "scalar" not in row["pointer_contract"]):
            raise ValueError("cross-version Audio evidence mismatch for %r" % (key,))
        result[key] = {
            "source": "cross_version_concrete_consumer",
            "words": words,
            "prototype": row["abi_prototype"],
            "pointer": row["pointer_contract"],
            "symbol": row["implementation_symbol"],
            "callability": "callable_typed_dormant",
        }
    concrete_rows = load(concrete_path)
    if len(concrete_rows) != 63 or Counter(
            row["contract_class"] for row in concrete_rows) != {
                "typed_dormant": 63,
            }:
        raise ValueError("concrete audit partition mismatch")
    for row in concrete_rows:
        key = (row["adapter"], row["slot"])
        if key in result or row["target"] != row["qr40_target"] or \
                row["body_match"] != "identical":
            raise ValueError("concrete audit overlap or cross-version mismatch")
        if row["contract_class"] == "typed_dormant":
            aggregate = EXPECTED_CONCRETE_AGGREGATE_ARGS.get(key)
            arguments = EXPECTED_CONCRETE_POINTER_ARGS.get(key)
            if aggregate is not None:
                arguments, pointer = aggregate
            elif arguments is None:
                arguments = ", ".join(
                    "uint32_t word%d" % word
                    for word in range(int(row["abi_words_after_self"])))
                pointer = "fixed scalar words only; no pointer ownership"
            else:
                pointer = {
                    ("24", "3"): "borrow NUL-terminated CSTR8 synchronously; consumer accepts 1..1027 bytes and copies into 0x404-byte payload",
                    ("32", "4"): "borrow two exact 10-byte areas synchronously; consumer compares 10 bytes and sends the second",
                    ("20", "7"): "borrow NUL-terminated CSTR16 URL and exact request_size/response_size byte buffers synchronously; consumer copies all inputs",
                    ("root", "6"): "borrow NUL-terminated CSTR16 path synchronously; consumer converts and consumes it before return",
                    ("21", "4"): "borrow NUL-terminated CSTR16 URL and exact request_size/response_size byte buffers synchronously; consumer copies all inputs",
                    ("19", "8"): "borrow NUL-terminated CSTR16 synchronously; uint64_t is naturally AAPCS-aligned on stack",
                    ("19", "9"): "borrow exactly size bytes synchronously; size must fit the concrete 0xfc0-byte consumer buffer; aligned UINT64 arguments preserve AAPCS padding",
                    ("13", "10"): "borrow NUL-terminated CSTR16 synchronously; consumer converts at most 0x4d9 bytes into a 0x4e0-byte payload",
                    ("32", "3"): "copy exactly 10 bytes before synchronous call; no retention",
                    ("14", "7"): "copy min(count,20)*6 bytes before synchronous call; no retention",
                    ("27", "5"): "copy exactly 0x30 bytes before synchronous call; no retention",
                    ("27", "6"): "for count<=4 copy exactly count*0x30 bytes before synchronous call; no retention",
                    ("9", "5"): "copy exactly 0x38 bytes before synchronous call; no retention",
                    ("root", "11"): "opaque pointer is not dereferenced, copied, or retained; forward synchronously only",
                    ("28", "5"): "copy exactly 0x180 bytes before synchronous call; no retention",
                    ("8", "10"): "copy exactly 8 bytes before synchronous call; no retention",
                    ("7", "5"): "copy exactly 0x22c bytes before synchronous call; no retention",
                }[key]
            comma = ", " if arguments else ""
            prototype = "uint32_t (*)(void *self%s%s)" % (comma, arguments)
            symbol = "mock_pas_%s_%s_%s" % (
                row["adapter"], row["slot"],
                re.sub(r"[^A-Za-z0-9_]", "_", row["api_name"]))
            result[key] = {
                "source": "cross_version_concrete_vtable_audit",
                "words": row["abi_words_after_self"],
                "prototype": prototype,
                "pointer": pointer,
                "symbol": symbol,
                "callability": "callable_typed_dormant",
            }
        else:
            pointer = row["contract_class"] == "pointer_fail_closed"
            result[key] = {
                "source": "cross_version_concrete_vtable_audit",
                "words": row["abi_words_after_self"],
                "prototype": "non-callable; unsafe contract intentionally has no C prototype",
                "pointer": "do not call; %s; %s" %
                           (row["ownership"], row["consumer_access"]),
                "symbol": "",
                "callability": ("fail_closed_pointer_contract" if pointer else
                                "fail_closed_aggregate_contract"),
            }
    if len(result) != 180:
        raise ValueError("expected 180 recovered contract keys")
    return result


def validate_raw(raw):
    if len(raw) != 249 or canonical_raw_digest(raw) != RAW_KEY_DIGEST:
        raise ValueError("QR39.6 raw proxy inventory identity mismatch")
    grouped = defaultdict(list)
    for row in raw:
        grouped[proxy_name(row["proxy"])].append(row)
    if set(grouped) != set(EXPECTED_PROXY_LAYOUT):
        raise ValueError("proxy set mismatch")
    for proxy, (adapter, vtable, point, maximum) in EXPECTED_PROXY_LAYOUT.items():
        del adapter
        group = grouped[proxy]
        if len(group) != maximum + 1:
            raise ValueError("slot count mismatch for " + proxy)
        for slot, row in enumerate(group):
            if (row["slot"] != str(slot) or row["vtable"] != vtable or
                    row["address_point"] != point or
                    int(row["entry"], 16) != int(point, 16) + slot * 4):
                raise ValueError("layout mismatch for %s/%d" % (proxy, slot))
            if slot >= 2 and (row["target"] != PURE_VIRTUAL or
                              "pure_virtual" not in row["function"]):
                raise ValueError("application slot is not abstract pure virtual")
            if slot < 2 and row["target"] == PURE_VIRTUAL:
                raise ValueError("destructor unexpectedly targets pure virtual")


def validate(manifest, raw, contracts, header, source, provider_source):
    if len(manifest) != 249 or list(manifest[0]) != FIELDS:
        raise ValueError("manifest shape mismatch")
    if Counter(r["callability"] for r in manifest) != EXPECTED_COUNTS:
        raise ValueError("callability partition mismatch")
    if Counter(r["slot_kind"] for r in manifest) != {
            "destructor": 50, "pure_virtual_application": 199}:
        raise ValueError("slot-kind partition mismatch")
    raw_by_key = {(proxy_name(r["proxy"]), r["slot"]): r for r in raw}
    seen = set()
    represented = set()
    for row in manifest:
        key = (row["proxy"], row["slot"])
        if key in seen or key not in raw_by_key:
            raise ValueError("duplicate or invented manifest slot")
        seen.add(key)
        raw_row = raw_by_key[key]
        proxy_layout = EXPECTED_PROXY_LAYOUT[row["proxy"]]
        if row["adapter"] != proxy_layout[0]:
            raise ValueError("adapter/proxy identity mismatch")
        for field in ("vtable", "address_point", "entry", "target"):
            if row[field] != raw_row[field]:
                raise ValueError("raw evidence mismatch for " + field)
        slot = int(row["slot"])
        if slot < 2:
            if (row["callability"] != "non_application_destructor" or
                    row["implementation_seam"] or row["contract_key"]):
                raise ValueError("destructor exposed as callback")
            continue
        direct_key = (row["adapter"], row["slot"])
        contract_key = direct_key
        contract = contracts.get(direct_key)
        if contract is None and row["slot"] == "2" and row["adapter"] in COMMON_READY:
            contract_key = ("all", "2")
            contract = contracts[contract_key]
        if contract is None:
            raise ValueError("application slot lacks a recovered contract")
        represented.add(contract_key)
        if row["contract_key"] != "/".join(contract_key):
            raise ValueError("wrong contract key")
        for field, expected in (
            ("contract_source", contract["source"]),
            ("callability", contract["callability"]),
            ("abi_words_after_self", contract["words"]),
            ("abi_prototype", contract["prototype"]),
            ("pointer_extent_ownership_consumer", contract["pointer"]),
            ("implementation_seam", contract["symbol"]),
        ):
            if row[field] != expected:
                raise ValueError("contract mismatch for %s: %s" % (field, contract_key))
        if row["callability"].startswith("callable_typed"):
            symbol = row["implementation_seam"]
            if (not symbol or "generic" in symbol.lower() or symbol not in header or
                    symbol not in source or "..." in row["abi_prototype"]):
                raise ValueError("callable slot lacks a fixed typed seam")
            if (row["contract_source"] ==
                    "cross_version_concrete_vtable_audit" and
                    symbol in provider_source):
                raise ValueError("dormant concrete seam gained a provider emitter")
        else:
            if row["implementation_seam"] or "do not call" not in row[
                    "pointer_extent_ownership_consumer"]:
                raise ValueError("unsafe pointer contract became callable")
    if seen != set(raw_by_key) or represented != set(contracts):
        raise ValueError("surface or recovered-contract reconciliation mismatch")


def negative_tests(manifest, raw, contracts, header, source, provider_source):
    mutations = []
    mutations.append(copy.deepcopy(manifest[:-1]))
    duplicate = copy.deepcopy(manifest); duplicate[-1] = copy.deepcopy(duplicate[0]); mutations.append(duplicate)
    aggregate = copy.deepcopy(manifest); next(r for r in aggregate if (r["adapter"], r["slot"]) == ("31", "12"))["abi_words_after_self"] = "99"; mutations.append(aggregate)
    unsafe = copy.deepcopy(manifest); next(r for r in unsafe if (r["adapter"], r["slot"]) == ("30", "4"))["implementation_seam"] = "mock_pas_forbidden"; mutations.append(unsafe)
    arity = copy.deepcopy(manifest); next(r for r in arity if r["callability"] == "callable_typed_dormant")["abi_words_after_self"] = "99"; mutations.append(arity)
    generic = copy.deepcopy(manifest); next(r for r in generic if r["callability"] == "callable_typed_dormant")["implementation_seam"] = "mock_pas_generic"; mutations.append(generic)
    destructor = copy.deepcopy(manifest); destructor[0]["callability"] = "callable_typed_emitted"; mutations.append(destructor)
    contract_key = copy.deepcopy(manifest); next(r for r in contract_key if r["contract_key"])["contract_key"] = "31/99"; mutations.append(contract_key)
    raw_link = copy.deepcopy(manifest); raw_link[2]["target"] = "deadbeef"; mutations.append(raw_link)
    for mutation in mutations:
        try:
            validate(mutation, raw, contracts, header, source, provider_source)
        except ValueError:
            continue
        raise ValueError("negative manifest mutation unexpectedly passed")
    dormant_symbol = next(
        row["implementation_seam"] for row in manifest
        if row["contract_source"] == "cross_version_concrete_vtable_audit" and
        row["callability"] == "callable_typed_dormant")
    try:
        validate(manifest, raw, contracts, header, source,
                 provider_source + "\n" + dormant_symbol + "();\n")
    except ValueError:
        pass
    else:
        raise ValueError("provider-emitter negative mutation unexpectedly passed")


def main():
    if len(sys.argv) != 12:
        raise SystemExit(
            "usage: check_pas_proxy_slot_manifest.py manifest raw matrix coverage "
            "additions contracts cross-version concrete-audit header source provider-source")
    manifest = load(sys.argv[1])
    raw = load(sys.argv[2])
    validate_raw(raw)
    contracts = catalog(*sys.argv[3:9])
    header = pathlib.Path(sys.argv[9]).read_text()
    source = pathlib.Path(sys.argv[10]).read_text()
    provider_source = pathlib.Path(sys.argv[11]).read_text()
    validate(manifest, raw, contracts, header, source, provider_source)
    negative_tests(manifest, raw, contracts, header, source, provider_source)
    print("PAS proxy closure passed: 25 vtables / 249 slots")
    print("50 destructors; 199 application slots = 199 typed + 0 fail-closed")
    print("Recovered contracts reconciled: 61 runtime + 7 direct static + 35 owned-thunk + 14 Audio + 63 concrete-vtable")


if __name__ == "__main__":
    main()
