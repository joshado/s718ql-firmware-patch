// Ghidra headless script: assemble AEON instructions via the SLEIGH assembler.
//
// Reads a patch-listing file: lines are either `@0xADDR` (set the current RAM
// address) or an AEON instruction to assemble at the running address. Branch
// targets (b.j/b.jal) are PC-relative, so each instruction is assembled at its
// actual address and the address advances by the emitted length. Writes an
// output file with one `RAMADDR HEX ; insn` line per instruction, which the
// build splices into ap_prefix.bin / ap_decompressed.bin.
//
//   analyzeHeadless ... -postScript AeonAssemble.java <in.lst> <out.txt>
//@category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.app.plugin.assembler.Assembler;
import ghidra.app.plugin.assembler.Assemblers;
import ghidra.program.model.address.Address;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class AeonAssemble extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String inPath = a[0], outPath = a[1];
        Assembler asm = Assemblers.getAssembler(currentProgram);
        List<String> lines = Files.readAllLines(Paths.get(inPath));
        StringBuilder out = new StringBuilder();
        Address cur = null;
        for (String raw : lines) {
            String ln = raw.trim();
            if (ln.isEmpty() || ln.startsWith("#")) continue;
            if (ln.startsWith("@")) { cur = toAddr(Long.decode(ln.substring(1))); continue; }
            byte[] b = asm.assembleLine(cur, ln);
            StringBuilder hex = new StringBuilder();
            for (byte x : b) hex.append(String.format("%02x", x & 0xff));
            out.append(String.format("%08x %s ; %s%n", cur.getOffset(), hex.toString(), ln));
            cur = cur.add(b.length);
        }
        Files.write(Paths.get(outPath), out.toString().getBytes());
        println("[AeonAssemble] wrote " + outPath);
    }
}
