// Ghidra headless post-script: dump the whole disassembly listing to a text
// file and print a short summary. Java (no PyGhidra / decompiler needed).
//
//   analyzeHeadless ... -postScript ExportListing.java /out/listing.asm
//
// Each line: ADDR  BYTES  MNEMONIC OPERANDS   (with "; ===== FUNC" headers).
//@category Export
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.HashMap;

public class ExportListing extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = (args.length > 0) ? args[0] : "/out/listing.asm";

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        HashMap<Address, String> funcAt = new HashMap<>();
        for (Function f : fm.getFunctions(true)) {
            funcAt.put(f.getEntryPoint(), f.getName());
        }

        int n = 0;
        PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(outPath)));
        try {
            InstructionIterator it = listing.getInstructions(true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                Address a = ins.getAddress();
                if (funcAt.containsKey(a)) {
                    w.printf("%n; ===== FUNC %s  %s%n", a, funcAt.get(a));
                }
                StringBuilder raw = new StringBuilder();
                for (byte b : ins.getBytes()) {
                    raw.append(String.format("%02x", b & 0xff));
                }
                w.printf("%s  %-12s %s%n", a, raw, ins.toString());
                n++;
            }
        } finally {
            w.close();
        }
        println("[ExportListing] instructions=" + n
                + " functions=" + fm.getFunctionCount() + " -> " + outPath);
    }
}
