// Bounded Ghidra evidence exporter for stripped PanaNav CI methods.
// @category Navigation

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class DumpCiStringFunctions extends GhidraScript {
    private static String clean(String s) {
        return s.replace('\t', ' ').replace('\r', ' ').replace('\n', ' ');
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) {
            throw new IllegalArgumentException("usage: OUTPUT_PREFIX STRING_SUBSTRING");
        }
        File summaryFile = new File(args[0] + ".tsv");
        File listingFile = new File(args[0] + ".listing.txt");
        if (summaryFile.exists() || listingFile.exists()) {
            throw new IllegalStateException("refusing to overwrite output");
        }

        Set<Address> emitted = new LinkedHashSet<>();
        try (BufferedWriter summary = new BufferedWriter(new FileWriter(summaryFile));
             BufferedWriter listing = new BufferedWriter(new FileWriter(listingFile))) {
            summary.write("string_address\tref_address\tfunction_entry\tfunction_name\tstring\n");
            DataIterator it = currentProgram.getListing().getDefinedData(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Data d = it.next();
                DataType dt = d.getDataType();
                if (dt == null || !dt.getName().toLowerCase().contains("string")) continue;
                Object value = d.getValue();
                if (!(value instanceof String)) continue;
                String text = (String)value;
                if (!text.contains(args[1])) continue;
                ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(d.getAddress());
                while (refs.hasNext()) {
                    Reference ref = refs.next();
                    Function f = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress());
                    if (f == null) continue;
                    summary.write(d.getAddress() + "\t" + ref.getFromAddress() + "\t" +
                        f.getEntryPoint() + "\t" + clean(f.getName(true)) + "\t" + clean(text) + "\n");
                    if (!emitted.add(f.getEntryPoint())) continue;
                    listing.write("FUNCTION " + f.getEntryPoint() + " " + clean(f.getName(true)) + "\n");
                    InstructionIterator ins = currentProgram.getListing().getInstructions(f.getBody(), true);
                    int count = 0;
                    while (ins.hasNext() && count < 2000) {
                        Instruction i = ins.next();
                        listing.write(i.getAddress() + "  " + clean(i.toString()) + "\n");
                        count++;
                    }
                    listing.write("END_FUNCTION instructions=" + count + "\n\n");
                }
            }
        }
        println("wrote " + summaryFile + " and " + listingFile + "; functions=" + emitted.size());
    }
}
