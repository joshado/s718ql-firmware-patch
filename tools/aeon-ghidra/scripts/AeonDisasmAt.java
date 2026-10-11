// Disassemble N instructions at each given address (verification helper).
//@category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
public class AeonDisasmAt extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        int count = Integer.parseInt(a[0]);
        for (int i = 1; i < a.length; i++) {
            Address at = toAddr(Long.decode(a[i]));
            for (int k = 0; k < count; k++) {
                new DisassembleCommand(at, null, false).applyTo(currentProgram, monitor);
                Instruction in = getInstructionAt(at);
                if (in == null) { println(String.format("%08x <undecodable>", at.getOffset())); break; }
                println(String.format("%08x  %-10s %s", at.getOffset(), in.getBytes()==null?"":bytesHex(in), in.toString()));
                at = at.add(in.getLength());
            }
            println("----");
        }
    }
    String bytesHex(Instruction in) throws Exception { StringBuilder s=new StringBuilder(); for(byte b:in.getBytes()) s.append(String.format("%02x",b&0xff)); return s.toString(); }
}
