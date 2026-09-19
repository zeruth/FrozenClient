// Recovers a global struct's layout from how the code touches it: for every address in
// [base, base+size) it emits one JSON line per reference with the field offset, the referencing
// function, the site, the reference type and the instruction text (which carries the access width
// and whether it is a read or a write). Sort by offset to read the layout off.
// Usage: -postScript ExportStructRefs.java <outfile.jsonl> <baseAddr> <size>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class ExportStructRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        long base = Long.parseLong(args[1].replace("0x", ""), 16);
        long size = Long.parseLong(args[2].replace("0x", ""), 16);
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        int n = 0;
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            for (long off = 0; off < size; off++) {
                Address a = toAddr(base + off);
                ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(a);
                while (ri.hasNext()) {
                    Reference r = ri.next();
                    Address site = r.getFromAddress();
                    Function f = fm.getFunctionContaining(site);
                    Instruction ins = listing.getInstructionAt(site);
                    String text = ins == null ? "" : ins.toString().replace("\\", "\\\\").replace("\"", "\\\"");
                    out.println("{\"offset\":" + off + ",\"addr\":\"" + a + "\",\"fn\":\"" + (f == null ? "?" : f.getEntryPoint()) + "\",\"fnName\":\"" + (f == null ? "?" : f.getName()) + "\",\"site\":\"" + site + "\",\"type\":\"" + r.getReferenceType() + "\",\"insn\":\"" + text + "\"}");
                    n++;
                }
            }
        }
        println("ExportStructRefs: " + n + " references into " + args[1] + "+" + args[2] + " -> " + args[0]);
    }
}
