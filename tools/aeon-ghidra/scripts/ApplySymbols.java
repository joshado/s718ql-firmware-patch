// Ghidra headless script: apply a curated symbol table to the program so the
// exported listing/decompilation uses our names. Run AFTER NameFromStrings so
// curated names override the auto-inferred ones.
//
//   analyzeHeadless ... -postScript ApplySymbols.java /opt/aeon/symbols.tsv
//
// Table format (tab-separated; '#' comments and blank lines ignored):
//   addr <TAB> kind <TAB> name <TAB> comment(optional)
//   addr : hex RAM address (e.g. 0x00219504)
//   kind : func | data
//   name : C identifier to apply
//   comment : optional; plate comment (func) or EOL comment (data)
//@category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.io.*;

public class ApplySymbols extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String path = args.length > 0 ? args[0] : "/opt/aeon/symbols.tsv";
        File f = new File(path);
        if (!f.exists()) { println("[ApplySymbols] no table at " + path); return; }

        BufferedReader r = new BufferedReader(new FileReader(f));
        int nf = 0, nd = 0, nc = 0, skip = 0;
        String line;
        while ((line = r.readLine()) != null) {
            String t = line.trim();
            if (t.isEmpty() || t.startsWith("#")) continue;
            String[] c = t.split("\t");
            if (c.length < 3) { skip++; continue; }
            Address a;
            try { a = toAddr(Long.decode(c[0].trim())); } catch (Exception e) { skip++; continue; }
            String kind = c[1].trim();
            String name = c[2].trim();
            String comment = c.length > 3 ? c[3].trim() : null;
            try {
                if (kind.equals("func")) {
                    Function fn = getFunctionAt(a);
                    if (fn == null) {
                        if (getInstructionAt(a) == null) disassemble(a);
                        fn = createFunction(a, name);
                    }
                    if (fn != null) { fn.setName(name, SourceType.USER_DEFINED); nf++; }
                    else { createLabel(a, name, true); nd++; }
                    if (comment != null && !comment.isEmpty()) {
                        if (fn != null) fn.setComment(comment); else setPlateComment(a, comment);
                        nc++;
                    }
                } else {
                    createLabel(a, name, true);
                    nd++;
                    if (comment != null && !comment.isEmpty()) { setEOLComment(a, comment); nc++; }
                }
            } catch (Exception e) { skip++; }
        }
        r.close();
        println("[ApplySymbols] funcs=" + nf + " data=" + nd + " comments=" + nc + " skipped=" + skip);
    }
}
