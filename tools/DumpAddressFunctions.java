// Dump bounded function listings for exact referenced addresses.
// @category Navigation

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;

import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class DumpAddressFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("usage: OUTPUT_PREFIX ADDRESS...");
        Set<Address> emitted = new LinkedHashSet<>();
        try (PrintWriter summary = new PrintWriter(args[0] + ".tsv");
             PrintWriter listing = new PrintWriter(args[0] + ".listing.txt")) {
            summary.println("target_address\tref_address\tfunction_entry\tfunction_name");
            for (int n = 1; n < args.length; n++) {
                Address target = toAddr(Long.parseUnsignedLong(args[n].replaceFirst("^0[xX]", ""), 16));
                for (Reference ref : getReferencesTo(target)) {
                    Function f = getFunctionContaining(ref.getFromAddress());
                    if (f == null) continue;
                    summary.printf("%s\t%s\t%s\t%s%n", target, ref.getFromAddress(),
                        f.getEntryPoint(), f.getName(true).replace('\t', ' '));
                    if (!emitted.add(f.getEntryPoint())) continue;
                    listing.println("FUNCTION " + f.getEntryPoint() + " " + f.getName(true));
                    InstructionIterator ins = currentProgram.getListing().getInstructions(f.getBody(), true);
                    int count = 0;
                    while (ins.hasNext() && count < 2000) {
                        Instruction i = ins.next();
                        listing.println(i.getAddress() + "  " + i);
                        count++;
                    }
                    listing.println("END_FUNCTION instructions=" + count + "\n");
                }
            }
        }
        println("functions=" + emitted.size());
    }
}
