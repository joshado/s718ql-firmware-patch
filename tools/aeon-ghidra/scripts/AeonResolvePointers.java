// Ghidra headless script: recover AEON pointer references that default analysis
// misses. The AEON core builds addresses with b.movhi + b.addi/b.ori (and
// loads/stores off such a base); Ghidra has no AEON constant-reference analyzer.
// We run SymbolicPropogator over each function with an evaluator that CREATES a
// reference whenever a computed constant points into memory (the default
// evaluator computes but makes no references — which is why a plain run did
// nothing). Then define an ASCII string at any new target that is text.
//
// Run this BEFORE NameFromStrings.java / DefineStrings.java.
//@category Analysis
import ghidra.app.plugin.core.analysis.ConstantPropagationContextEvaluator;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.StringDataType;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.util.SymbolicPropogator;
import ghidra.program.util.VarnodeContext;
import ghidra.app.script.GhidraScript;

public class AeonResolvePointers extends GhidraScript {
    public void run() throws Exception {
        final Address min = currentProgram.getMinAddress();
        final Address max = currentProgram.getMaxAddress();
        FunctionManager fm = currentProgram.getFunctionManager();
        final int[] refsMade = {0};

        ConstantPropagationContextEvaluator eval =
            new ConstantPropagationContextEvaluator(monitor, true) {
                @Override
                public boolean evaluateReference(VarnodeContext ctx, Instruction instr,
                        int pcodeop, Address addr, int size, DataType dt, RefType rt) {
                    // Load/store/branch targets inside the image.
                    if (addr != null && addr.isMemoryAddress()
                            && addr.compareTo(min) >= 0 && addr.compareTo(max) <= 0) {
                        refsMade[0]++;
                        return true;
                    }
                    return false;
                }
                @Override
                public Address evaluateConstant(VarnodeContext ctx, Instruction instr,
                        int pcodeop, Address constant, int size, DataType dt, RefType rt) {
                    // A computed constant that points into the DATA region (above the
                    // code, >=0x209c79) is almost always a string/data pointer being
                    // set up as a call argument. Create a reference to it.
                    if (constant != null && constant.isMemoryAddress()
                            && constant.getOffset() >= 0x209c79L
                            && constant.compareTo(max) <= 0) {
                        refsMade[0]++;
                        return constant;   // non-null -> reference created from instr
                    }
                    return null;
                }
            };

        int funcs = 0;
        for (Function f : fm.getFunctions(true)) {
            if (monitor.isCancelled()) break;
            SymbolicPropogator sp = new SymbolicPropogator(currentProgram);
            sp.flowConstants(f.getEntryPoint(), f.getBody(), eval, true, monitor);
            funcs++;
        }

        // Define ASCII strings at data reference targets that look like text.
        ReferenceManager rm = currentProgram.getReferenceManager();
        int made = 0;
        ReferenceIterator ri = rm.getReferenceIterator(min);
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
        println("[AeonResolvePointers] flowed " + funcs + " functions; references~" + refsMade[0]
                + "; defined " + made + " new strings");
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
