// Dump decompiler output, direct references, and embedded strings for functions.
// @category VP4R
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;

public class DumpFunctions extends GhidraScript {
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        for (String argument : getScriptArgs()) {
            Address address = toAddr(Long.decode(argument));
            Function function = getFunctionContaining(address);
            if (function == null) function = getFunctionAt(address);
            println("=== requested=" + address + " function=" + function + " ===");
            if (function == null) continue;
            println("prototype=" + function.getPrototypeString(true, true));
            DecompileResults result = decompiler.decompileFunction(function, 120, monitor);
            if (result.decompileCompleted()) {
                println(result.getDecompiledFunction().getC());
            } else {
                println("DECOMPILE_ERROR " + result.getErrorMessage());
            }
            println("-- instructions and resolved references --");
            for (Instruction instruction : currentProgram.getListing().getInstructions(function.getBody(), true)) {
                StringBuilder suffix = new StringBuilder();
                for (Reference reference : instruction.getReferencesFrom()) {
                    Data data = getDataAt(reference.getToAddress());
                    if (data != null && data.hasStringValue()) {
                        suffix.append(" STRING=").append(data.getValue());
                    }
                    Function target = getFunctionAt(reference.getToAddress());
                    if (target != null) suffix.append(" TARGET=").append(target);
                }
                if (suffix.length() != 0) println(instruction.getAddress() + " " + instruction + suffix);
            }
            println("-- callers --");
            for (Reference reference : getReferencesTo(function.getEntryPoint())) {
                println(reference.getFromAddress() + " " + reference.getReferenceType() +
                        " in " + getFunctionContaining(reference.getFromAddress()));
            }
        }
        decompiler.dispose();
    }
}
