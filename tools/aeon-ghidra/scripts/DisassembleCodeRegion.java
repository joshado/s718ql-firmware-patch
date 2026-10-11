// Ghidra headless script: exhaustively disassemble the AEON code region.
// Default analysis follows only entry-reachable flow and leaves ~65% of the
// code undisassembled. This fills every gap: at each still-undefined address it
// disassembles (following flow), then advances past what got defined. Then it
// creates functions at call targets so the decompiler/naming see them.
//
//   analyzeHeadless ... -postScript DisassembleCodeRegion.java [START END]
// START/END hex; default 0x180000 0x230000 (the scaler AP code region).
//@category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class DisassembleCodeRegion extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        long start = a.length > 0 ? Long.decode(a[0]) : 0x180000L;
        long end   = a.length > 1 ? Long.decode(a[1]) : 0x230000L;
        Address endA = toAddr(end);
        Listing listing = currentProgram.getListing();

        int before = countInstr();
        Address cur = toAddr(start);
        int gaps = 0;
        while (cur != null && cur.compareTo(endA) < 0) {
            if (monitor.isCancelled()) break;
            Instruction ins = listing.getInstructionAt(cur);
            if (ins != null) { cur = ins.getMaxAddress().add(1); continue; }
            // undefined (or data we overwrite): disassemble here, following flow
            DisassembleCommand cmd = new DisassembleCommand(cur, null, true);
            cmd.applyTo(currentProgram, monitor);
            gaps++;
            ins = listing.getInstructionAt(cur);
            cur = (ins != null) ? ins.getMaxAddress().add(1) : cur.add(1);
        }
        int after = countInstr();
        println("[DisassembleCodeRegion] instructions " + before + " -> " + after
                + " (+"+(after-before)+"), gap-starts " + gaps);

        // Create functions at the destinations of call instructions (b.jal).
        int made = 0;
        InstructionIterator it = listing.getInstructions(toAddr(start), true);
        while (it.hasNext()) {
            Instruction in = it.next();
            if (in.getAddress().compareTo(endA) >= 0) break;
            for (Reference r : in.getReferencesFrom()) {
                if (r.getReferenceType().isCall()) {
                    Address t = r.getToAddress();
                    if (getFunctionAt(t) == null && listing.getInstructionAt(t) != null) {
                        CreateFunctionCmd fc = new CreateFunctionCmd(t);
                        if (fc.applyTo(currentProgram, monitor)) made++;
                    }
                }
            }
        }
        println("[DisassembleCodeRegion] created " + made + " functions at call targets; total "
                + currentProgram.getFunctionManager().getFunctionCount());
    }

    int countInstr() {
        int n = 0;
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) { it.next(); n++; }
        return n;
    }
}
