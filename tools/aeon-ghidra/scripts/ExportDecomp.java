// Ghidra headless post-script: decompile functions to C and write them to a
// file. Needs a working decompiler native (run the image under linux/amd64 —
// the arm64 Ghidra ships no decompiler).
//
//   analyzeHeadless ... -postScript ExportDecomp.java /out/decomp.c [START [END]]
//
// START/END (hex, optional) limit decompilation to functions whose entry is in
// [START,END); omit to do all. Under emulation, decompiling everything is slow,
// so a range is usually what you want.
//@category Export
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = (args.length > 0) ? args[0] : "/out/decomp.c";
        Long lo = (args.length > 1) ? Long.decode(args[1]) : null;
        Long hi = (args.length > 2) ? Long.decode(args[2]) : null;

        DecompInterface d = new DecompInterface();
        if (!d.openProgram(currentProgram)) {
            println("[ExportDecomp] decompiler failed to open: " + d.getLastMessage());
            return;
        }

        int done = 0, failed = 0;
        PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(outPath)));
        try {
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext()) {
                if (monitor.isCancelled()) break;
                Function f = it.next();
                long ep = f.getEntryPoint().getOffset();
                if (lo != null && ep < lo) continue;
                if (hi != null && ep >= hi) continue;
                DecompileResults r = d.decompileFunction(f, 60, monitor);
                if (r.decompileCompleted() && r.getDecompiledFunction() != null) {
                    w.println("/* " + f.getEntryPoint() + "  " + f.getName() + " */");
                    w.println(r.getDecompiledFunction().getC());
                    w.println();
                    done++;
                } else {
                    w.println("/* " + f.getEntryPoint() + "  " + f.getName()
                              + "  DECOMPILE FAILED: " + r.getErrorMessage() + " */\n");
                    failed++;
                }
            }
        } finally {
            w.close();
            d.dispose();
        }
        println("[ExportDecomp] decompiled=" + done + " failed=" + failed + " -> " + outPath);
    }
}
