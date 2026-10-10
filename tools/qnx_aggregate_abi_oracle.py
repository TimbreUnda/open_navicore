#!/usr/bin/env python3
"""Compile the four recovered PAS aggregate callback declarations for QNX ARM."""

import argparse
import csv
import hashlib
import os
import pathlib
import subprocess
import tempfile

from qnx_arm_abi_oracle import FLAGS, parse_objdump


DEFAULT_SDK = "/mnt/d/tools/qnx660-sdk"
DEFAULT_HOST_LIBS = "/mnt/d/tools/qnx-host-libs"
FIELDS = [
    "adapter", "slot", "symbol", "signature", "words_after_self",
    "location_size", "mbr_size", "mbr_alignment", "callee_sha256",
    "caller_sha256", "compiler_flags",
]


FIXTURE = r'''
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned long long u64;
#define A __attribute__((noinline,used))

typedef struct { double longitude; double latitude; } Location;
typedef struct {
    Location lower_left;
    Location upper_right;
    Location center;
} Mbr;

typedef char location_size_must_be_16[(sizeof(Location) == 16) ? 1 : -1];
typedef char location_align_must_be_8[(__alignof__(Location) == 8) ? 1 : -1];
typedef char mbr_size_must_be_48[(sizeof(Mbr) == 48) ? 1 : -1];
typedef char mbr_align_must_be_8[(__alignof__(Mbr) == 8) ? 1 : -1];

static u32 dword(double value) { union { double d; u64 u; } x; x.d=value; return (u32)x.u; }

A u32 callee_wsa(void *self, u32 handle, Location location, u32 radius, u32 unit)
{ return (u32)(unsigned long)self ^ handle ^ dword(location.longitude) ^
         dword(location.latitude) ^ radius ^ unit; }
A u32 caller_wsa(void *self, const u32 *p)
{ Location x; __builtin_memcpy(&x,p+1,16); return callee_wsa(self,p[0],x,p[5],p[6]); }

A u32 callee_apogee_collection(void *self, u32 handle, u16 type, Mbr mbr, u32 index)
{ return (u32)(unsigned long)self ^ handle ^ type ^ dword(mbr.lower_left.longitude) ^
         dword(mbr.center.latitude) ^ index; }
A u32 caller_apogee_collection(void *self, const u32 *p)
{ Mbr x; __builtin_memcpy(&x,p+1,48); return callee_apogee_collection(self,p[0],(u16)p[13],x,p[14]); }

A u32 callee_apogee_extraction(void *self, u32 handle, u16 type, Mbr mbr, u32 index)
{ return (u32)(unsigned long)self ^ handle ^ type ^ dword(mbr.lower_left.longitude) ^
         dword(mbr.center.latitude) ^ index; }
A u32 caller_apogee_extraction(void *self, const u32 *p)
{ Mbr x; __builtin_memcpy(&x,p+1,48); return callee_apogee_extraction(self,p[0],(u16)p[13],x,p[14]); }

A u32 callee_parking(void *self, u32 handle, Mbr mbr, u16 index)
{ return (u32)(unsigned long)self ^ handle ^ dword(mbr.lower_left.longitude) ^
         dword(mbr.center.latitude) ^ index; }
A u32 caller_parking(void *self, const u32 *p)
{ Mbr x; __builtin_memcpy(&x,p+1,48); return callee_parking(self,p[0],x,(u16)p[13]); }
'''


ROWS = [
    ("31", "12", "wsa",
     "uint32_t(void*,uint32_t,MockPasSxmLocation,uint32_t,uint32_t)", "7"),
    ("30", "4", "apogee_collection",
     "uint32_t(void*,uint32_t,uint16_t,MockPasSxmMbr,uint32_t)", "16"),
    ("30", "5", "apogee_extraction",
     "uint32_t(void*,uint32_t,uint16_t,MockPasSxmMbr,uint32_t)", "16"),
    ("29", "4", "parking",
     "uint32_t(void*,uint32_t,MockPasSxmMbr,uint16_t)", "14"),
]


def digest(ops):
    return hashlib.sha256("\n".join(ops).encode()).hexdigest()


def build(sdk, host_libs):
    host = pathlib.Path(sdk) / "host/linux/x86"
    env = os.environ.copy()
    env["QNX_HOST"] = str(host)
    env["QNX_TARGET"] = str(pathlib.Path(sdk) / "target/qnx6")
    env["LD_LIBRARY_PATH"] = host_libs
    with tempfile.TemporaryDirectory(prefix="qnx-aggregate-abi-") as temp:
        source = pathlib.Path(temp) / "aggregate.c"
        obj = pathlib.Path(temp) / "aggregate.o"
        source.write_text(FIXTURE)
        subprocess.run([str(host / "usr/bin/qcc"), *FLAGS, "-std=c99", "-c",
                        str(source), "-o", str(obj)], env=env, check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        result = subprocess.run([str(host / "usr/bin/ntoarmv7-objdump"), "-dr",
                                 str(obj)], env=env, check=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True)
    functions = parse_objdump(result.stdout)
    output = []
    for adapter, slot, symbol, signature, words in ROWS:
        callee = functions["callee_" + symbol]
        caller = functions["caller_" + symbol]
        output.append({
            "adapter": adapter, "slot": slot, "symbol": symbol,
            "signature": signature, "words_after_self": words,
            "location_size": "16", "mbr_size": "48", "mbr_alignment": "8",
            "callee_sha256": digest(callee), "caller_sha256": digest(caller),
            "compiler_flags": " ".join(FLAGS + ["-std=c99"]),
        })
    return output


def write(rows, path):
    with pathlib.Path(path).open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", default=DEFAULT_SDK)
    parser.add_argument("--host-libs", default=DEFAULT_HOST_LIBS)
    parser.add_argument("--output", default="evidence/qr39/oracles/aggregate-callback-abi.csv")
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    actual = build(args.sdk, args.host_libs)
    path = pathlib.Path(args.output)
    if args.verify:
        with path.open(newline="") as stream:
            expected = list(csv.DictReader(stream))
        if actual != expected:
            raise SystemExit("aggregate callback ABI oracle is stale")
        print("QNX aggregate callback ABI oracle passed: 4/4 declarations")
    else:
        write(actual, path)


if __name__ == "__main__":
    main()
