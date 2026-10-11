// Ghidra headless script: recover scaler AP symbol names from referenced debug
// strings, conservatively. For every function it:
//   * defines the ASCII strings it references (so they show inline),
//   * sets a plate comment listing those strings (always; navigable evidence),
//   * records the source-file module from any "*.c" path it references,
//   * auto-renames ONLY on strong signals:
//       - it references exactly one identifier-like string, referenced by no
//         other function  -> name = that identifier;
//       - it references several identifiers sharing a long "Module_..." prefix
//         -> name = that common prefix (e.g. MDrv_SERFLASH).
// Writes /out/scaler_symbols.tsv (addr, old, new, module, evidence...).
//
//@category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.data.StringDataType;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.symbol.*;
import ghidra.util.exception.DuplicateNameException;
import ghidra.util.exception.InvalidInputException;
import java.io.*;
import java.util.*;
import java.util.regex.*;

public class NameFromStrings extends GhidraScript {

    // An identifier token: >=4 chars, letters/digits/_, must look like a symbol
    // (contains '_' or a lower->Upper camel hump), not a plain English word.
    static final Pattern IDENT = Pattern.compile("[A-Za-z][A-Za-z0-9_]{3,63}");

    // Known MStar symbol prefixes — the reliable ones. Avoids naming a function
    // after a mid-word fragment of a packed string (refs often land mid-blob).
    static final String[] PREFIX = {
        "msAPI", "msApi", "MApi", "MDrv", "MHal", "mhal", "HAL_", "PQ_", "PQ",
        "USER_PREF", "MONITOR", "Panel", "Scaler", "msScaler", "Win", "Mode",
        "GPO", "Def", "Draw", "Load", "Init", "Set", "Get", "Enable", "Mute",
        "Power", "Keystone", "CEC", "Cursor", "Osd", "OSD", "Flash", "SERFLASH",
        "Capture", "FrameSync", "g_", "u8", "u16", "u32", "bEn", "RV_" };

    boolean looksLikeSymbol(String t) {
        if (t.length() < 5) return false;
        for (String p : PREFIX) if (t.startsWith(p)) return true;
        // otherwise require an uppercase start plus an underscore (e.g. SOME_FLAG)
        if (!Character.isUpperCase(t.charAt(0))) return false;
        return t.indexOf('_') > 0;
    }

    // Leading identifier token of a string ("PQ_Init: %d" -> "PQ_Init").
    String leadIdent(String s) {
        Matcher m = IDENT.matcher(s);
        if (m.lookingAt()) {
            String t = m.group();
            if (looksLikeSymbol(t)) return t;
        }
        return null;
    }

    String readCString(Address a) {
        StringBuilder sb = new StringBuilder();
        try {
            for (int i = 0; i < 128; i++) {
                int b = getByte(a.add(i)) & 0xff;
                if (b == 0) return sb.length() >= 3 ? sb.toString() : null;
                if (b < 0x20 || b > 0x7e) return null;
                sb.append((char) b);
            }
        } catch (Exception e) { return null; }
        return null;
    }

    String commonPrefix(List<String> xs) {
        if (xs.isEmpty()) return null;
        String p = xs.get(0);
        for (String x : xs) {
            int n = Math.min(p.length(), x.length());
            int i = 0; while (i < n && p.charAt(i) == x.charAt(i)) i++;
            p = p.substring(0, i);
        }
        int cut = p.lastIndexOf('_');          // trim to a token boundary
        if (cut > 0) p = p.substring(0, cut);
        return (p.length() >= 5 && looksLikeSymbol(p)) ? p : null;
    }

    public void run() throws Exception {
        String[] args = getScriptArgs();
        String report = args.length > 0 ? args[0] : "/out/scaler_symbols.tsv";

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        // func -> ordered distinct referenced strings; and identifier tokens.
        LinkedHashMap<Function, LinkedHashSet<String>> refs = new LinkedHashMap<>();
        LinkedHashMap<Function, LinkedHashSet<String>> idents = new LinkedHashMap<>();
        LinkedHashMap<Function, String> module = new LinkedHashMap<>();
        HashMap<String, Integer> identUses = new HashMap<>();   // token -> #functions

        // Walk every reference in the program; attribute string targets to the
        // containing function. Catches refs on instructions and in pointer data,
        // and mid-string targets (packed __FUNCTION__ blobs) alike.
        ReferenceManager rm = currentProgram.getReferenceManager();
        AddressIterator ai = rm.getReferenceSourceIterator(
                currentProgram.getMemory().getAllInitializedAddressSet(), true);
        while (ai.hasNext()) {
            Address from = ai.next();
            Function f = getFunctionContaining(from);
            if (f == null) continue;
            for (Reference r : rm.getReferencesFrom(from)) {
                Address to = r.getToAddress();
                if (to == null || !to.isMemoryAddress()) continue;
                String s = stringAt(to);
                if (s == null) continue;
                refs.computeIfAbsent(f, k -> new LinkedHashSet<>()).add(s);
                if (s.contains(".c") || s.contains("monitor_ap") || s.contains("/MST9U/"))
                    module.putIfAbsent(f, moduleOf(s));
                String id = leadIdent(s);
                if (id != null) idents.computeIfAbsent(f, k -> new LinkedHashSet<>()).add(id);
            }
        }
        for (LinkedHashSet<String> ids : idents.values())
            for (String id : ids) identUses.merge(id, 1, Integer::sum);

        int renamed = 0;
        Set<String> used = new HashSet<>();
        for (Function f : fm.getFunctions(true))
            if (f.getSymbol().getSource() == SourceType.USER_DEFINED) used.add(f.getName());

        PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(report)));
        w.println("addr\told\tnew\tmodule\tevidence");
        for (Function f : fm.getFunctions(true)) {
            LinkedHashSet<String> ss = refs.get(f);
            String newName = null;
            LinkedHashSet<String> ids = idents.get(f);
            if (ids != null) {
                List<String> idl = new ArrayList<>(ids);
                // Prefer the longest identifier that only this function references.
                String best = null;
                for (String t : idl)
                    if (identUses.getOrDefault(t, 0) == 1
                            && (best == null || t.length() > best.length())) best = t;
                if (best != null) {
                    newName = best;
                } else if (idl.size() > 1) {
                    String cp = commonPrefix(idl);              // shared Module_ prefix
                    if (cp != null) newName = cp;
                }
            }
            String applied = "";
            if (newName != null) {
                String cand = newName;
                int n = 2;
                while (used.contains(cand)) cand = newName + "_" + (n++);
                try {
                    f.setName(cand, SourceType.USER_DEFINED);
                    used.add(cand); applied = cand; renamed++;
                } catch (DuplicateNameException | InvalidInputException e) { applied = ""; }
            }
            if (ss != null) {
                StringBuilder c = new StringBuilder("refs: ");
                int k = 0;
                for (String s : ss) { if (k++ > 0) c.append(" | "); c.append(s); if (k == 10) break; }
                f.setComment(c.toString());
            }
            if (ss != null || applied.length() > 0) {
                StringBuilder ev = new StringBuilder();
                if (ss != null) { int k = 0; for (String s : ss) { if (k++>0) ev.append(" ## "); ev.append(s.replace('\t',' ')); if (k==6) break; } }
                w.printf("%s\t%s\t%s\t%s\t%s%n", f.getEntryPoint(), f.getName().equals(applied) ? "FUN_"+f.getEntryPoint() : f.getName(),
                         applied, module.getOrDefault(f, ""), ev.toString());
            }
        }
        w.close();
        println("[NameFromStrings] functions renamed=" + renamed
                + " / with-string-refs=" + refs.size() + " -> " + report);
    }

    // Return the C string at a (defining it if needed); else null.
    String stringAt(Address a) {
        Data d = getDataAt(a);
        if (d != null && d.hasStringValue()) {
            Object v = d.getValue();
            return v == null ? null : v.toString();
        }
        String s = readCString(a);
        if (s == null) return null;
        if (d == null) {
            try { createData(a, new StringDataType()); } catch (Exception e) { /* keep text anyway */ }
        }
        return s;
    }

    String moduleOf(String s) {
        Matcher m = Pattern.compile("([A-Za-z0-9_]+)\\.c\\b").matcher(s);
        if (m.find()) return m.group(1);
        return s.length() > 40 ? s.substring(0, 40) : s;
    }
}
