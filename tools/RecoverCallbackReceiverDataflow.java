// Reproducibly recover receiver/vtable-object dataflow for every exact-chain
// virtual dispatch.  The output is an evidence input: it proves equality or a
// concrete mismatch, but deliberately does not infer callback ownership from
// the callback-shaped instruction syntax.
// @category NaviCore

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.PcodeOpAST;
import ghidra.program.model.pcode.Varnode;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class RecoverCallbackReceiverDataflow extends GhidraScript {
    private static class Candidate {
        String function, callsite, slot, base;
        Candidate(String[] f) { function=f[0]; callsite=f[1]; slot=f[2]; base=f[3]; }
    }

    private Varnode strip(Varnode node) {
        Set<Varnode> seen = new HashSet<>();
        while (node != null && seen.add(node)) {
            PcodeOp def = node.getDef();
            if (def == null) break;
            int op = def.getOpcode();
            if (op != PcodeOp.COPY && op != PcodeOp.CAST &&
                op != PcodeOp.INT_ZEXT && op != PcodeOp.INT_SEXT &&
                op != PcodeOp.INDIRECT) break;
            node = def.getInput(0);
        }
        return node;
    }

    private boolean equivalent(Varnode left, Varnode right, int depth,
            Set<String> active) {
        left = strip(left); right = strip(right);
        if (left == right || (left != null && left.equals(right))) return true;
        if (left == null || right == null || depth > 24) return false;
        String pair = left.toString() + "=" + right.toString();
        if (!active.add(pair)) return false;
        try {
            if (left.isConstant() || right.isConstant())
                return left.isConstant() && right.isConstant() &&
                    left.getOffset() == right.getOffset();
            PcodeOp a = left.getDef(), b = right.getDef();
            if (a == null || b == null || a.getOpcode() != b.getOpcode() ||
                a.getNumInputs() != b.getNumInputs()) return false;
            for (int i = 0; i < a.getNumInputs(); ++i)
                if (!equivalent(a.getInput(i), b.getInput(i), depth + 1, active))
                    return false;
            return true;
        } finally { active.remove(pair); }
    }

    // Root tokens retain parameter/global/stack identity and each concrete
    // load/field offset.  They are diagnostic and are not by themselves used
    // as callback ownership proof.
    private Set<String> roots(Varnode node, int depth, Set<Varnode> active) {
        LinkedHashSet<String> out = new LinkedHashSet<>();
        node = strip(node);
        if (node == null) { out.add("null"); return out; }
        if (depth > 28 || !active.add(node)) { out.add("cycle"); return out; }
        try {
            if (node.isConstant()) {
                out.add(String.format("const:0x%x", node.getOffset())); return out;
            }
            PcodeOp def = node.getDef();
            if (def == null) {
                out.add("input:" + node.getAddress() + ":" + node.getSize()); return out;
            }
            int opcode = def.getOpcode();
            if (opcode == PcodeOp.LOAD) {
                for (String root : roots(def.getInput(1), depth + 1, active))
                    out.add("load(" + root + ")");
                return out;
            }
            if (opcode == PcodeOp.INT_ADD || opcode == PcodeOp.PTRSUB ||
                opcode == PcodeOp.PTRADD) {
                Varnode base = def.getInput(0);
                String suffix = "?";
                for (int i = 1; i < def.getNumInputs(); ++i) {
                    Varnode term = strip(def.getInput(i));
                    if (term != null && term.isConstant())
                        suffix = String.format("0x%x", term.getOffset());
                }
                for (String root : roots(base, depth + 1, active))
                    out.add(root + "+" + suffix);
                return out;
            }
            if (opcode == PcodeOp.MULTIEQUAL) {
                for (int i = 0; i < def.getNumInputs(); ++i)
                    out.addAll(roots(def.getInput(i), depth + 1, active));
                return out;
            }
            if (opcode == PcodeOp.CALL || opcode == PcodeOp.CALLIND) {
                Varnode target = strip(def.getInput(0));
                out.add(target != null && target.isAddress()
                    ? "call:" + target.getAddress() : "callind");
                return out;
            }
            for (int i = 0; i < def.getNumInputs(); ++i)
                out.addAll(roots(def.getInput(i), depth + 1, active));
            if (out.isEmpty()) out.add("op:" + def.getMnemonic());
            return out;
        } finally { active.remove(node); }
    }

    private Varnode vtableObject(Varnode target) {
        target = strip(target);
        if (target == null || target.getDef() == null ||
            target.getDef().getOpcode() != PcodeOp.LOAD) return null;
        Varnode address = strip(target.getDef().getInput(1));
        if (address == null || address.getDef() == null) return null;
        PcodeOp add = address.getDef();
        if (add.getOpcode() != PcodeOp.INT_ADD &&
            add.getOpcode() != PcodeOp.PTRADD &&
            add.getOpcode() != PcodeOp.PTRSUB) return null;
        Varnode vtable = strip(add.getInput(0));
        if (vtable == null || vtable.getDef() == null ||
            vtable.getDef().getOpcode() != PcodeOp.LOAD) return null;
        return strip(vtable.getDef().getInput(1));
    }

    private String joined(Set<String> values) {
        return String.join(";", values).replace(',', ':');
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) throw new IllegalArgumentException(
            "usage: exact-dispatches.csv receiver-dataflow.csv");
        List<Candidate> candidates = new ArrayList<>();
        try (BufferedReader reader = new BufferedReader(new FileReader(args[0]))) {
            String line; boolean header = true;
            while ((line = reader.readLine()) != null) {
                if (header) { header=false; continue; }
                String[] fields = line.split(",", -1);
                if (fields.length >= 4) candidates.add(new Candidate(fields));
            }
        }
        Map<String,List<Candidate>> byFunction = new LinkedHashMap<>();
        for (Candidate c : candidates)
            byFunction.computeIfAbsent(c.function, unused -> new ArrayList<>()).add(c);
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(false); decompiler.toggleSyntaxTree(true);
        decompiler.openProgram(currentProgram);
        PrintWriter out = new PrintWriter(new File(args[1]));
        out.println("function,callsite,slot,base_register,relation,receiver_roots,vtable_object_roots,evidence");
        int count = 0;
        try {
            for (Map.Entry<String,List<Candidate>> entry : byFunction.entrySet()) {
                monitor.checkCancelled();
                Function fn = getFunctionAt(toAddr(entry.getKey()));
                DecompileResults result = fn == null ? null :
                    decompiler.decompileFunction(fn, 120, monitor);
                HighFunction high = result != null && result.decompileCompleted()
                    ? result.getHighFunction() : null;
                for (Candidate c : entry.getValue()) {
                    String relation="callind_receiver_unavailable", receiver="", object="";
                    String evidence=result == null ? "function missing" :
                        (result.decompileCompleted() ? "no CALLIND p-code at instruction" :
                         result.getErrorMessage());
                    if (high != null) {
                        Iterator<PcodeOpAST> ops = high.getPcodeOps(toAddr(c.callsite));
                        while (ops.hasNext()) {
                            PcodeOpAST op = ops.next();
                            if (op.getOpcode() != PcodeOp.CALLIND || op.getNumInputs() < 2)
                                continue;
                            Varnode actualReceiver = strip(op.getInput(1));
                            Varnode tableObject = vtableObject(op.getInput(0));
                            receiver=joined(roots(actualReceiver,0,new HashSet<Varnode>()));
                            object=joined(roots(tableObject,0,new HashSet<Varnode>()));
                            if (tableObject == null) {
                                relation="target_shape_unrecovered";
                                evidence="CALLIND found but target is not recovered LOAD(vtable[slot])";
                            } else if (actualReceiver == tableObject ||
                                    equivalent(actualReceiver,tableObject,0,new HashSet<String>())) {
                                relation="receiver_matches_vtable_object";
                                evidence="decompiler p-code definition/use proves r0 and vtable load share one value";
                            } else {
                                Set<String> rr=roots(actualReceiver,0,new HashSet<Varnode>());
                                Set<String> oo=roots(tableObject,0,new HashSet<Varnode>());
                                Set<String> common=new HashSet<>(rr); common.retainAll(oo);
                                if (common.isEmpty() && !rr.contains("cycle") && !oo.contains("cycle")) {
                                    relation="receiver_vtable_disjoint_origins";
                                    evidence="r0 and vtable object have disjoint recovered definition/use origins";
                                } else {
                                    relation="receiver_vtable_relation_unresolved";
                                    evidence="r0 and vtable object differ but phi/cycle/shared roots prevent mismatch proof";
                                }
                            }
                            break;
                        }
                    }
                    out.printf("%s,%s,%s,%s,%s,%s,%s,%s%n", c.function,c.callsite,
                        c.slot,c.base,relation,receiver,object,evidence.replace(',', ';'));
                    ++count;
                }
            }
        } finally { out.close(); decompiler.dispose(); }
        println("Wrote " + count + " receiver dataflow rows to " + args[1]);
    }
}
