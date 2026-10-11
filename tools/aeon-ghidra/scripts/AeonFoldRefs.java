// Ghidra headless script: AEON constant-pointer reference pass.
//
// The AEON core builds addresses as movhi+addi / movhi+ori (and loads/stores
// off such a base). Ghidra's generic constant propagation only recovers a small
// fraction for this core, so most string/data pointers never become references
// and the decompiler can't show them. This does a per-function linear fold of
// movhi/movi/ori/addi/mov and load/store effective addresses, and CREATES a
// data reference (and defines an ASCII string) for every resulting pointer into
// the image's data region. Measured: ~359 functions gain string references vs
// ~48 from the default analysis.
//
// Run BEFORE NameFromStrings.java (and it subsumes AeonResolvePointers for our
// purposes). Default range is the whole program.
//@category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataType;
import ghidra.program.model.lang.OperandType;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;
import java.util.HashMap;
import java.util.HashSet;

public class AeonFoldRefs extends GhidraScript {
    static final long IMG_LO = 0x180000L, IMG_HI = 0x302000L;  // loaded image + BSS
    static final long DATA_LO = 0x209c80L;                     // above the main code

    public void run() throws Exception {
        ReferenceManager rm = currentProgram.getReferenceManager();
        HashSet<Long> strTargets = new HashSet<>();
        int refs = 0;

        // Iterate ALL instructions (the sweep leaves many outside function bodies),
        // resetting the tracked registers at each function entry.
        HashMap<String, Long> reg = new HashMap<>();
        reg.put("r0", 0L);
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction in = it.next();
            if (monitor.isCancelled()) break;
            if (getFunctionAt(in.getAddress()) != null) { reg.clear(); reg.put("r0", 0L); }
            {
                String mn = in.getMnemonicString();
                try {
                    if (mn.equals("b.movhi")) {
                        put(reg, destReg(in), (scalar(in, 1) & 0xffffL) << 16);
                    } else if (mn.equals("b.movi")) {
                        put(reg, destReg(in), scalar(in, 1));            // signed small const
                    } else if (mn.equals("b.mov")) {
                        Long v = reg.get(srcReg(in, 1));
                        if (v != null) put(reg, destReg(in), v); else reg.remove(destReg(in));
                    } else if (mn.equals("b.addi")) {
                        Long base = reg.get(srcReg(in, 1));
                        if (base != null) {
                            long v = base + scalar(in, 2);
                            put(reg, destReg(in), v);
                            if (mkRef(rm, in, v)) { refs++; noteStr(strTargets, v); }
                        } else reg.remove(destReg(in));
                    } else if (mn.equals("b.ori")) {
                        String s = srcReg(in, 1);
                        Long base = reg.get(s);
                        if (base != null) {
                            long v = base | (scalar(in, 2) & 0xffffL);
                            put(reg, destReg(in), v);
                            if (mkRef(rm, in, v)) { refs++; noteStr(strTargets, v); }
                        } else reg.remove(destReg(in));
                    } else if (isMem(mn)) {
                        // effective address = base_reg + disp from the dynamic operand
                        for (int i = 0; i < in.getNumOperands(); i++) {
                            if (!OperandType.isDynamic(in.getOperandType(i))) continue;
                            Register b = null; long disp = 0;
                            for (Object o : in.getOpObjects(i)) {
                                if (o instanceof Register) b = (Register) o;
                                else if (o instanceof Scalar) disp = ((Scalar) o).getSignedValue();
                            }
                            if (b != null && reg.get(b.getName()) != null) {
                                long v = reg.get(b.getName()) + disp;
                                if (mkRef(rm, in, v)) { refs++; noteStr(strTargets, v); }
                            }
                        }
                        if (isLoad(mn)) reg.remove(destReg(in));   // loaded value unknown
                    } else {
                        String d = destReg(in);                    // any other write clears dest
                        if (d != null && !d.equals("r0")) reg.remove(d);
                    }
                } catch (Exception e) { /* skip malformed */ }
            }
        }

        // Define ASCII strings at the printable targets (so the decompiler shows them).
        int made = 0;
        for (Long t : strTargets) {
            Address a = toAddr(t);
            if (getDataAt(a) != null) continue;
            if (!printable(a)) continue;
            try { createData(a, new StringDataType()); made++; } catch (Exception e) { }
        }
        println("[AeonFoldRefs] references=" + refs + " string-targets=" + strTargets.size()
                + " strings-defined=" + made);
    }

    boolean mkRef(ReferenceManager rm, Instruction in, long v) {
        if (v < IMG_LO || v >= IMG_HI) return false;
        rm.addMemoryReference(in.getAddress(), toAddr(v), RefType.DATA, SourceType.ANALYSIS, 0);
        return true;
    }
    void noteStr(HashSet<Long> s, long v) { if (v >= DATA_LO && v < IMG_HI) s.add(v); }

    void put(HashMap<String, Long> reg, String r, long v) { if (r != null) reg.put(r, v & 0xffffffffL); }
    String destReg(Instruction in) { return regAt(in, 0); }
    String srcReg(Instruction in, int i) { return regAt(in, i); }
    String regAt(Instruction in, int i) {
        if (i >= in.getNumOperands()) return null;
        for (Object o : in.getOpObjects(i)) if (o instanceof Register) return ((Register) o).getName();
        return null;
    }
    long scalar(Instruction in, int i) {
        for (Object o : in.getOpObjects(i)) if (o instanceof Scalar) return ((Scalar) o).getSignedValue();
        return 0;
    }
    boolean isMem(String mn) { return isLoad(mn) || mn.equals("b.sb") || mn.equals("b.sh") || mn.equals("b.sw"); }
    boolean isLoad(String mn) {
        return mn.equals("b.lbz") || mn.equals("b.lbs") || mn.equals("b.lhz")
            || mn.equals("b.lhs") || mn.equals("b.lwz") || mn.equals("b.lws");
    }
    boolean printable(Address a) {
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
