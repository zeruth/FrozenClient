// Exports every {name string, function pointer} pair found in data: the FrameScript / glue
// binding tables ({"GetVideoCaps", &Script_GetVideoCaps} ...) and any other name->function table
// laid out the same way. A string in .rdata that is pointed to from data, whose pointer's next
// dword is a function entry, is such a pair.
// Usage: -postScript ExportScriptTables.java <outfile.jsonl>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.*;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class ExportScriptTables extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Listing listing = currentProgram.getListing();
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refs = currentProgram.getReferenceManager();
        int n = 0;
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            DataIterator di = listing.getDefinedData(true);
            while (di.hasNext()) {
                Data d = di.next();
                if (monitor.isCancelled()) break;
                if (!d.hasStringValue()) continue;
                Object v = d.getValue();
                if (v == null) continue;
                String name = v.toString();
                if (name.length() < 2 || name.length() > 64 || !name.matches("[A-Za-z_][A-Za-z0-9_]*")) continue;
                ReferenceIterator ri = refs.getReferencesTo(d.getAddress());
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Address from = r.getFromAddress();
                    if (listing.getInstructionAt(from) != null) continue; // code ref, not a table
                    try {
                        long fp = mem.getInt(from.add(4)) & 0xffffffffL;
                        Address fa = from.getNewAddress(fp);
                        Function f = fm.getFunctionAt(fa);
                        if (f == null) continue;
                        out.println("{\"name\":\"" + name + "\",\"fn\":\"" + fa + "\",\"table\":\"" + from + "\"}");
                        n++;
                    } catch (Exception e) {
                        // off the end of a block
                    }
                }
            }
        }
        println("ExportScriptTables: wrote " + n + " pairs to " + args[0]);
    }
}
