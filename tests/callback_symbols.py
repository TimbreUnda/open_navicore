#!/usr/bin/env python3
"""Canonical names for handwritten stock-host callback adapters."""

import re


ADAPTER_DOMAINS = {
    "root": "root",
    "1": "hmi",
    "4": "hardkey",
    "6": "logging",
    "7": "i18n",
    "8": "vehicle",
    "9": "gps",
    "10": "audio",
    "12": "diagnosis",
    "13": "nav_diagnosis",
    "14": "rds",
    "17": "cluster",
    "19": "data_exchange",
    "20": "connectivity",
    "21": "live_traffic",
    "23": "rotary",
    "24": "tts",
    "25": "voice",
    "26": "phone",
    "27": "tpeg",
    "28": "vics",
    "29": "parking",
    "30": "apogee",
    "31": "agw",
    "32": "ews",
}

SYMBOL_OVERRIDES = {
    # Two distinct Voice Recognition callbacks share the vendor API label but
    # have different contracts. Slot 31 completes a request with the compact
    # two-word reply, while slot 23 carries the live three-word status reply.
    ("25", "31"): "nc_host_voice_reply_route_guidance_status_completion",
}


def snake_case(name):
    """Convert PAS API spelling, including acronym runs, to lower snake case."""
    first = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", first).lower()


def callback_symbol(adapter, slot, api_name):
    """Return the descriptive C seam for a typed callback contract."""
    override = SYMBOL_OVERRIDES.get((adapter, slot))
    if override:
        return override
    if adapter == "all":
        if api_name != "adapterReady":
            raise ValueError("only adapterReady has an all-adapter callback seam")
        return "nc_host_adapter_ready"
    try:
        domain = ADAPTER_DOMAINS[adapter]
    except KeyError as error:
        raise ValueError("unknown callback adapter: " + adapter) from error
    return "nc_host_%s_%s" % (domain, snake_case(api_name))
