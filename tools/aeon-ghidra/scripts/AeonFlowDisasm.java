// Ghidra headless script: flow-based AEON code discovery.
//
// Replaces the blind linear sweep (DisassembleCodeRegion). That swept every
// byte as code, giving coverage but junk in data regions and unreliable
// function boundaries. This instead discovers functions the way they really
// start, so boundaries come from flow, not guesswork:
//   1. prologue scan: find AEON entries `b.addi r1,r1,-N` (N>0, word) followed
//      shortly by `b.sw D(r1),r9` (save return reg) -> disassemble + make a
//      function there (checked with the pseudo-disassembler, no commit on miss);
//   2. call-target closure: make functions at every call reference target;
//   3. iterate until stable. Ghidra follows intra-function flow for each.
//
// Run INSTEAD of DisassembleCodeRegion. Default range is the code region.
//   analyzeHeadless ... -postScript AeonFlowDisasm.java [START END]
//@category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.util.PseudoDisassembler;
import ghidra.app.util.PseudoInstruction;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;

public class AeonFlowDisasm extends GhidraScript {
    PseudoDisassembler pdis;

    public void run() throws Exception {
        String[] a = getScriptArgs();
        long lo = a.length > 0 ? Long.decode(a[0]) : 0x180000L;
        long hi = a.length > 1 ? Long.decode(a[1]) : 0x230000L;
        Address loA = toAddr(lo), hiA = toAddr(hi);
        pdis = new PseudoDisassembler(currentProgram);
        Listing listing = currentProgram.getListing();

        int before = currentProgram.getFunctionManager().getFunctionCount();

        // Phase 1: prologue scan. Step every 2 bytes (prologues are 2-byte
        // aligned) and make a function at each prologue not already one -- even
        // inside already-disassembled-but-not-functionised code, which is where
        // most undiscovered functions sit.
        int proCreated = 0;
        for (Address p = loA; p.compareTo(hiA) < 0; p = p.add(2)) {
            if (monitor.isCancelled()) break;
            if (getFunctionAt(p) != null) continue;
            if (listing.getDefinedDataAt(p) != null) continue;
            if (!isPrologue(p)) continue;
            if (listing.getInstructionAt(p) == null)
                new DisassembleCommand(p, null, true).applyTo(currentProgram, monitor);
            if (new CreateFunctionCmd(p).applyTo(currentProgram, monitor)) proCreated++;
        }

        // Phase 2/3: close over call targets until stable.
        int callCreated = 0, rounds = 0;
        boolean changed = true;
        while (changed && rounds < 8) {
            if (monitor.isCancelled()) break;
            changed = false; rounds++;
            InstructionIterator it = listing.getInstructions(loA, true);
            while (it.hasNext()) {
                Instruction in = it.next();
                if (in.getAddress().compareTo(hiA) >= 0) break;
                for (Reference r : in.getReferencesFrom()) {
                    if (!r.getReferenceType().isCall()) continue;
                    Address t = r.getToAddress();
                    if (t == null || getFunctionAt(t) != null) continue;
                    if (listing.getInstructionAt(t) == null)
                        new DisassembleCommand(t, null, true).applyTo(currentProgram, monitor);
                    if (listing.getInstructionAt(t) != null
                            && new CreateFunctionCmd(t).applyTo(currentProgram, monitor)) {
                        callCreated++; changed = true;
                    }
                }
            }
        }

        int after = currentProgram.getFunctionManager().getFunctionCount();
        println("[AeonFlowDisasm] functions " + before + " -> " + after
                + " (prologue " + proCreated + ", call-target " + callCreated
                + ", rounds " + rounds + ")");
    }

    // b.addi r1,r1,-N  (N>0, word-aligned) followed within 6 insns by
    // b.sw D(r1),r9  before any branch/return -> a real function entry.
    boolean isPrologue(Address a) {
        PseudoInstruction p0 = tryDecode(a);
        if (p0 == null || !p0.getMnemonicString().equals("b.addi")) return false;
        if (!"r1".equals(reg(p0, 0)) || !"r1".equals(reg(p0, 1))) return false;
        long n = scalar(p0, 2);
        if (n >= 0 || (n & 3) != 0) return false;             // stack grows down, word frame
        Address at = a.add(p0.getLength());
        for (int k = 0; k < 6; k++) {
            PseudoInstruction q = tryDecode(at);
            if (q == null) return false;
            String m = q.getMnemonicString();
            if (m.equals("b.sw") && "r9".equals(lastReg(q)) && baseIsR1(q)) return true;
            if (m.startsWith("b.j") || m.equals("b.bf") || m.equals("b.bnf") || m.startsWith("b.b")
                    || m.equals("b.jr") || m.equals("b.jal")) return false;
            at = at.add(q.getLength());
        }
        return false;
    }

    PseudoInstruction tryDecode(Address a) {
        try { return pdis.disassemble(a); } catch (Exception e) { return null; }
    }
    String reg(PseudoInstruction in, int i) {
        if (i >= in.getNumOperands()) return null;
        for (Object o : in.getOpObjects(i)) if (o instanceof Register) return ((Register) o).getName();
        return null;
    }
    String lastReg(PseudoInstruction in) {           // source reg of a store (last operand)
        return reg(in, in.getNumOperands() - 1);
    }
    boolean baseIsR1(PseudoInstruction in) {
        // AEON renders a store as separate operands [disp, base, src] (none
        // "dynamic"), so just look for r1 among the operands.
        for (int i = 0; i < in.getNumOperands(); i++)
            for (Object o : in.getOpObjects(i))
                if (o instanceof Register && ((Register) o).getName().equals("r1")) return true;
        return false;
    }
    long scalar(PseudoInstruction in, int i) {
        if (i >= in.getNumOperands()) return 0;
        for (Object o : in.getOpObjects(i)) if (o instanceof Scalar) return ((Scalar) o).getSignedValue();
        return 0;
    }
}
