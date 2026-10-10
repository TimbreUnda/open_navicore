#!/usr/bin/env python3
"""Build and match a QNX 6.6 ARM ABI compiler oracle.

This is deliberately an ABI oracle, not a C/C++ source reconstructor.  It
compiles bounded declaration/call-wrapper permutations with the target qcc,
normalizes the resulting ARM call setup, and compares those fingerprints with
independently supplied producer and consumer snippets.  A slot is never
promoted unless both sides exist and agree and the winning class excludes the
hazards listed in UNSAFE_CLASSES.
"""

import argparse
import csv
import difflib
import hashlib
import os
import pathlib
import re
import subprocess
import tempfile


DEFAULT_SDK = "/mnt/d/tools/qnx660-sdk"
DEFAULT_HOST_LIBS = "/mnt/d/tools/qnx-host-libs"
FLAGS = [
    "-Vgcc_ntoarmv7le", "-O2", "-fno-inline", "-fno-ipa-sra",
    "-fno-optimize-sibling-calls", "-ffunction-sections",
]
UNSAFE_CLASSES = {
    "u64_aligned", "float_soft", "double_soft", "pod_by_value",
    "hidden_sret", "pointer_or_reference", "function_pointer",
    "member_pointer", "cpp_reference", "cpp_virtual",
}
CATALOG_FIELDS = [
    "symbol", "language", "role", "family", "abi_class", "words_after_self",
    "hazards", "compiler_flags", "normalized_sha256", "normalized_ops",
]
ASSESS_FIELDS = [
    "adapter", "slot", "interface", "producer_evidence", "consumer_evidence",
    "best_family", "best_score", "second_score", "two_sided_agreement",
    "hazards_excluded", "decision", "reason",
]


def word_args(n):
    return ", ".join(["void *self"] + ["u32 w%d" % i for i in range(n)])


def word_values(n):
    return ", ".join(["self"] + ["p[%d]" % i for i in range(n)])


def c_fixture():
    out = [
        "typedef unsigned int u32; typedef unsigned long long u64;",
        "typedef u32 (*Fn)(u32);",
        "typedef struct { u32 v[1]; } P1; typedef struct { u32 v[2]; } P2;",
        "typedef struct { u32 v[3]; } P3; typedef struct { u32 v[4]; } P4;",
        "typedef struct { u32 v[5]; } P5;",
        "#define A __attribute__((noinline,used))",
    ]
    for n in range(11):
        out += [
            "A u32 callee_words_%d(%s) { return (u32)(unsigned long)self%s; }" % (
                n, word_args(n), "".join(" ^ w%d" % i for i in range(n))),
            "A u32 caller_words_%d(void *self,const u32 *p) { return callee_words_%d(%s); }" % (
                n, n, word_values(n)),
        ]
    out += [
        "A u32 callee_u64_a(void *s,u32 a,u64 b,u32 c){return (u32)(unsigned long)s^a^(u32)b^(u32)(b>>32)^c;}",
        "A u32 caller_u64_a(void*s,const u32*p){u64 x=((u64)p[2]<<32)|p[1];return callee_u64_a(s,p[0],x,p[3]);}",
        "A u32 callee_u64_b(void *s,u64 a,u32 b){return (u32)(unsigned long)s^(u32)a^(u32)(a>>32)^b;}",
        "A u32 caller_u64_b(void*s,const u32*p){u64 x=((u64)p[1]<<32)|p[0];return callee_u64_b(s,x,p[2]);}",
        "A u32 callee_float(void*s,float a,float b){union{float f;u32 u;}x={a},y={b};return (u32)(unsigned long)s^x.u^y.u;}",
        "A u32 caller_float(void*s,const u32*p){union{float f;u32 u;}a,b;a.u=p[0];b.u=p[1];return callee_float(s,a.f,b.f);}",
        "A u32 callee_double(void*s,double a){union{double d;u64 u;}x={a};return (u32)(unsigned long)s^(u32)x.u^(u32)(x.u>>32);}",
        "A u32 caller_double(void*s,const u32*p){union{double d;u64 u;}a;a.u=((u64)p[1]<<32)|p[0];return callee_double(s,a.d);}",
        "A u32 callee_ptr(void*s,const u32*p,u32*q){return (u32)(unsigned long)s^*p^*q;}",
        "A u32 caller_ptr(void*s,const u32*p){return callee_ptr(s,p,(u32*)(p+1));}",
        "A u32 callee_fn(void*s,Fn f,u32 a){return (u32)(unsigned long)s^f(a);}",
        "A u32 caller_fn(void*s,const u32*p){return callee_fn(s,(Fn)(unsigned long)p[0],p[1]);}",
    ]
    for n in range(1, 6):
        out += [
            "A u32 callee_p%d(void*s,P%d a,u32 z){return (u32)(unsigned long)s^a.v[0]^z;}" % (n, n),
            "A u32 caller_p%d(void*s,const u32*p){P%d a;u32 i;for(i=0;i<%d;i++)a.v[i]=p[i];return callee_p%d(s,a,p[%d]);}" % (n, n, n, n, n),
            "A P%d callee_sret%d(void*s,u32 a){P%d x={{0}};x.v[0]=(u32)(unsigned long)s^a;return x;}" % (n, n, n),
            "A P%d caller_sret%d(void*s,const u32*p){return callee_sret%d(s,p[0]);}" % (n, n, n),
        ]
    return "\n".join(out) + "\n"


def cpp_fixture():
    return r'''typedef unsigned int u32;
#define A __attribute__((noinline,used))
struct C { u32 x; virtual u32 vf(u32 a); u32 mf(u32 a); };
u32 C::vf(u32 a){return x^a;} u32 C::mf(u32 a){return x+a;}
typedef u32 (C::*Mem)(u32);
extern "C" {
A u32 callee_ref(void *s,const u32 &a,u32 &b){return (u32)(unsigned long)s^a^b;}
A u32 caller_ref(void*s,const u32*p){u32 b=p[1];return callee_ref(s,p[0],b);}
A u32 callee_mem(void*s,C*c,Mem m,u32 a){return (u32)(unsigned long)s^(c->*m)(a);}
A u32 caller_mem(void*s,const u32*p){Mem m=&C::mf;return callee_mem(s,(C*)(unsigned long)p[0],m,p[1]);}
A u32 callee_virtual(void*s,C*c,u32 a){return (u32)(unsigned long)s^c->vf(a);}
A u32 caller_virtual(void*s,const u32*p){return callee_virtual(s,(C*)(unsigned long)p[0],p[1]);}
}
'''


def run(cmd, env):
    return subprocess.run(cmd, env=env, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def normalize_operand(text):
    text = re.sub(r"\b0x[0-9a-fA-F]+\b", "IMM", text)
    text = re.sub(r"(?<![A-Za-z_])#[+-]?[0-9]+", "#IMM", text)
    text = re.sub(r"\b[0-9a-fA-F]{6,}\b", "ADDR", text)
    text = re.sub(r"<[^>]+>", "<TARGET>", text)
    return re.sub(r"\s+", " ", text.strip())


def parse_objdump(text):
    funcs = {}
    current = None
    for line in text.splitlines():
        m = re.match(r"^[0-9a-f]+ <([^>]+)>:$", line.strip())
        if m:
            current = m.group(1)
            funcs[current] = []
            continue
        if current is None:
            continue
        # Encoding groups are 4 or 8 hex digits in ntoarmv7-objdump.  A
        # looser expression accidentally consumed mnemonics such as `add`
        # because those letters are themselves hexadecimal digits.
        ins = re.match(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{4,8}\s+)+\s*([a-z][a-z0-9.]*)\s*(.*)$", line)
        if ins:
            funcs[current].append(ins.group(1) + (" " + normalize_operand(ins.group(2)) if ins.group(2).strip() else ""))
            continue
        rel = re.match(r"^\s*[0-9a-f]+:\s+R_ARM_([A-Z0-9_]+)\s+(.+)$", line)
        if rel:
            funcs[current].append("RELOC_" + rel.group(1) + " <TARGET>")
    return funcs


def call_window(ops, call_index=0, width=20):
    """Return bounded call setup ending at the selected BL/BLX.

    The relocation immediately following a direct call is retained.  This
    makes fixture wrappers comparable with an isolated stock callsite rather
    than requiring the surrounding function bodies to be identical.
    """
    calls = [i for i, op in enumerate(ops)
             if re.match(r"^(bl|blx)(\.|\s|$)", op)]
    if call_index < 0 or call_index >= len(calls):
        raise ValueError("requested call index is absent")
    at = calls[call_index]
    end = at + 1
    if end < len(ops) and ops[end].startswith("RELOC_"):
        end += 1
    return ops[max(0, at - width):end]


def metadata(symbol):
    role = "caller" if symbol.startswith("caller_") else "callee"
    base = re.sub(r"^(caller|callee)_", "", symbol)
    if base.startswith("words_"):
        return role, base, "word_scalar", base.split("_")[1], "none"
    if base.startswith("u64_"):
        return role, base, "u64_aligned", "variable", "64-bit/alignment"
    if base == "float":
        return role, base, "float_soft", "2", "soft-float/register equivalence"
    if base == "double":
        return role, base, "double_soft", "2", "soft-float;64-bit/alignment"
    if base == "ptr":
        return role, base, "pointer_or_reference", "2", "pointer extent/ownership"
    if base == "fn":
        return role, base, "function_pointer", "2", "callable pointer"
    if base == "ref":
        return role, base, "cpp_reference", "2", "reference extent/ownership"
    if base == "mem":
        return role, base, "member_pointer", "variable", "member-pointer representation"
    if base == "virtual":
        return role, base, "cpp_virtual", "2", "pointer/vtable ownership"
    if base.startswith("p"):
        return role, base, "pod_by_value", "variable", "by-value aggregate"
    if base.startswith("sret"):
        return role, base, "hidden_sret", "variable", "hidden return pointer"
    raise ValueError("unclassified fixture symbol: " + symbol)


def build_catalog(sdk, host_libs, work):
    host = pathlib.Path(sdk) / "host/linux/x86"
    target = pathlib.Path(sdk) / "target/qnx6"
    qcc = host / "usr/bin/qcc"
    objdump = host / "usr/bin/ntoarmv7-objdump"
    env = dict(os.environ, QNX_HOST=str(host), QNX_TARGET=str(target),
               LD_LIBRARY_PATH=host_libs)
    paths = []
    for lang, source in (("c", c_fixture()), ("cpp", cpp_fixture())):
        src = pathlib.Path(work) / ("oracle." + ("c" if lang == "c" else "cc"))
        obj = pathlib.Path(work) / ("oracle_" + lang + ".o")
        src.write_text(source)
        command = [str(qcc)] + FLAGS
        if lang == "c":
            command += ["-std=c99"]
        else:
            command += ["-lang-c++"]
        command += ["-c", str(src), "-o", str(obj)]
        run(command, env)
        dump = run([str(objdump), "-dr", str(obj)], env)
        paths.append((lang, parse_objdump(dump)))
    rows = []
    flag_text = " ".join(FLAGS)
    for language, funcs in paths:
        for symbol in sorted(funcs):
            if not symbol.startswith(("caller_", "callee_")):
                continue
            role, family, abi_class, words, hazards = metadata(symbol)
            selected = call_window(funcs[symbol]) if role == "caller" else funcs[symbol]
            ops = ";".join(selected)
            rows.append({
                "symbol": symbol, "language": language, "role": role,
                "family": family, "abi_class": abi_class,
                "words_after_self": words, "hazards": hazards,
                "compiler_flags": flag_text,
                "normalized_sha256": hashlib.sha256(ops.encode()).hexdigest(),
                "normalized_ops": ops,
            })
    return rows


def score(a, b):
    aa, bb = a.split(";"), b.split(";")
    return round(100.0 * difflib.SequenceMatcher(None, aa, bb, autojunk=False).ratio(), 3)


def assess(unresolved, catalog, evidence):
    callers = [r for r in catalog if r["role"] == "caller"]
    by_key = {(r["adapter"], r["slot"]): r for r in evidence}
    output = []
    for row in unresolved:
        key = (row["adapter"], row["slot"])
        ev = by_key.get(key, {})
        producer = ev.get("producer_ops", "").strip()
        consumer = ev.get("consumer_ops", "").strip()
        ranked_p = sorted(((score(producer, c["normalized_ops"]), c) for c in callers), reverse=True, key=lambda x:x[0]) if producer else []
        ranked_c = sorted(((score(consumer, c["normalized_ops"]), c) for c in callers), reverse=True, key=lambda x:x[0]) if consumer else []
        agree = bool(ranked_p and ranked_c and ranked_p[0][1]["family"] == ranked_c[0][1]["family"])
        winner = ranked_p[0][1] if agree else None
        excluded = bool(winner and winner["abi_class"] not in UNSAFE_CLASSES)
        combined = min(ranked_p[0][0], ranked_c[0][0]) if agree else 0.0
        second = max(ranked_p[1][0] if len(ranked_p)>1 else 0.0,
                     ranked_c[1][0] if len(ranked_c)>1 else 0.0)
        decision = "abi_equivalent_candidate" if agree and excluded and combined == 100.0 and combined > second else "remain_fail_closed"
        if not producer or not consumer:
            reason = "missing independent producer and/or concrete PAS consumer call-setup evidence"
        elif not agree:
            reason = "producer and consumer best oracle families disagree"
        elif not excluded:
            reason = "winning ABI family retains hidden-return/64-bit/by-value/pointer hazard"
        elif combined != 100.0 or combined <= second:
            reason = "bounded permutation has no unique exact two-sided match"
        else:
            reason = "unique exact two-sided ABI-equivalent match; source-level type/semantics not proven"
        output.append({
            "adapter": row["adapter"], "slot": row["slot"], "interface": row["interface"],
            "producer_evidence": ev.get("producer_evidence", "none"),
            "consumer_evidence": ev.get("consumer_evidence", "none"),
            "best_family": winner["family"] if winner else "",
            "best_score": ("%.3f" % combined), "second_score": ("%.3f" % second),
            "two_sided_agreement": "yes" if agree else "no",
            "hazards_excluded": "yes" if excluded else "no",
            "decision": decision, "reason": reason,
        })
    return output


def matcher_self_test(catalog):
    """Exercise positive, hazardous, and disagreeing bounded permutations."""
    callers = {r["family"]: r for r in catalog if r["role"] == "caller"}
    unresolved = [
        {"adapter": "x", "slot": "1", "interface": "safe"},
        {"adapter": "x", "slot": "2", "interface": "hazard"},
        {"adapter": "x", "slot": "3", "interface": "disagree"},
    ]
    def evidence(slot, producer, consumer):
        return {
            "adapter": "x", "slot": slot,
            "producer_evidence": "synthetic compiler fixture",
            "consumer_evidence": "independent synthetic compiler fixture",
            "producer_ops": callers[producer]["normalized_ops"],
            "consumer_ops": callers[consumer]["normalized_ops"],
        }
    result = assess(unresolved, catalog, [
        evidence("1", "words_4", "words_4"),
        evidence("2", "u64_a", "u64_a"),
        evidence("3", "words_4", "words_5"),
    ])
    if [r["decision"] for r in result] != [
            "abi_equivalent_candidate", "remain_fail_closed", "remain_fail_closed"]:
        raise ValueError("oracle matcher self-test failed")


def write_csv(path, fields, rows):
    with pathlib.Path(path).open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader(); writer.writerows(rows)


def load(path):
    with pathlib.Path(path).open(newline="") as stream:
        return list(csv.DictReader(stream))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--sdk", default=DEFAULT_SDK)
    p.add_argument("--host-libs", default=DEFAULT_HOST_LIBS)
    p.add_argument("--manifest", default="contracts/qr39/callbacks/proxy-slot-manifest.csv")
    p.add_argument("--evidence", default="evidence/qr39/oracles/qnx-arm-evidence.csv")
    p.add_argument("--catalog", default="evidence/qr39/oracles/qnx-arm-catalog.csv")
    p.add_argument("--assessment", default="evidence/qr39/oracles/qnx-arm-assessment.csv")
    p.add_argument("--verify", action="store_true")
    p.add_argument("--normalize-objdump")
    p.add_argument("--function")
    p.add_argument("--call-index", type=int, default=0)
    args = p.parse_args()
    if args.normalize_objdump:
        funcs = parse_objdump(pathlib.Path(args.normalize_objdump).read_text())
        if not args.function or args.function not in funcs:
            raise SystemExit("--function must name a function in --normalize-objdump")
        print(";".join(call_window(funcs[args.function], args.call_index)))
        return
    with tempfile.TemporaryDirectory(prefix="qnx-arm-abi-oracle-") as work:
        catalog = build_catalog(args.sdk, args.host_libs, work)
    matcher_self_test(catalog)
    # Continue assessing the historical 63-row gap set after the independent
    # concrete-consumer audit integrates it into production.  This oracle did
    # not itself promote the newly typed rows.
    unresolved = [r for r in load(args.manifest)
                  if r["contract_source"] ==
                  "cross_version_concrete_vtable_audit"]
    evidence = load(args.evidence)
    assessment = assess(unresolved, catalog, evidence)
    if args.verify:
        if load(args.catalog) != catalog or load(args.assessment) != assessment:
            raise SystemExit("QNX ARM ABI oracle artifacts are stale; regenerate without --verify")
    else:
        write_csv(args.catalog, CATALOG_FIELDS, catalog)
        write_csv(args.assessment, ASSESS_FIELDS, assessment)
    print("QNX ARM ABI oracle: %d fixture functions, %d historical gap slots, %d promoted candidates" %
          (len(catalog), len(assessment), sum(r["decision"] == "abi_equivalent_candidate" for r in assessment)))


if __name__ == "__main__":
    main()
