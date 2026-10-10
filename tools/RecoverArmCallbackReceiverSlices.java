// Recover machine-level receiver/vtable provenance at every exact BLX chain.
// This is deliberately independent of decompiler CALLIND parameter recovery.
// @category NaviCore

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class RecoverArmCallbackReceiverSlices extends GhidraScript {
    private static final String STOCK_SHA256 = "fd1e931430cca61116c533cdf617dc805548e312a9131e972f55a7d30ab64842";
    private static final int MAX_DEPTH = 320;
    private static final int MAX_VALUES = 24;
    private static final Pattern REGISTER = Pattern.compile("(?:r(?:1[0-2]|[0-9])|sp|fp|ip|lr)");
    private static final Pattern BLX = Pattern.compile("(?:blx|bx)(?:\\.[a-z]+)*(r(?:1[0-2]|[0-9]))");
    private static final Pattern MOVE = Pattern.compile("mov(?:\\.[a-z]+)*(r(?:1[0-2]|[0-9])),(r(?:1[0-2]|[0-9])|sp|fp|ip)");
    private static final Pattern LOAD = Pattern.compile("ldr(?:\\.[a-z]+)*(r(?:1[0-2]|[0-9])),\\[(r(?:1[0-2]|[0-9])|sp|fp|ip)(?:,#(-?0x[0-9a-f]+|-?[0-9]+))?\\]");
    private static final Pattern STORE = Pattern.compile("str(?:\\.[a-z]+)*(r(?:1[0-2]|[0-9])),\\[(r(?:1[0-2]|[0-9])|sp|fp|ip)(?:,#(-?0x[0-9a-f]+|-?[0-9]+))?\\]");
    private static final Pattern ARITH3 = Pattern.compile("(add|sub)(?:s|\\.[a-z]+)*(r(?:1[0-2]|[0-9])|sp),(r(?:1[0-2]|[0-9])|sp|fp|ip),#(-?0x[0-9a-f]+|-?[0-9]+)");
    private static final Pattern ARITH2 = Pattern.compile("(add|sub)(?:s|\\.[a-z]+)*(r(?:1[0-2]|[0-9])|sp),#(-?0x[0-9a-f]+|-?[0-9]+)");

    private static class Candidate {
        String function, callsite, slot, base;
        Candidate(String[] f) { function=f[0]; callsite=f[1]; slot=f[2]; base=f[3]; }
    }

    private static class Expr {
        final String op;
        final String atom;
        final Expr child;
        final long offset;
        Expr(String op, String atom, Expr child, long offset) {
            this.op=op; this.atom=atom; this.child=child; this.offset=offset;
        }
        static Expr atom(String text) { return new Expr("atom",text,null,0); }
        static Expr load(Expr address) { return new Expr("load",null,address,0); }
        static Expr add(Expr base, long value) {
            if (value == 0) return base;
            if (base != null && base.op.equals("add"))
                return new Expr("add",null,base.child,base.offset + value);
            return new Expr("add",null,base,value);
        }
        boolean uncertain() {
            return (op.equals("atom") && (atom.startsWith("cycle:") ||
                atom.startsWith("depth:") || atom.startsWith("ambiguous:") ||
                atom.startsWith("write:") || atom.startsWith("call:"))) ||
                (child != null && child.uncertain());
        }
        void roots(Set<String> out) {
            if (op.equals("atom")) { out.add(atom); return; }
            if (op.equals("load")) {
                // Retain the full address identity: two independent fields of
                // one object are not the same receiver origin.
                out.add(toString());
                return;
            }
            child.roots(out);
        }
        @Override public String toString() {
            if (op.equals("atom")) return atom;
            if (op.equals("load")) return "load(" + child + ")";
            return child + (offset < 0 ? "-0x" : "+0x") +
                Long.toHexString(Math.abs(offset));
        }
        @Override public boolean equals(Object other) {
            return other instanceof Expr && toString().equals(other.toString());
        }
        @Override public int hashCode() { return toString().hashCode(); }
    }

    private Listing listing;
    private Function function;
    private Map<Address,List<Instruction>> predecessors;
    private Map<String,LinkedHashSet<Expr>> beforeMemo;
    private Map<String,LinkedHashSet<Expr>> memoryMemo;
    private Set<String> active;

    private static String norm(String value) {
        value=value.toLowerCase();
        if (value.equals("sp")) return "r13";
        if (value.equals("fp")) return "r11";
        if (value.equals("ip")) return "r12";
        return value;
    }

    private static long number(String text) {
        if (text == null || text.isEmpty()) return 0;
        boolean negative=text.charAt(0)=='-';
        String body=negative ? text.substring(1) : text;
        long value=body.startsWith("0x") ? Long.parseLong(body.substring(2),16) : Long.parseLong(body);
        return negative ? -value : value;
    }

    private static String clean(Instruction instruction) {
        return instruction.toString().toLowerCase().replace(" ","");
    }

    private LinkedHashSet<Expr> one(Expr value) {
        LinkedHashSet<Expr> result=new LinkedHashSet<>(); result.add(value); return result;
    }

    private LinkedHashSet<Expr> capped(Set<Expr> values, String why) {
        LinkedHashSet<Expr> result=new LinkedHashSet<>();
        for (Expr value : values) {
            if (result.size() >= MAX_VALUES) return one(Expr.atom("ambiguous:"+why));
            result.add(value);
        }
        return result;
    }

    private boolean writes(Instruction instruction, String register) {
        register=norm(register);
        String text=clean(instruction);
        Matcher move=MOVE.matcher(text), load=LOAD.matcher(text), a3=ARITH3.matcher(text), a2=ARITH2.matcher(text);
        if (move.matches() && norm(move.group(1)).equals(register)) return true;
        if (load.matches() && norm(load.group(1)).equals(register)) return true;
        if (a3.matches() && norm(a3.group(2)).equals(register)) return true;
        if (a2.matches() && norm(a2.group(2)).equals(register)) return true;
        String mnemonic=instruction.getMnemonicString().toLowerCase();
        if ((mnemonic.startsWith("bl") || mnemonic.equals("svc")) &&
            (register.equals("r0") || register.equals("r1") || register.equals("r2") ||
             register.equals("r3") || register.equals("r12") || register.equals("lr"))) return true;
        for (Object object : instruction.getResultObjects())
            if (norm(object.toString()).equals(register)) return true;
        return false;
    }

    private boolean conditional(Instruction instruction) {
        String mnemonic=instruction.getMnemonicString().toLowerCase();
        return mnemonic.contains(".") && !mnemonic.endsWith(".w");
    }

    private LinkedHashSet<Expr> before(Instruction instruction, String register, int depth) {
        register=norm(register);
        String key=instruction.getAddress()+"|"+register;
        if (beforeMemo.containsKey(key)) return beforeMemo.get(key);
        if (depth > MAX_DEPTH) return one(Expr.atom("depth:"+key));
        if (!active.add("b:"+key)) return one(Expr.atom("cycle:"+key));
        LinkedHashSet<Expr> result=new LinkedHashSet<>();
        List<Instruction> preds=predecessors.get(instruction.getAddress());
        if (preds == null || preds.isEmpty()) result.add(Expr.atom("entry:"+register));
        else for (Instruction pred : preds) result.addAll(after(pred,register,depth+1));
        active.remove("b:"+key);
        result=capped(result,key);
        beforeMemo.put(key,result);
        return result;
    }

    private LinkedHashSet<Expr> after(Instruction instruction, String register, int depth) {
        if (!writes(instruction,register)) return before(instruction,register,depth+1);
        LinkedHashSet<Expr> written=evalWrite(instruction,register,depth+1);
        if (conditional(instruction)) written.addAll(before(instruction,register,depth+1));
        return capped(written,instruction.getAddress()+"|"+register);
    }

    private LinkedHashSet<Expr> evalWrite(Instruction instruction, String register, int depth) {
        String text=clean(instruction), address=instruction.getAddress().toString();
        Matcher matcher=MOVE.matcher(text);
        if (matcher.matches() && norm(matcher.group(1)).equals(register))
            return before(instruction,norm(matcher.group(2)),depth+1);
        matcher=LOAD.matcher(text);
        if (matcher.matches() && norm(matcher.group(1)).equals(register)) {
            long offset=number(matcher.group(3));
            LinkedHashSet<Expr> addresses=new LinkedHashSet<>();
            for (Expr base : before(instruction,norm(matcher.group(2)),depth+1))
                addresses.add(Expr.add(base,offset));
            LinkedHashSet<Expr> values=new LinkedHashSet<>();
            for (Expr location : addresses) {
                if (isStack(location)) values.addAll(memoryBefore(instruction,location,depth+1));
                else values.add(Expr.load(location));
            }
            return capped(values,address+"|load");
        }
        matcher=ARITH3.matcher(text);
        if (matcher.matches() && norm(matcher.group(2)).equals(register)) {
            long value=number(matcher.group(4)); if (matcher.group(1).equals("sub")) value=-value;
            LinkedHashSet<Expr> values=new LinkedHashSet<>();
            for (Expr base : before(instruction,norm(matcher.group(3)),depth+1)) values.add(Expr.add(base,value));
            return capped(values,address+"|arith");
        }
        matcher=ARITH2.matcher(text);
        if (matcher.matches() && norm(matcher.group(2)).equals(register)) {
            long value=number(matcher.group(3)); if (matcher.group(1).equals("sub")) value=-value;
            LinkedHashSet<Expr> values=new LinkedHashSet<>();
            for (Expr base : before(instruction,register,depth+1)) values.add(Expr.add(base,value));
            return capped(values,address+"|arith");
        }
        String mnemonic=instruction.getMnemonicString().toLowerCase();
        if (mnemonic.startsWith("bl") || mnemonic.equals("svc"))
            return one(Expr.atom("call:"+address+":"+register));
        return one(Expr.atom("write:"+address+":"+register));
    }

    private boolean isStack(Expr expr) {
        String text=expr.toString();
        return text.startsWith("entry:r13") || text.contains(":r13");
    }

    private LinkedHashSet<Expr> memoryBefore(Instruction instruction, Expr location, int depth) {
        String key=instruction.getAddress()+"|"+location;
        if (memoryMemo.containsKey(key)) return memoryMemo.get(key);
        if (depth > MAX_DEPTH) return one(Expr.atom("depth:mem:"+key));
        if (!active.add("m:"+key)) return one(Expr.atom("cycle:mem:"+key));
        LinkedHashSet<Expr> values=new LinkedHashSet<>();
        List<Instruction> preds=predecessors.get(instruction.getAddress());
        if (preds == null || preds.isEmpty()) values.add(Expr.load(location));
        else for (Instruction pred : preds) {
            Matcher store=STORE.matcher(clean(pred));
            boolean matched=false;
            if (store.matches()) {
                long offset=number(store.group(3));
                for (Expr base : before(pred,norm(store.group(2)),depth+1)) {
                    if (Expr.add(base,offset).equals(location)) {
                        values.addAll(before(pred,norm(store.group(1)),depth+1)); matched=true;
                    }
                }
            }
            if (!matched) values.addAll(memoryBefore(pred,location,depth+1));
        }
        active.remove("m:"+key);
        values=capped(values,"mem:"+key); memoryMemo.put(key,values); return values;
    }

    private Expr vtableObject(Expr target, long slotBytes) {
        if (target == null || !target.op.equals("load")) return null;
        Expr address=target.child;
        if (address.op.equals("add")) {
            if (address.offset != slotBytes) return null;
            address=address.child;
        } else if (slotBytes != 0) return null;
        if (!address.op.equals("load")) return null;
        return address.child;
    }

    // A local def/use proof is stronger than expanding a loop-carried object's
    // earlier value: if the target is loaded from r0's vtable and r0 is not
    // overwritten before BLX, receiver equality holds for every incoming path.
    private boolean directR0VtableObject(Instruction call, String targetRegister,
            long slotBytes) {
        String wanted=norm(targetRegister); boolean haveTarget=false;
        Instruction cursor=call.getPrevious(); int count=0;
        while (cursor != null && function.getBody().contains(cursor.getAddress()) && count++ < 160) {
            String text=clean(cursor); Matcher move=MOVE.matcher(text), load=LOAD.matcher(text);
            if (!haveTarget) {
                if (move.matches() && norm(move.group(1)).equals(wanted)) {
                    wanted=norm(move.group(2)); cursor=cursor.getPrevious(); continue;
                }
                if (load.matches() && norm(load.group(1)).equals(wanted) &&
                        number(load.group(3)) == slotBytes) {
                    wanted=norm(load.group(2)); haveTarget=true; cursor=cursor.getPrevious(); continue;
                }
                if (writes(cursor,wanted)) return false;
            } else {
                if (move.matches() && norm(move.group(1)).equals(wanted)) {
                    wanted=norm(move.group(2)); cursor=cursor.getPrevious(); continue;
                }
                if (load.matches() && norm(load.group(1)).equals(wanted) &&
                        number(load.group(3)) == 0) {
                    String object=norm(load.group(2));
                    if (!object.equals("r0")) return false;
                    // The forward interval was already traversed.  Any r0
                    // write in it would have made the vtable object stale or
                    // different; rescan that small interval explicitly.
                    for (Instruction between=cursor.getNext(); between != null &&
                            between.getAddress().compareTo(call.getAddress()) < 0;
                            between=between.getNext())
                        if (writes(between,"r0")) return false;
                    return true;
                }
                if (writes(cursor,wanted)) return false;
            }
            cursor=cursor.getPrevious();
        }
        return false;
    }

    private void buildGraph(Function fn) {
        function=fn; predecessors=new LinkedHashMap<>(); beforeMemo=new HashMap<>();
        memoryMemo=new HashMap<>(); active=new HashSet<>();
        List<Instruction> instructions=new ArrayList<>();
        for (Instruction ins : listing.getInstructions(fn.getBody(),true)) instructions.add(ins);
        Set<Address> body=new HashSet<>(); for (Instruction ins : instructions) body.add(ins.getAddress());
        for (Instruction ins : instructions) {
            Address fall=ins.getFallThrough();
            if (fall != null && body.contains(fall)) predecessors.computeIfAbsent(fall,k->new ArrayList<>()).add(ins);
            if (!ins.getFlowType().isCall()) for (Address flow : ins.getFlows())
                if (body.contains(flow)) predecessors.computeIfAbsent(flow,k->new ArrayList<>()).add(ins);
        }
    }

    private String join(Set<Expr> values) {
        List<String> text=new ArrayList<>(); for (Expr value : values) text.add(value.toString());
        return String.join(";",text).replace(',',':');
    }

    private String relation(Set<Expr> receivers, Set<Expr> objects) {
        if (receivers.isEmpty() || objects.isEmpty()) return "arm_slice_unavailable";
        boolean exact=false, uncertain=false; Set<String> rr=new HashSet<>(), oo=new HashSet<>();
        for (Expr value : receivers) { if (value.uncertain()) uncertain=true; value.roots(rr); }
        for (Expr value : objects) { if (value.uncertain()) uncertain=true; value.roots(oo); }
        for (Expr r : receivers) if (objects.contains(r)) exact=true;
        if (exact && receivers.size()==1 && objects.size()==1) return "arm_receiver_matches_vtable_object";
        Set<String> common=new HashSet<>(rr); common.retainAll(oo);
        if (!exact && common.isEmpty() && !uncertain) return "arm_receiver_vtable_disjoint_origins";
        return "arm_receiver_vtable_relation_ambiguous";
    }

    @Override public void run() throws Exception {
        String[] args=getScriptArgs(); if (args.length != 2)
            throw new IllegalArgumentException("usage: exact.csv arm-slices.csv");
        if (!STOCK_SHA256.equals(currentProgram.getExecutableSHA256()))
            throw new IllegalStateException("wrong stock provider SHA-256: "+
                currentProgram.getExecutableSHA256());
        if (currentProgram.getImageBase().getOffset() != 0x10000)
            throw new IllegalStateException("expected Ghidra image base 0x10000; got "+
                currentProgram.getImageBase());
        List<Candidate> candidates=new ArrayList<>();
        try (BufferedReader reader=new BufferedReader(new FileReader(args[0]))) {
            String line; boolean header=true; while ((line=reader.readLine()) != null) {
                if (header) { header=false; continue; } String[] f=line.split(",",-1);
                if (f.length >= 4) candidates.add(new Candidate(f));
            }
        }
        listing=currentProgram.getListing(); Function prior=null; int count=0;
        try (PrintWriter out=new PrintWriter(new File(args[1]))) {
            out.println("function,callsite,slot,base_register,relation,receiver_origins,vtable_object_origins,evidence");
            for (Candidate c : candidates) {
                monitor.checkCancelled(); Function fn=getFunctionAt(toAddr(c.function));
                if (fn == null) fn=getFunctionContaining(toAddr(c.callsite));
                if (fn != prior) { if (fn != null) buildGraph(fn); prior=fn; }
                Instruction call=listing.getInstructionAt(toAddr(c.callsite));
                LinkedHashSet<Expr> receivers=new LinkedHashSet<>(), objects=new LinkedHashSet<>();
                String evidence="instruction-level backward slice unavailable";
                if (fn != null && call != null) {
                    Matcher branch=BLX.matcher(clean(call));
                    if (branch.matches()) {
                        receivers=before(call,"r0",0);
                        Set<Expr> targets=before(call,norm(branch.group(1)),0);
                        long bytes=Long.parseLong(c.slot)*4;
                        for (Expr target : targets) { Expr object=vtableObject(target,bytes); if (object != null) objects.add(object); }
                        evidence="ARM/Thumb CFG backward slice through register definitions and stack stores; target slot checked exactly";
                        if (directR0VtableObject(call,branch.group(1),bytes)) {
                            receivers=one(Expr.atom("machine:r0@"+c.callsite));
                            objects=one(Expr.atom("machine:r0@"+c.callsite));
                            evidence="local ARM/Thumb def/use proves target is loaded from unchanged machine r0 vtable; exact target slot checked";
                        }
                    } else evidence="candidate address is not an indirect branch in imported listing";
                }
                String rel=relation(receivers,objects);
                out.printf("%s,%s,%s,%s,%s,%s,%s,%s%n",c.function,c.callsite,c.slot,c.base,
                    rel,join(receivers),join(objects),evidence.replace(',', ';')); count++;
            }
        }
        println("Wrote "+count+" ARM receiver slices to "+args[1]);
    }
}
