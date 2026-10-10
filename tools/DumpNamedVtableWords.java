// Dump bounded raw words and resolved functions for selected Ghidra vtable symbols.
// @category Navigation

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

import java.io.PrintWriter;

public class DumpNamedVtableWords extends GhidraScript {
    private static String clean(String s) {
        return s.replace(',', ';').replace('\r', ' ').replace('\n', ' ');
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) {
            throw new IllegalArgumentException("usage: OUTPUT_CSV SYMBOL_SUBSTRING WORD_COUNT");
        }
        int wordCount = Integer.parseInt(args[2]);
        if (wordCount < 1 || wordCount > 512) throw new IllegalArgumentException("bad word count");
        try (PrintWriter out = new PrintWriter(args[0])) {
            out.println("symbol,symbol_address,index,entry_address,raw,target,function,is_code");
            SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
            int matched = 0;
            Memory memory = currentProgram.getMemory();
            while (symbols.hasNext()) {
                monitor.checkCancelled();
                Symbol symbol = symbols.next();
                String name = symbol.getName(true);
                if (!name.contains(args[1]) || !name.endsWith("::vtable")) continue;
                matched++;
                for (int i = 0; i < wordCount; i++) {
                    Address entry = symbol.getAddress().add(4L * i);
                    if (!memory.contains(entry)) break;
                    long raw = Integer.toUnsignedLong(getInt(entry));
                    Address target = toAddr(raw & ~1L);
                    Function function = getFunctionAt(target);
                    if (function == null) function = getFunctionContaining(target);
                    boolean code = memory.contains(target) && memory.getBlock(target).isExecute();
                    out.printf("%s,%s,%d,%s,0x%08x,%s,%s,%s%n",
                        clean(name), symbol.getAddress(), i, entry, raw, target,
                        function == null ? "" : clean(function.getName(true)), code);
                }
            }
            if (matched == 0) throw new IllegalStateException("no matching vtable symbol");
            println("matched vtables=" + matched);
        }
    }
}
