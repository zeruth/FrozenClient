// Exports every function in the program as one JSON line: address, name, whether the name is
// analyst-given or a default FUN_, size, callees, callers count, referenced string literals and
// referenced global data addresses. Feeds tools/recomp (the reference <-> whoa function map).
// Usage: -postScript ExportFunctions.java <outfile.jsonl>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class ExportFunctions extends GhidraScript {
    static String esc(String s) {
        StringBuilder b = new StringBuilder();
        for (char c : s.toCharArray()) {
            if (c == '"' || c == '\\') { b.append('\\').append(c); }
            else if (c == '\n') b.append("\\n");
            else if (c == '\r') b.append("\\r");
            else if (c == '\t') b.append("\\t");
            else if (c < 0x20 || c > 0x7e) b.append(String.format("\\u%04x", (int) c));
            else b.append(c);
        }
        return b.toString();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Listing listing = currentProgram.getListing();
        ReferenceManager refs = currentProgram.getReferenceManager();
        FunctionManager fm = currentProgram.getFunctionManager();
        int total = 0;
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            FunctionIterator it = fm.getFunctions(true);
            while (it.hasNext()) {
                Function f = it.next();
                if (monitor.isCancelled()) break;
                total++;
                Address entry = f.getEntryPoint();
                long size = f.getBody().getNumAddresses();
                Symbol sym = f.getSymbol();
                boolean named = sym != null && sym.getSource() != SourceType.DEFAULT;
                TreeSet<String> callees = new TreeSet<>();
                List<String> calls = new ArrayList<>();   // call sites in address order, repeats kept
                TreeSet<String> strings = new TreeSet<>();
                TreeSet<String> data = new TreeSet<>();
                int callSites = 0;
                InstructionIterator ii = listing.getInstructions(f.getBody(), true);
                while (ii.hasNext()) {
                    Instruction ins = ii.next();
                    for (Reference r : ins.getReferencesFrom()) {
                        Address to = r.getToAddress();
                        RefType t = r.getReferenceType();
                        if (t.isCall()) {
                            callSites++;
                            Function cf = fm.getFunctionAt(to);
                            String key = cf != null ? cf.getEntryPoint().toString() : "ext:" + to;
                            callees.add(key);
                            calls.add(key);
                        } else if (t.isData()) {
                            Data d = listing.getDataAt(to);
                            if (d != null && d.hasStringValue()) {
                                Object v = d.getValue();
                                if (v != null) {
                                    String s = v.toString();
                                    if (s.length() > 200) s = s.substring(0, 200);
                                    strings.add(s);
                                }
                            } else {
                                data.add(to.toString());
                            }
                        }
                    }
                }
                int callers = 0;
                ReferenceIterator ri = refs.getReferencesTo(entry);
                while (ri.hasNext()) { Reference r = ri.next(); if (r.getReferenceType().isCall()) callers++; }
                StringBuilder sb = new StringBuilder();
                sb.append("{\"addr\":\"").append(entry).append("\"");
                sb.append(",\"name\":\"").append(esc(f.getName(true))).append("\"");
                sb.append(",\"named\":").append(named);
                sb.append(",\"thunk\":").append(f.isThunk());
                sb.append(",\"size\":").append(size);
                sb.append(",\"callSites\":").append(callSites);
                sb.append(",\"callers\":").append(callers);
                sb.append(",\"callees\":[");
                boolean first = true;
                for (String c : callees) { if (!first) sb.append(','); first = false; sb.append('"').append(c).append('"'); }
                sb.append("],\"calls\":[");
                first = true;
                for (String c : calls) { if (!first) sb.append(','); first = false; sb.append('"').append(c).append('"'); }
                sb.append("],\"strings\":[");
                first = true;
                for (String s : strings) { if (!first) sb.append(','); first = false; sb.append('"').append(esc(s)).append('"'); }
                sb.append("],\"data\":[");
                first = true;
                int n = 0;
                for (String s : data) { if (n++ >= 64) break; if (!first) sb.append(','); first = false; sb.append('"').append(s).append('"'); }
                sb.append("]}");
                out.println(sb);
            }
        }
        println("ExportFunctions: wrote " + total + " functions to " + args[0]);
    }
}
