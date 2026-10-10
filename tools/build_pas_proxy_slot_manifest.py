#!/usr/bin/env python3
"""Join QR39.6 proxy vtables to the independently recovered callback contracts."""

import csv
import re
import sys


PROXY_IDENTITY = {
    "IF_ServerSoftwareInfrastructureProxy": ("root", "SI_SoftwareInfrastructure"),
    "IF_ServerHMIIntegrationProxy": ("1", "SI_HMIIntegration"),
    "IF_ServerHMIControl_HardkeyProxy": ("4", "SI_HMIControl_Hardkey"),
    "IF_ServerLoggingProxy": ("6", "SI_Logging"),
    "IF_ServerInternationalizationProxy": ("7", "SI_Internationalization"),
    "IF_ServerVehicleDataProxy": ("8", "SI_VehicleData"),
    "IF_ServerGPSDataProxy": ("9", "SI_GPSData"),
    "IF_ServerAudioHandlingProxy": ("10", "SI_AudioHandling"),
    "IF_ServerDiagnosisProxy": ("12", "SI_Diagnosis"),
    "IF_ServerDiagnosis_NavigationProxy": ("13", "SI_Diagnosis_Navigation"),
    "IF_ServerTTI_RDSProxy": ("14", "SI_TTI_RDS"),
    "IF_ServerInstrumentClusterProxy": ("17", "SI_InstrumentCluster"),
    "IF_ServerInterApplicationDataExchangeProxy": ("19", "SI_InterApplicationDataExchange"),
    "IF_ServerConnectivityProxy": ("20", "SI_Connectivity"),
    "IF_ServerTTI_LiveTrafficProxy": ("21", "SI_TTI_LiveTraffic"),
    "IF_ServerHMIControl_RotaryKnobProxy": ("23", "SI_HMIControl_RotaryKnob"),
    "IF_ServerTTSProxy": ("24", "SI_TTS"),
    "IF_ServerVoiceRecognitionProxy": ("25", "SI_VoiceRecognition"),
    "IF_ServerPhoneProxy": ("26", "SI_Phone"),
    "IF_ServerTTI_TPEGProxy": ("27", "SI_TTI_TPEG"),
    "IF_ServerTTI_FM_VICSProxy": ("28", "SI_TTI_FM_VICS"),
    "IF_ServerTTI_SXM_ParkingProxy": ("29", "SI_TTI_SXM_Parking"),
    "IF_ServerTTI_SXM_ApogeeProxy": ("30", "SI_TTI_SXM_Apogee"),
    "IF_ServerTTI_SXM_AGWProxy": ("31", "SI_TTI_SXM_AGW"),
    "IF_ServerTTI_TPEG_EWSProxy": ("32", "SI_TTI_TPEG_EWS"),
}

# Exactly the adapters named by the runtime all/2 contract.  Adapters 21, 27,
# and 28 are absent because stock rejects them; adapter 1 has its own direct
# row and is deliberately not expanded from all/2.
COMMON_READY_ADAPTERS = {
    "4", "6", "7", "8", "9", "10", "12", "13", "14", "17", "19",
    "20", "23", "24", "25", "26", "29", "30", "31", "32",
}

FIELDS = [
    "proxy", "adapter", "interface", "vtable", "address_point", "slot",
    "entry", "target", "target_name", "slot_kind", "contract_key",
    "api_name", "contract_source", "callability", "abi_words_after_self",
    "abi_prototype", "pointer_extent_ownership_consumer",
    "return_status_behavior", "implementation_seam", "emitted",
    "fail_closed_reason", "evidence",
]


def rows(path):
    with open(path, newline="") as stream:
        return list(csv.DictReader(stream))


def short_proxy(qualified):
    match = re.search(r"PanaNav::([^:]+)::vtable$", qualified)
    if not match or match.group(1) not in PROXY_IDENTITY:
        raise ValueError("unknown proxy: " + qualified)
    return match.group(1)


def words_in_prototype(prototype):
    match = re.search(r"\((.*)\)", prototype)
    if not match:
        raise ValueError("unparseable prototype: " + prototype)
    arguments = [part.strip() for part in match.group(1).split(",")]
    if not arguments or "self" not in arguments[0]:
        raise ValueError("prototype lacks self: " + prototype)
    return str(len(arguments) - 1)


def runtime_contracts(matrix_path, coverage_path):
    matrix = {(r["adapter"], r["slot"]): r for r in rows(matrix_path)}
    coverage = rows(coverage_path)
    if len(matrix) != 61 or len(coverage) != 61:
        raise ValueError("runtime callback baseline must contain 61 rows")
    result = {}
    for covered in coverage:
        key = (covered["adapter"], covered["slot"])
        if key not in matrix:
            raise ValueError("coverage row missing matrix metadata: %r" % (key,))
        source = matrix[key]
        result[key] = {
            "contract_key": "/".join(key),
            "api_name": source["name"],
            "contract_source": "runtime_matrix",
            "callability": "callable_typed_emitted",
            "abi_words_after_self": words_in_prototype(source["prototype"]),
            "abi_prototype": source["prototype"],
            "pointer_extent_ownership_consumer": source["pointer_contract"],
            "return_status_behavior": (
                "consumer uint32_t status returned directly; NULL proxy/vtable/slot "
                "returns 1; typed wrapper validation may return 1"
            ),
            "implementation_seam": covered["symbol"],
            "emitted": covered["emitted"],
            "fail_closed_reason": "",
            "evidence": "%s; target=%s; evidence=%s; %s" % (
                source["trace_scope"], source["target"],
                source["evidence_status"], source["notes"]),
        }
    return result


def static_contracts(additions_path, recovered_path):
    result = {}
    for source_name, path in (
        ("direct_static_dispatch", additions_path),
        ("owned_thunk_static_contract", recovered_path),
    ):
        for source in rows(path):
            key = (source["adapter"], source["slot"])
            if key in result:
                raise ValueError("duplicate static contract: %r" % (key,))
            unsafe = source["state"] == "static_pointer_fail_closed"
            symbol = source["implementation_symbol"]
            if unsafe and symbol:
                raise ValueError("unsafe static contract has a seam: %r" % (key,))
            if not unsafe and not symbol:
                raise ValueError("safe static contract lacks a seam: %r" % (key,))
            if source_name == "direct_static_dispatch":
                name = source["trigger_relation"].split("::")[-1]
                evidence = source["stock_callsite"] + "; " + source["trigger_relation"]
            else:
                name = source["ci_name"]
                evidence = source["producer_evidence"] + "; " + source["consumer_evidence"]
            result[key] = {
                "contract_key": "/".join(key),
                "api_name": name,
                "contract_source": source_name,
                "callability": (
                    "fail_closed_pointer_contract" if unsafe
                    else "callable_typed_dormant"
                ),
                "abi_words_after_self": source["abi_words_after_self"],
                "abi_prototype": source["abi_prototype"],
                "pointer_extent_ownership_consumer": source["pointer_contract"],
                "return_status_behavior": (
                    "not invoked; no status fabricated" if unsafe else
                    "consumer uint32_t status returned directly; NULL proxy/vtable/slot returns 1"
                ),
                "implementation_seam": symbol,
                "emitted": source["emitted"],
                "fail_closed_reason": source["pointer_contract"] if unsafe else "",
                "evidence": evidence,
            }
    return result


def cross_version_contracts(path):
    result = {}
    for source in rows(path):
        key = (source["adapter"], source["slot"])
        if key in result:
            raise ValueError("duplicate cross-version contract: %r" % (key,))
        if (source["qr396_vtable"] != source["qr40_vtable"] or
                source["qr396_target"] != source["qr40_target"] or
                source["emitted"] != "no" or
                source["implementation_symbol"] == ""):
            raise ValueError("unqualified cross-version contract: %r" % (key,))
        result[key] = {
            "contract_key": "/".join(key),
            "api_name": source["api_name"],
            "contract_source": "cross_version_concrete_consumer",
            "callability": "callable_typed_dormant",
            "abi_words_after_self": source["abi_words_after_self"],
            "abi_prototype": source["abi_prototype"],
            "pointer_extent_ownership_consumer": source["pointer_contract"],
            "return_status_behavior": (
                "consumer uint32_t status returned directly; NULL proxy/vtable/slot returns 1"
            ),
            "implementation_seam": source["implementation_symbol"],
            "emitted": "no",
            "fail_closed_reason": "",
            "evidence": (
                "QR39.6/QR40 concrete SI_AudioHandling vtable=%s target=%s; %s" %
                (source["qr396_vtable"], source["qr396_target"],
                 source["machine_evidence"])),
        }
    return result


CONCRETE_POINTER_SIGNATURES = {
    ("24", "3"): ("uint32_t handle, uint32_t source, uint32_t text_type, "
                     "const char *tts_text",
                     "borrow NUL-terminated CSTR8 synchronously; consumer accepts 1..1027 bytes and copies into 0x404-byte payload"),
    ("32", "4"): ("const void *old_area_10, const void *new_area_10",
                     "borrow two exact 10-byte areas synchronously; consumer compares 10 bytes and sends the second"),
    ("20", "7"): ("uint32_t handle, uint32_t service, const wchar_t *url, "
                     "uint32_t method, uint32_t request_size, const void *request_data, "
                     "uint32_t response_size, const void *response_data",
                     "borrow NUL-terminated CSTR16 URL and exact request_size/response_size byte buffers synchronously; consumer copies all inputs"),
    ("root", "6"): ("const wchar_t *path",
                       "borrow NUL-terminated CSTR16 path synchronously; consumer converts and consumes it before return"),
    ("21", "4"): ("uint32_t handle, const wchar_t *url, uint32_t method, "
                     "uint32_t request_size, const void *request_data, "
                     "uint32_t response_size, const void *response_data",
                     "borrow NUL-terminated CSTR16 URL and exact request_size/response_size byte buffers synchronously; consumer copies all inputs"),
    ("19", "8"): ("uint32_t handle, uint32_t object_id, uint32_t object_type, "
                     "const wchar_t *description, uint64_t size, uint32_t reply_type",
                     "borrow NUL-terminated CSTR16 synchronously; uint64_t is naturally AAPCS-aligned on stack"),
    ("19", "9"): ("uint32_t handle, uint32_t object_id, uint64_t size, "
                     "const void *data, uint64_t total_size, uint32_t abort",
                     "borrow exactly size bytes synchronously; size must fit the concrete 0xfc0-byte consumer buffer; aligned UINT64 arguments preserve AAPCS padding"),
    ("13", "10"): ("int32_t handle, const wchar_t *versions",
                      "borrow NUL-terminated CSTR16 synchronously; consumer converts at most 0x4d9 bytes into a 0x4e0-byte payload"),
    ("32", "3"): ("uint32_t handle, const void *area_10",
                    "copy exactly 10 bytes before synchronous call; no retention"),
    ("14", "7"): ("uint32_t count, const void *filters_6x20",
                     "copy min(count,20)*6 bytes before synchronous call; no retention"),
    ("27", "5"): ("const void *station_0x30",
                     "copy exactly 0x30 bytes before synchronous call; no retention"),
    ("27", "6"): ("uint32_t count, const void *stations_0x30x4",
                     "for count<=4 copy exactly count*0x30 bytes before synchronous call; no retention"),
    ("9", "5"): ("const void *position_0x38",
                    "copy exactly 0x38 bytes before synchronous call; no retention"),
    ("root", "11"): ("uint32_t word0, uint32_t word1, const void *opaque",
                        "opaque pointer is not dereferenced, copied, or retained; forward synchronously only"),
    ("28", "5"): ("uint32_t word0, uint32_t word1, uint32_t word2, "
                      "uint32_t minute, const void *warning_0x180",
                      "copy exactly 0x180 bytes before synchronous call; no retention"),
    ("8", "10"): ("uint32_t word0, uint32_t word1, const void *payload_8",
                     "copy exactly 8 bytes before synchronous call; no retention"),
    ("7", "5"): ("uint32_t handle, const void *timezone_0x22c",
                    "copy exactly 0x22c bytes before synchronous call; no retention"),
}

CONCRETE_AGGREGATE_SIGNATURES = {
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


def concrete_symbol(source):
    api = re.sub(r"[^A-Za-z0-9_]", "_", source["api_name"])
    return "mock_pas_%s_%s_%s" % (source["adapter"], source["slot"], api)


def concrete_contracts(path):
    result = {}
    source_rows = rows(path)
    if len(source_rows) != 63:
        raise ValueError("concrete vtable audit must contain 63 rows")
    for source in source_rows:
        key = (source["adapter"], source["slot"])
        if key in result or source["target"] != source["qr40_target"] or \
                source["body_match"] != "identical":
            raise ValueError("invalid concrete vtable contract: %r" % (key,))
        contract_class = source["contract_class"]
        if contract_class == "typed_dormant":
            if key in CONCRETE_AGGREGATE_SIGNATURES:
                arguments, pointer_contract = CONCRETE_AGGREGATE_SIGNATURES[key]
            elif key in CONCRETE_POINTER_SIGNATURES:
                arguments, pointer_contract = CONCRETE_POINTER_SIGNATURES[key]
            else:
                count = int(source["abi_words_after_self"])
                arguments = ", ".join("uint32_t word%d" % word
                                      for word in range(count))
                pointer_contract = "fixed scalar words only; no pointer ownership"
            comma = ", " if arguments else ""
            prototype = "uint32_t (*)(void *self%s%s)" % (comma, arguments)
            contract = {
                "callability": "callable_typed_dormant",
                "abi_prototype": prototype,
                "pointer_extent_ownership_consumer": pointer_contract,
                "return_status_behavior": (
                    "consumer uint32_t status returned directly; NULL proxy/vtable/slot "
                    "or failed bounded-pointer validation returns 1"
                ),
                "implementation_seam": concrete_symbol(source),
                "fail_closed_reason": "",
            }
        elif contract_class in ("pointer_fail_closed", "aggregate_fail_closed"):
            pointer = contract_class == "pointer_fail_closed"
            contract = {
                "callability": ("fail_closed_pointer_contract" if pointer else
                                "fail_closed_aggregate_contract"),
                "abi_prototype": "non-callable; unsafe contract intentionally has no C prototype",
                "pointer_extent_ownership_consumer": (
                    "do not call; %s; %s" %
                    (source["ownership"], source["consumer_access"])
                ),
                "return_status_behavior": "not invoked; no status fabricated",
                "implementation_seam": "",
                "fail_closed_reason": source["consumer_access"],
            }
        else:
            raise ValueError("unknown concrete contract class: " + contract_class)
        result[key] = {
            "contract_key": "/".join(key),
            "api_name": source["api_name"],
            "contract_source": "cross_version_concrete_vtable_audit",
            "abi_words_after_self": source["abi_words_after_self"],
            "emitted": "no",
            "evidence": (
                "QR39.6/QR40 identical concrete %s vtable=%s target=%s; %s; %s" %
                (source["interface"], source["concrete_vtable"], source["target"],
                 source["consumer_access"], source["evidence"])
            ),
            **contract,
        }
    return result


def unknown_contract():
    return {
        "contract_key": "",
        "api_name": "unrecovered_application_method",
        "contract_source": "qr39.6_base_proxy_vtable_only",
        "callability": "fail_closed_unrecovered_abi",
        "abi_words_after_self": "unknown",
        "abi_prototype": "unknown",
        "pointer_extent_ownership_consumer": "unknown",
        "return_status_behavior": "not invoked; no status fabricated",
        "implementation_seam": "",
        "emitted": "no",
        "fail_closed_reason": (
            "pure-virtual base slot proves presence only; arity and pointer/output, "
            "hidden-return, 64-bit, and by-value aggregate hazards are unproved"
        ),
        "evidence": "QR39.6 abstract proxy vtable entry only",
    }


def destructor_contract(slot):
    return {
        "contract_key": "",
        "api_name": "complete_destructor" if slot == "0" else "deleting_destructor",
        "contract_source": "qr39.6_base_proxy_vtable",
        "callability": "non_application_destructor",
        "abi_words_after_self": "abi_managed",
        "abi_prototype": "C++ ABI destructor; not an outbound application callback",
        "pointer_extent_ownership_consumer": "object lifetime is PAS-owned",
        "return_status_behavior": "not invoked by mock provider",
        "implementation_seam": "",
        "emitted": "no",
        "fail_closed_reason": "non-application lifetime slot",
        "evidence": "Itanium ABI address point slots 0 and 1",
    }


def build(vtable_path, matrix_path, coverage_path, additions_path,
          recovered_path, cross_version_path, concrete_path, output_path):
    runtime = runtime_contracts(matrix_path, coverage_path)
    static = static_contracts(additions_path, recovered_path)
    cross_version = cross_version_contracts(cross_version_path)
    concrete = concrete_contracts(concrete_path)
    catalogs = (set(runtime), set(static), set(cross_version), set(concrete))
    if any(catalogs[i] & catalogs[j] for i in range(4) for j in range(i + 1, 4)):
        raise ValueError("recovered contract catalogs overlap")
    if len(runtime) + len(static) + len(cross_version) + len(concrete) != 180:
        raise ValueError("expected 180 disjoint recovered contract keys")
    output = []
    seen = set()
    for raw in rows(vtable_path):
        proxy = short_proxy(raw["proxy"])
        adapter, interface = PROXY_IDENTITY[proxy]
        slot = raw["slot"]
        key = (adapter, slot)
        identity = (proxy, slot)
        if identity in seen:
            raise ValueError("duplicate proxy slot: %r" % (identity,))
        seen.add(identity)
        if slot in ("0", "1"):
            contract = destructor_contract(slot)
            slot_kind = "destructor"
        else:
            contract = (runtime.get(key) or static.get(key) or
                        cross_version.get(key) or concrete.get(key))
            if contract is None and slot == "2" and adapter in COMMON_READY_ADAPTERS:
                contract = dict(runtime[("all", "2")])
            if contract is None:
                contract = unknown_contract()
            slot_kind = "pure_virtual_application"
        output.append({
            "proxy": proxy,
            "adapter": adapter,
            "interface": interface,
            "vtable": raw["vtable"],
            "address_point": raw["address_point"],
            "slot": slot,
            "entry": raw["entry"],
            "target": raw["target"],
            "target_name": raw["function"],
            "slot_kind": slot_kind,
            **contract,
        })
    if len(output) != 249 or len({r["proxy"] for r in output}) != 25:
        raise ValueError("QR39.6 proxy inventory must be 25 vtables / 249 slots")
    with open(output_path, "w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(output)


def main():
    if len(sys.argv) != 9:
        raise SystemExit(
            "usage: build_pas_proxy_slot_manifest.py vtables.csv matrix.csv "
            "coverage.csv additions.csv contracts.csv cross-version.csv "
            "concrete-audit.csv output.csv")
    build(*sys.argv[1:])


if __name__ == "__main__":
    main()
