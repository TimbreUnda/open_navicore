// Dump decompiler p-code definition/use trees for exact virtual dispatches.
// This is a diagnostic companion to RecoverCallbackReceiverDataflow.java.
// @category NaviCore

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.HighVariable;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.PcodeOpAST;
import ghidra.program.model.pcode.Varnode;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Set;

public class TraceCallbackReceiverDataflow extends GhidraScript {
    private String trace(Varnode node, int depth, Set<Varnode> active) {
        if (node == null) return "null";
        if (depth > 18) return "DEPTH(" + node + ")";
        if (!active.add(node)) return "CYCLE(" + node + ")";
        try {
            if (node.isConstant()) return String.format("CONST(0x%x)", node.getOffset());
            if (node.isAddress()) return "ADDR(" + node.getAddress() + ")";
            PcodeOp def = node.getDef();
            if (def == null) {
                HighVariable high = node.getHigh();
                String name = high == null ? "" : ":" + high.getName();
                return "INPUT(" + node + name + ")";
            }
            int opcode = def.getOpcode();
            String opname = def.getMnemonic();
            if (opcode == PcodeOp.COPY || opcode == PcodeOp.CAST ||
                opcode == PcodeOp.INT_ZEXT || opcode == PcodeOp.INT_SEXT ||
                opcode == PcodeOp.SUBPIECE || opcode == PcodeOp.INDIRECT)
                return opname + "(" + trace(def.getInput(0), depth + 1, active) + ")";
            List<String> parts = new ArrayList<>();
            for (int i = opcode == PcodeOp.LOAD ? 1 : 0; i < def.getNumInputs(); ++i)
                parts.add(trace(def.getInput(i), depth + 1, active));
            return opname + "(" + String.join(",", parts) + ")@" +
                def.getSeqnum().getTarget();
        } finally {
            active.remove(node);
        }
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) throw new IllegalArgumentException("callsite ...");
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        decompiler.openProgram(currentProgram);
        try {
            for (String text : args) {
                Address callsite = toAddr(text);
                Function function = getFunctionContaining(callsite);
                println("=== " + callsite + " function=" +
                    (function == null ? "null" : function.getEntryPoint()) + " ===");
                if (function == null) continue;
                DecompileResults results = decompiler.decompileFunction(function, 90, monitor);
                if (!results.decompileCompleted()) {
                    println("DECOMPILE_FAILED " + results.getErrorMessage());
                    continue;
                }
                HighFunction high = results.getHighFunction();
                Iterator<PcodeOpAST> iterator = high.getPcodeOps(callsite);
                int found = 0;
                while (iterator.hasNext()) {
                    PcodeOpAST op = iterator.next();
                    if (op.getOpcode() != PcodeOp.CALLIND) continue;
                    ++found;
                    println("target=" + trace(op.getInput(0), 0, new HashSet<Varnode>()));
                    for (int i = 1; i < op.getNumInputs(); ++i)
                        println("arg" + (i - 1) + "=" +
                            trace(op.getInput(i), 0, new HashSet<Varnode>()));
                }
                if (found == 0) println("NO_CALLIND_AT_ADDRESS");
            }
        } finally {
            decompiler.dispose();
        }
    }
}
