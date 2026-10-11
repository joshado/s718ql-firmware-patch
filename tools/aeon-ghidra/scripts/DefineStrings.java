// Ghidra headless script: define ASCII strings across the scaler AP data region
// so the decompiler renders them inline. Default analysis defines only a few
// hundred of the ~6k printable strings; this creates the rest by scanning for
// printable, null-terminated runs in the data area (above the code).
//@category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.mem.*;

public class DefineStrings extends GhidraScript {
    public void run() throws Exception {
        // Data lives above the code; code ends ~0x209c79 (header ap_ram_end).
        long dataStart = 0x209c80L;
        int minLen = 5;
        Memory mem = currentProgram.getMemory();
        Address end = currentProgram.getMaxAddress();
        Address a = toAddr(dataStart);
        int made = 0, skipped = 0;

        while (a != null && a.compareTo(end) < 0) {
            if (monitor.isCancelled()) break;
            Data d = getDataAt(a);
            if (d != null && d.isDefined()) { a = a.add(d.getLength()); continue; }
            int len = runLen(a, end);
            if (len >= minLen) {
                try {
                    createData(a, new StringDataType());
                    made++;
                    a = a.add(len + 1);        // past the terminator
                    continue;
                } catch (Exception e) { skipped++; }
            }
            a = a.add(1);
        }
        println("[DefineStrings] defined " + made + " strings (skipped " + skipped + ")");
    }

    // Length of a printable run at a, requiring a null terminator; else 0.
    int runLen(Address a, Address end) {
        int n = 0;
        try {
            while (a.add(n).compareTo(end) < 0 && n < 512) {
                int b = getByte(a.add(n)) & 0xff;
                if (b == 0) return n;              // terminated
                if (b < 0x20 || b > 0x7e) return 0;
                n++;
            }
        } catch (Exception e) { return 0; }
        return 0;
    }
}
