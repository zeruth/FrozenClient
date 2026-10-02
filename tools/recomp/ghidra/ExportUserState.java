// Export everything a person or script added to a Ghidra program on top of its auto-analysis --
// non-default symbols, function names, signatures and calling conventions, plate/EOL comments --
// as a JSON-lines file, so a re-import of a byte-identical binary can have it re-applied by
// ImportUserState.java. Usage (headless, -noanalysis): -postScript ExportUserState.java <out.jsonl>
//@category Recomp

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.io.*;

public class ExportUserState extends GhidraScript {
    private static String esc(String s) {
        if (s == null) return "";
        StringBuilder b = new StringBuilder();
        for (char c : s.toCharArray()) {
            if (c == '"' || c == (char) 92) { b.append((char) 92).append(c); }
            else if (c < 0x20) { b.append(String.format("%su%04x", String.valueOf((char) 92), (int) c)); }
            else b.append(c);
        }
        return b.toString();
    }

    @Override
    public void run() throws Exception {
        String out = getScriptArgs()[0];
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(out), "UTF-8"));
        int syms = 0, funcs = 0, comments = 0;

        SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Symbol s = it.next();
            SourceType src = s.getSource();
            if (src == SourceType.DEFAULT || src == SourceType.ANALYSIS) continue;
            if (s.isExternal()) continue;
            w.printf("{\"kind\":\"symbol\",\"addr\":\"%s\",\"name\":\"%s\",\"ns\":\"%s\",\"type\":\"%s\",\"source\":\"%s\",\"primary\":%s}%n",
                s.getAddress(), esc(s.getName()), esc(s.getParentNamespace().getName(true)),
                s.getSymbolType(), src, s.isPrimary());
            syms++;
        }

        FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(true);
        while (fit.hasNext() && !monitor.isCancelled()) {
            Function f = fit.next();
            if (f.getSignatureSource() == SourceType.DEFAULT && f.getSymbol().getSource() == SourceType.DEFAULT) continue;
            w.printf("{\"kind\":\"function\",\"addr\":\"%s\",\"name\":\"%s\",\"signature\":\"%s\",\"convention\":\"%s\",\"sigsource\":\"%s\"}%n",
                f.getEntryPoint(), esc(f.getName(true)), esc(f.getPrototypeString(true, true)),
                esc(f.getCallingConventionName()), f.getSignatureSource());
            funcs++;
        }

        int[] types = { CodeUnit.PLATE_COMMENT, CodeUnit.PRE_COMMENT, CodeUnit.EOL_COMMENT, CodeUnit.POST_COMMENT, CodeUnit.REPEATABLE_COMMENT };
        for (int t : types) {
            AddressIterator ai = currentProgram.getListing().getCommentAddressIterator(t, currentProgram.getMemory(), true);
            while (ai.hasNext() && !monitor.isCancelled()) {
                Address a = ai.next();
                String c = currentProgram.getListing().getComment(t, a);
                w.printf("{\"kind\":\"comment\",\"addr\":\"%s\",\"type\":%d,\"text\":\"%s\"}%n", a, t, esc(c));
                comments++;
            }
        }

        w.close();
        println(String.format("ExportUserState: %d symbols, %d functions, %d comments -> %s", syms, funcs, comments, out));
    }
}
