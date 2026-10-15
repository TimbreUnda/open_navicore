#!/usr/bin/env python3
"""Guard statically recovered callback gaps without claiming full closure."""

import csv
import io
import pathlib
import sys

EXPECTED = {
    ("root", "5"): ("SI_SoftwareInfrastructure", "1", "0x00263e6a",
                       "nc_host_root_signal_initiate_restart_once"),
    ("1", "4"): ("SI_HMIIntegration", "1", "0x00262eec",
                    "nc_host_hmi_request_create_request_code"),
    ("1", "5"): ("SI_HMIIntegration", "1", "0x00262f94",
                    "nc_host_hmi_request_create_finger_print"),
    ("9", "6"): ("SI_GPSData", "2", "0x002640ce", "nc_host_gps_reply_map_matched_address"),
    ("20", "6"): ("SI_Connectivity", "1", "0x0026cc50",
                     "nc_host_connectivity_release_connection_for_service"),
    ("24", "4"): ("SI_TTS", "1", "0x002b13d2",
                     "nc_host_tts_internal_deinitialize"),
    ("26", "4"): ("SI_Phone", "3", "0x0026f3ce", "nc_host_phone_make_phone_call"),
}

FIELDS = [
    "adapter", "slot", "si_name", "state", "abi_words_after_self",
    "abi_prototype", "pointer_contract", "stock_callsite",
    "trigger_relation", "implementation_symbol", "emitted",
]


def parse(text):
    reader = csv.DictReader(io.StringIO(text))
    if reader.fieldnames != FIELDS:
        raise ValueError("unexpected static callback header")
    rows = list(reader)
    keyed = {(r["adapter"], r["slot"]): r for r in rows}
    if len(keyed) != len(rows):
        raise ValueError("duplicate static callback key")
    if set(keyed) != set(EXPECTED):
        raise ValueError("static callback catalog changed")
    for key, (name, words, address, symbol) in EXPECTED.items():
        row = keyed[key]
        if row["si_name"] != name or row["abi_words_after_self"] != words:
            raise ValueError("identity/arity mismatch for %s/%s" % key)
        if address not in row["stock_callsite"]:
            raise ValueError("provenance mismatch for %s/%s" % key)
        if row["state"] not in (
            "static_typed_not_emitted", "static_pointer_fail_closed"
        ):
            raise ValueError("invalid fail-closed state for %s/%s" % key)
        if row["implementation_symbol"] != symbol or row["emitted"] != "no":
            raise ValueError("static callback seam/emission mismatch")
        pointer = row["pointer_contract"] != "none"
        if row["state"] == "static_pointer_fail_closed":
            if not pointer or "do not call" not in row["pointer_contract"]:
                raise ValueError("unsafe pointer row for %s/%s" % key)
        elif not symbol:
            raise ValueError("typed row lacks implementation for %s/%s" % key)
        elif pointer and "borrowed synchronous" not in row["pointer_contract"]:
            raise ValueError("typed pointer row lacks ownership for %s/%s" % key)
        elif not pointer and row["pointer_contract"] != "none":
            raise ValueError("unexpected scalar pointer contract for %s/%s" % key)
    return keyed


def check(runtime_path, additions_path):
    runtime = list(csv.DictReader(pathlib.Path(runtime_path).open(newline="")))
    runtime_keys = {(r["adapter"], r["slot"]) for r in runtime}
    if len(runtime) != 61 or len(runtime_keys) != 61:
        raise ValueError("runtime callback baseline must remain 61 unique rows")
    additions_text = pathlib.Path(additions_path).read_text()
    added = parse(additions_text)
    overlap = runtime_keys & set(added)
    if overlap:
        raise ValueError("static additions overlap runtime baseline: %r" % overlap)
    return additions_text


def negative_tests(text):
    mutations = [
        text.replace("root,5,", "root,15,", 1),
        text.replace("0x00263e6a", "0x00263e6c", 1),
        text.replace("9,6,SI_GPSData,static_typed_not_emitted",
                     "9,6,SI_GPSData,static_pointer_fail_closed", 1),
        text.replace(",nc_host_root_signal_initiate_restart_once,no\n",
                     ",open_navi_pas_forbidden,yes\n", 1),
        text.replace("borrowed synchronous 0x50-byte ARM record", "none", 1),
    ]
    for mutation in mutations:
        try:
            parse(mutation)
        except ValueError:
            continue
        raise ValueError("negative mutation unexpectedly passed")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: check_static_callback_additions.py runtime.csv additions.csv")
    text = check(sys.argv[1], sys.argv[2])
    negative_tests(text)
    print("Static callback addition guard passed: 61 runtime + 7 prior static rows")
    print("This checker covers the seven direct additions only")


if __name__ == "__main__":
    main()
