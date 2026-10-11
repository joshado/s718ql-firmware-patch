// Ghidra headless script: recover AEON pointer references that default analysis
// misses. The AEON core builds addresses with b.movhi + b.addi/b.ori (and
// loads/stores off such a base); Ghidra has no AEON constant-reference analyzer,
// so those pointers never become references. We run SymbolicPropogator over each
// function (it understands the movhi/add pcode) which creates the references,
// then define an ASCII string at any new target that is printable text.
//
// Run this BEFORE NameFromStrings.java.
//@category Analysis
import ghidra.app.plugin.core.analysis.ConstantPropagationContextEvaluator;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataType;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.util.SymbolicPropogator;
import ghidra.app.script.GhidraScript;

public class AeonResolvePointers extends GhidraScript {
    public void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        int funcs = 0;
        for (Function f : fm.getFunctions(true)) {
            if (monitor.isCancelled()) break;
            SymbolicPropogator sp = new SymbolicPropogator(currentProgram);
            ConstantPropagationContextEvaluator eval =
                    new ConstantPropagationContextEvaluator(monitor, true);
            sp.flowConstants(f.getEntryPoint(), f.getBody(), eval, true, monitor);
            funcs++;
        }

        // Define ASCII strings at data reference targets that look like text.
        ReferenceManager rm = currentProgram.getReferenceManager();
        int made = 0;
        ReferenceIterator ri = rm.getReferenceIterator(currentProgram.getMinAddress());
        java.util.HashSet<Address> seen = new java.util.HashSet<>();
        while (ri.hasNext()) {
            if (monitor.isCancelled()) break;
            Reference r = ri.next();
            if (!r.getReferenceType().isData()) continue;
            Address to = r.getToAddress();
            if (to == null || !to.isMemoryAddress() || !seen.add(to)) continue;
            if (getDataAt(to) != null) continue;
            if (!printableRun(to)) continue;
            try { createData(to, new StringDataType()); made++; } catch (Exception e) { /* skip */ }
        }
        println("[AeonResolvePointers] flowed " + funcs + " functions; defined " + made + " new strings");
    }

    boolean printableRun(Address a) {
        try {
            int n = 0;
            for (int i = 0; i < 6; i++) {
                int b = getByte(a.add(i)) & 0xff;
                if (b == 0) break;
                if (b < 0x20 || b > 0x7e) return false;
                n++;
            }
            return n >= 4;
        } catch (Exception e) { return false; }
    }
}
