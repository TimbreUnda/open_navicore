// Dump QR39.6 callback-facing IF_Server*Proxy vtables without crossing them.
// The abstract application tail is a contiguous __cxa_pure_virtual run.
// @category NaviCore

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import java.io.PrintWriter;

public class DumpServerProxyVtables extends GhidraScript {
    private String functionName(Address target) {
        Function function = getFunctionAt(target);
        if (function == null) function = getFunctionContaining(target);
        return function == null ? "" : function.getName(true).replace(',', ';');
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("usage: output-csv");
        PrintWriter out = new PrintWriter(args[0]);
        out.println("proxy,vtable,address_point,slot,entry,target,function");
        SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
        int proxies = 0;
        int entries = 0;
        while (symbols.hasNext()) {
            monitor.checkCancelled();
            Symbol symbol = symbols.next();
            String name = symbol.getName(true);
            if (!name.contains("IF_Server") || !name.endsWith("Proxy::vtable")) continue;
            Address addressPoint = symbol.getAddress().add(8);
            Address applicationTarget = null;
            for (int slot = 0; slot < 64; slot++) {
                Address entry = addressPoint.add(slot * 4L);
                long raw = Integer.toUnsignedLong(getInt(entry));
                Address target = toAddr(raw & ~1L);
                String targetName = functionName(target);
                if (slot == 2) {
                    if (!targetName.contains("__cxa_pure_virtual")) {
                        throw new IllegalStateException(name + " slot 2 is not pure virtual");
                    }
                    applicationTarget = target;
                } else if (slot > 2 && !target.equals(applicationTarget)) {
                    // This exact check rejects three old false tail rows that all
                    // pointed at std::ios_base::Init::~Init in adjacent data.
                    break;
                }
                out.printf("%s,%s,%s,%d,%s,%s,%s%n",
                    name.replace(',', ';'), symbol.getAddress(), addressPoint,
                    slot, entry, target, targetName);
                entries++;
            }
            proxies++;
        }
        out.close();
        if (proxies != 25 || entries != 249) {
            throw new IllegalStateException(
                "expected 25 proxy vtables / 249 entries, got " + proxies + "/" + entries);
        }
    }
}
