// For every call site of a target function, recovers the constant arguments pushed right before
// the call (x86 cdecl/stdcall: PUSH imm / PUSH addr, last argument pushed first) and emits one
// JSON line per site: {"caller","site","args":[...]} with args in source order. Immediates that
// are function entry points come out as addresses, which is what links handler tables
// (SetMessageHandler(opcode, handler)) and registration lists (CVar::Register(name, ...)).
// Usage: -postScript ExportCallArgs.java <outfile.jsonl> <targetAddr> <nargs>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

public class ExportCallArgs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Address target = toAddr(Long.parseLong(args[1].replace("0x", ""), 16));
        int nargs = Integer.parseInt(args[2]);
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        int sites = 0;
        try (PrintWriter out = new PrintWriter(new FileWriter(args[0]))) {
            ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(target);
            while (ri.hasNext()) {
                Reference r = ri.next();
                if (!r.getReferenceType().isCall()) continue;
                Address site = r.getFromAddress();
                Function caller = fm.getFunctionContaining(site);
                List<String> found = new ArrayList<>();
                Instruction ins = listing.getInstructionAt(site);
                int steps = 0;
                while (ins != null && found.size() < nargs && steps++ < 40) {
                    ins = ins.getPrevious();
                    if (ins == null) break;
                    String mn = ins.getMnemonicString();
                    if (mn.equals("CALL") || mn.equals("RET") || mn.startsWith("J")) break;
                    if (!mn.equals("PUSH")) continue;
                    Object[] ops = ins.getOpObjects(0);
                    String v = "?";
                    if (ops.length == 1 && ops[0] instanceof Scalar) {
                        long s = ((Scalar) ops[0]).getUnsignedValue();
                        Address a = toAddr(s);
                        if (fm.getFunctionAt(a) != null) v = "fn:" + a;
                        else {
                            Data d = listing.getDataAt(a);
                            if (d != null && d.hasStringValue() && d.getValue() != null) v = "str:" + d.getValue().toString().replace("\\", "\\\\").replace("\"", "\\\"");
                            else v = "0x" + Long.toHexString(s);
                        }
                    } else if (ops.length == 1 && ops[0] instanceof Address) {
                        Address a = (Address) ops[0];
                        v = fm.getFunctionAt(a) != null ? "fn:" + a : "addr:" + a;
                    }
                    found.add(v);
                }
                // walking back from the call meets the first argument first (it is pushed last),
                // so `found` is already in source order
                StringBuilder sb = new StringBuilder();
                sb.append("{\"caller\":\"").append(caller == null ? "?" : caller.getEntryPoint()).append("\",\"site\":\"").append(site).append("\",\"args\":[");
                for (int i = 0; i < found.size(); i++) { if (i > 0) sb.append(','); sb.append('"').append(found.get(i)).append('"'); }
                sb.append("]}");
                out.println(sb);
                sites++;
            }
        }
        println("ExportCallArgs: " + sites + " call sites of " + target + " -> " + args[0]);
    }
}
