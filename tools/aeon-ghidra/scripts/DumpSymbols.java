// Ghidra headless script: dump all user-defined function and label names to a
// symbols.tsv-format file, so auto-discovered (NameFromStrings) names and any
// manual edits can be captured and merged back into the curated table.
//
//   analyzeHeadless ... -postScript DumpSymbols.java /out/symbols_dump.tsv
//@category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.*;
import java.io.*;

public class DumpSymbols extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String out = args.length > 0 ? args[0] : "/out/symbols_dump.tsv";
        PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)));
        w.println("# addr\tkind\tname\tcomment");
        int nf = 0, nd = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f.getSymbol().getSource() == SourceType.USER_DEFINED) {
                String cm = f.getComment();
                w.printf("0x%08x\tfunc\t%s\t%s%n", f.getEntryPoint().getOffset(),
                         f.getName(), cm == null ? "" : cm.replace('\t', ' ').replace('\n', ' '));
                nf++;
            }
        }
        SymbolTable st = currentProgram.getSymbolTable();
        for (Symbol s : st.getAllSymbols(false)) {
            if (s.getSource() == SourceType.USER_DEFINED && s.getSymbolType() == SymbolType.LABEL) {
                w.printf("0x%08x\tdata\t%s\t%n", s.getAddress().getOffset(), s.getName());
                nd++;
            }
        }
        w.close();
        println("[DumpSymbols] funcs=" + nf + " data=" + nd + " -> " + out);
    }
}
