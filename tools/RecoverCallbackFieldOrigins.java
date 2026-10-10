// Context-sensitive receiver-field origin recovery for callback-shaped calls.
//
// This script deliberately treats receiver equality as only a starting point.
// It expands HighParam values through every direct incoming CALL context while
// preserving loads and constant field offsets.  Indirect entries are reported
// with their concrete data/vtable references; they are never guessed to be a
// callback merely from a caller name or nearby CI string.
// @category NaviCore

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.HighParam;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.PcodeOpAST;
import ghidra.program.model.pcode.Varnode;
import ghidra.program.model.symbol.Reference;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.PrintWriter;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class RecoverCallbackFieldOrigins extends GhidraScript {
    private static final int MAX_CALL_DEPTH = 5;
    private static final int MAX_EXPR_DEPTH = 32;
    private static final int MAX_ORIGINS = 96;

    private static class Candidate {
        String function, callsite, slot, base, classification, category;
        Candidate(String[] f) {
            function=f[0]; callsite=f[1]; slot=f[2]; base=f[3];
            classification=f[4]; category=f[5];
        }
        String key() { return function + "," + callsite + "," + slot; }
    }

    private static class Dec {
        Function function;
        HighFunction high;
        String error;
    }

    private DecompInterface decompiler;
    private final Map<Address,Dec> cache = new HashMap<>();
    private final Map<Address,Boolean> constructorCache = new HashMap<>();

    private Dec decompile(Function function) {
        if (function == null) return null;
        Dec old=cache.get(function.getEntryPoint());
        if (old != null) return old;
        Dec out=new Dec(); out.function=function;
        DecompileResults result=decompiler.decompileFunction(function,45,monitor);
        if (result != null && result.decompileCompleted()) out.high=result.getHighFunction();
        else out.error=result == null ? "missing" : result.getErrorMessage();
        cache.put(function.getEntryPoint(),out);
        return out;
    }

    private Varnode strip(Varnode node) {
        Set<Varnode> seen=new HashSet<>();
        while (node != null && seen.add(node)) {
            PcodeOp def=node.getDef();
            if (def == null) break;
            int op=def.getOpcode();
            if (op != PcodeOp.COPY && op != PcodeOp.CAST &&
                op != PcodeOp.INT_ZEXT && op != PcodeOp.INT_SEXT &&
                op != PcodeOp.SUBPIECE && op != PcodeOp.INDIRECT) break;
            node=def.getInput(0);
        }
        return node;
    }

    private String offset(PcodeOp op) {
        long total=0; boolean found=false;
        for (int i=1;i<op.getNumInputs();++i) {
            Varnode n=strip(op.getInput(i));
            if (n != null && n.isConstant()) { total += n.getOffset(); found=true; }
            else return "?";
        }
        return found ? String.format("0x%x",total) : "?";
    }

    private LinkedHashSet<String> cap(Set<String> values, String marker) {
        List<String> sorted=new ArrayList<>(values); Collections.sort(sorted);
        LinkedHashSet<String> out=new LinkedHashSet<>();
        for (String value : sorted) {
            if (out.size() == MAX_ORIGINS) { out.add("truncated:"+marker); break; }
            out.add(value);
        }
        return out;
    }

    private List<Reference> directCallers(Function function) {
        List<Reference> out=new ArrayList<>();
        for (Reference ref : getReferencesTo(function.getEntryPoint()))
            if (ref.getReferenceType().isCall()) out.add(ref);
        out.sort(Comparator.comparing(r -> r.getFromAddress()));
        return out;
    }

    private List<String> dataMembership(Function function) {
        List<String> out=new ArrayList<>();
        for (Reference ref : getReferencesTo(function.getEntryPoint()))
            if (ref.getReferenceType().isData())
                out.add(ref.getFromAddress().toString()+":"+ref.getReferenceType());
        Collections.sort(out); return out;
    }

    private Varnode actualAt(Dec caller, Address callsite, Function target, int slot) {
        if (caller == null || caller.high == null) return null;
        Iterator<PcodeOpAST> at=caller.high.getPcodeOps(callsite);
        while (at.hasNext()) {
            PcodeOpAST op=at.next();
            if (op.getOpcode()!=PcodeOp.CALL || op.getNumInputs()<=slot+1) continue;
            Varnode dest=strip(op.getInput(0));
            if (dest != null && dest.isAddress() &&
                dest.getAddress().equals(target.getEntryPoint())) return op.getInput(slot+1);
        }
        // Thumb references can be attached to the containing instruction while
        // the p-code sequence target differs by a byte.  Accept only the unique
        // direct CALL at that instruction, never a CALLIND.
        PcodeOpAST unique=null;
        at=caller.high.getPcodeOps(callsite);
        while (at.hasNext()) {
            PcodeOpAST op=at.next();
            if (op.getOpcode()==PcodeOp.CALL && op.getNumInputs()>slot+1) {
                if (unique != null) return null;
                unique=op;
            }
        }
        return unique == null ? null : unique.getInput(slot+1);
    }

    private LinkedHashSet<String> resolveParam(HighParam param, Dec current,
            int callDepth, Set<String> active) {
        int slot=param.getSlot();
        List<Reference> callers=directCallers(current.function);
        LinkedHashSet<String> out=new LinkedHashSet<>();
        if (callDepth >= MAX_CALL_DEPTH)
            out.add("depth:"+current.function.getEntryPoint()+":arg"+slot);
        else for (Reference ref : callers) {
            Function callerFn=getFunctionContaining(ref.getFromAddress());
            Dec caller=decompile(callerFn);
            Varnode actual=actualAt(caller,ref.getFromAddress(),current.function,slot);
            String edge=(callerFn==null?"none":callerFn.getEntryPoint())+">"+
                current.function.getEntryPoint()+"@"+ref.getFromAddress()+":arg"+slot;
            if (actual == null) out.add("unrecovered-call:"+edge);
            else for (String origin : resolve(actual,caller,callDepth+1,0,active))
                out.add("via:"+edge+"("+origin+")");
        }
        if (callers.isEmpty()) {
            List<String> memberships=dataMembership(current.function);
            if (memberships.isEmpty())
                out.add("entry-unreferenced:"+current.function.getEntryPoint()+":arg"+slot);
            else
                out.add("indirect-entry:"+current.function.getEntryPoint()+":arg"+slot+
                    ":data["+String.join(";",memberships)+"]");
        }
        return cap(out,"param");
    }

    private LinkedHashSet<String> resolveReturn(Function target, int callDepth,
            Set<String> active) {
        LinkedHashSet<String> out=new LinkedHashSet<>();
        Dec callee=decompile(target);
        if (callee==null || callee.high==null) {
            out.add("return-unrecovered:"+(target==null?"indirect":target.getEntryPoint()));
            return out;
        }
        Iterator<PcodeOpAST> ops=callee.high.getPcodeOps();
        while (ops.hasNext()) {
            PcodeOpAST op=ops.next();
            if (op.getOpcode()==PcodeOp.RETURN && op.getNumInputs()>1)
                for (String value : resolve(op.getInput(1),callee,callDepth+1,0,active))
                    out.add("return:"+target.getEntryPoint()+"("+value+")");
        }
        if (out.isEmpty()) out.add("return-void:"+target.getEntryPoint());
        return cap(out,"return");
    }

    private boolean basedOnThis(Varnode raw, Set<Varnode> active) {
        Varnode node=strip(raw);
        if (node==null || !active.add(node)) return false;
        try {
            if (node.getHigh() instanceof HighParam)
                return ((HighParam)node.getHigh()).getSlot()==0;
            PcodeOp def=node.getDef();
            if (def==null) return false;
            int opcode=def.getOpcode();
            if (opcode==PcodeOp.INT_ADD || opcode==PcodeOp.PTRADD ||
                opcode==PcodeOp.PTRSUB)
                return basedOnThis(def.getInput(0),active);
            if (opcode==PcodeOp.MULTIEQUAL) {
                if (def.getNumInputs()==0) return false;
                for (int i=0;i<def.getNumInputs();++i)
                    if (!basedOnThis(def.getInput(i),active)) return false;
                return true;
            }
            return false;
        } finally { active.remove(node); }
    }

    private boolean dependsOnParameter(Varnode raw, Set<Varnode> active) {
        Varnode node=strip(raw);
        if (node==null || !active.add(node)) return false;
        try {
            if (node.getHigh() instanceof HighParam) return true;
            PcodeOp def=node.getDef();
            if (def==null) return false;
            for (int i=0;i<def.getNumInputs();++i)
                if (dependsOnParameter(def.getInput(i),active)) return true;
            return false;
        } finally { active.remove(node); }
    }

    // A direct function is accepted as a provider-side concrete constructor
    // only when it stores a nonzero, non-parameter-derived value through arg0.
    // This is stronger than a function/vtable membership guess: it proves the
    // candidate object's dispatch table is installed by this provider image.
    private boolean installsProviderVtable(Function target) {
        if (target==null) return false;
        Boolean old=constructorCache.get(target.getEntryPoint());
        if (old!=null) return old;
        boolean found=false; Dec dec=decompile(target);
        if (dec!=null && dec.high!=null) {
            Iterator<PcodeOpAST> ops=dec.high.getPcodeOps();
            while (ops.hasNext()) {
                PcodeOpAST op=ops.next();
                if (op.getOpcode()!=PcodeOp.STORE || op.getNumInputs()<3 ||
                    !basedOnThis(op.getInput(1),new HashSet<Varnode>())) continue;
                Varnode value=strip(op.getInput(2));
                if (value==null || (value.isConstant() && value.getOffset()==0) ||
                    dependsOnParameter(value,new HashSet<Varnode>())) continue;
                found=true; break;
            }
        }
        constructorCache.put(target.getEntryPoint(),found); return found;
    }

    private List<String> constructedUses(Varnode raw) {
        Varnode node=strip(raw); List<String> out=new ArrayList<>();
        if (node==null) return out;
        Iterator<PcodeOp> uses=node.getDescendants();
        while (uses.hasNext()) {
            PcodeOp use=uses.next();
            Varnode actual=use.getNumInputs()<2 ? null : strip(use.getInput(1));
            if (use.getOpcode()!=PcodeOp.CALL || actual==null ||
                !actual.equals(node)) continue;
            Varnode dest=strip(use.getInput(0));
            Function target=dest!=null && dest.isAddress() ? getFunctionAt(dest.getAddress()) : null;
            if (installsProviderVtable(target))
                out.add("provider-constructor:"+target.getEntryPoint()+"@"+
                    use.getSeqnum().getTarget());
        }
        Collections.sort(out); return out;
    }

    private LinkedHashSet<String> resolve(Varnode raw, Dec current, int callDepth,
            int exprDepth, Set<String> active) {
        LinkedHashSet<String> out=new LinkedHashSet<>();
        Varnode node=strip(raw);
        if (node==null) { out.add("null"); return out; }
        if (exprDepth>MAX_EXPR_DEPTH) { out.add("expr-depth:"+node); return out; }
        String token=(current==null?"none":current.function.getEntryPoint())+"|"+node;
        if (!active.add(token)) { out.add("cycle:"+token); return out; }
        try {
            if (node.isConstant()) { out.add(String.format("constant:0x%x",node.getOffset())); return out; }
            if (node.isAddress()) { out.add("address:"+node.getAddress()); return out; }
            if (node.getHigh() instanceof HighParam && current!=null)
                return resolveParam((HighParam)node.getHigh(),current,callDepth,active);
            PcodeOp def=node.getDef();
            if (def==null) { out.add("local-input:"+node.getAddress()+":"+node.getSize()); return out; }
            int opcode=def.getOpcode();
            if (opcode==PcodeOp.LOAD) {
                for (String base : resolve(def.getInput(1),current,callDepth,exprDepth+1,active))
                    out.add("load("+base+")");
            } else if (opcode==PcodeOp.INT_ADD || opcode==PcodeOp.PTRSUB || opcode==PcodeOp.PTRADD) {
                String off=offset(def);
                for (String base : resolve(def.getInput(0),current,callDepth,exprDepth+1,active))
                    out.add("field("+base+","+off+")");
            } else if (opcode==PcodeOp.MULTIEQUAL) {
                for (int i=0;i<def.getNumInputs();++i)
                    out.addAll(resolve(def.getInput(i),current,callDepth,exprDepth+1,active));
            } else if (opcode==PcodeOp.CALL) {
                Varnode dest=strip(def.getInput(0));
                Function target=dest!=null && dest.isAddress() ? getFunctionAt(dest.getAddress()) : null;
                List<String> constructors=constructedUses(node);
                if (!constructors.isEmpty()) out.addAll(constructors);
                else out.addAll(resolveReturn(target,callDepth,active));
            } else if (opcode==PcodeOp.CALLIND) {
                out.add("indirect-return:"+def.getSeqnum().getTarget());
            } else {
                for (int i=0;i<def.getNumInputs();++i)
                    out.addAll(resolve(def.getInput(i),current,callDepth,exprDepth+1,active));
                if (out.isEmpty()) out.add("op:"+def.getMnemonic()+"@"+def.getSeqnum().getTarget());
            }
            return cap(out,"expr");
        } finally { active.remove(token); }
    }

    private Varnode receiver(Candidate candidate, Dec dec) {
        if (dec==null || dec.high==null) return null;
        Iterator<PcodeOpAST> ops=dec.high.getPcodeOps(toAddr(candidate.callsite));
        while (ops.hasNext()) {
            PcodeOpAST op=ops.next();
            if (op.getOpcode()==PcodeOp.CALLIND && op.getNumInputs()>1) return op.getInput(1);
        }
        return null;
    }

    private static String csv(String value) {
        if (value==null) return "";
        if (value.indexOf(',')<0 && value.indexOf('"')<0 && value.indexOf('\n')<0) return value;
        return "\""+value.replace("\"","\"\"")+"\"";
    }

    private static String digest(List<String> origins) throws Exception {
        MessageDigest md=MessageDigest.getInstance("SHA-256");
        for (String value : origins) md.update((value+"\n").getBytes("UTF-8"));
        byte[] bytes=md.digest(); StringBuilder out=new StringBuilder();
        for (byte b : bytes) out.append(String.format("%02x",b));
        return out.substring(0,16);
    }

    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        if (args.length!=4) throw new IllegalArgumentException(
            "usage: outside-inventory.csv arm-slices.csv receiver-dataflow.csv field-origins.csv");
        Set<String> armEqual=new HashSet<>();
        Map<String,String> armOrigins=new HashMap<>();
        try (BufferedReader reader=new BufferedReader(new FileReader(args[1]))) {
            String line; boolean header=true;
            while ((line=reader.readLine())!=null) {
                if (header) { header=false; continue; }
                String[] f=line.split(",",-1);
                if (f.length>=6 && f[4].equals("arm_receiver_matches_vtable_object")) {
                    String key=f[0]+","+f[1]+","+f[2];
                    armEqual.add(key); armOrigins.put(key,f[5]);
                }
            }
        }
        Map<String,String> priorPcodeRoots=new HashMap<>();
        try (BufferedReader reader=new BufferedReader(new FileReader(args[2]))) {
            String line; boolean header=true;
            while ((line=reader.readLine())!=null) {
                if (header) { header=false; continue; }
                String[] f=line.split(",",-1);
                if (f.length>=6 && f[4].equals("receiver_matches_vtable_object"))
                    priorPcodeRoots.put(f[0]+","+f[1]+","+f[2],f[5]);
            }
        }
        List<Candidate> candidates=new ArrayList<>();
        try (BufferedReader reader=new BufferedReader(new FileReader(args[0]))) {
            String line; boolean header=true;
            while ((line=reader.readLine())!=null) {
                if (header) { header=false; continue; }
                // Inventory evidence is the only quoted field and comes last;
                // the first ten fields therefore remain safe for this split.
                String[] f=line.split(",",11);
                if (f.length>=6 && f[4].equals("unresolved") && armEqual.contains(f[0]+","+f[1]+","+f[2]))
                    candidates.add(new Candidate(f));
            }
        }
        decompiler=new DecompInterface();
        decompiler.toggleCCode(false); decompiler.toggleSyntaxTree(true);
        decompiler.openProgram(currentProgram);
        PrintWriter out=new PrintWriter(new File(args[3]));
        out.println("function,callsite,slot,origin_status,origin_group,terminal_count,origins,evidence");
        int recovered=0, machineOnly=0;
        try {
            for (Candidate candidate : candidates) {
                monitor.checkCancelled();
                Function fn=getFunctionAt(toAddr(candidate.function));
                Dec dec=decompile(fn); Varnode recv=receiver(candidate,dec);
                List<String> origins=new ArrayList<>(); String status,evidence;
                if (recv==null) {
                    String prior=priorPcodeRoots.get(candidate.key());
                    if (prior != null) {
                        status="traced_local_dataflow_only"; ++recovered;
                        origins.add("prior-pcode:"+prior);
                        evidence="independent 120-second p-code pass recovered the equal local receiver; this shorter interprocedural pass timed out; no ownership inferred";
                    } else {
                        status="unresolved_machine_only"; ++machineOnly;
                        origins.add("machine-equal:"+armOrigins.get(candidate.key()));
                        evidence="ARM equality is proven but decompiler exposed no receiver varnode; no ownership inferred";
                    }
                } else {
                    origins.addAll(resolve(recv,dec,0,0,new HashSet<String>()));
                    Collections.sort(origins); ++recovered;
                    boolean concrete=!origins.isEmpty();
                    for (String origin : origins)
                        if (!origin.contains("provider-constructor:")) concrete=false;
                    if (concrete) {
                        status="proven_provider_constructed_noncallback";
                        evidence="every context terminates in a direct provider constructor that stores a dispatch table through its this argument";
                    } else {
                        status="traced_no_registered_ci_owner";
                        evidence="context-sensitive HighParam/field/caller expansion; terminal is not registered-CI ownership proof";
                    }
                }
                String joined=String.join(";",origins);
                out.println(candidate.function+","+candidate.callsite+","+candidate.slot+","+
                    status+","+digest(origins)+","+origins.size()+","+csv(joined)+","+csv(evidence));
            }
        } finally { out.close(); decompiler.dispose(); }
        println("Wrote "+candidates.size()+" equal-origin rows: "+recovered+
            " context-traced, "+machineOnly+" machine-only");
    }
}
