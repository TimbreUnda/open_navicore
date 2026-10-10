// Create missing Thumb functions at supplied entry addresses.
// @category VP4R
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class CreateThumbFunctions extends GhidraScript {
    public void run() throws Exception {
        for (String argument : getScriptArgs()) {
            long raw = Long.decode(argument);
            Address entry = toAddr(raw & ~1L);
            if (getFunctionAt(entry) == null) createFunction(entry, null);
            println(entry + " " + getFunctionAt(entry));
        }
    }
}
