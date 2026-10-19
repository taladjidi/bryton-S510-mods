// FuncNameStrings.java: read-only: propose original function names from __func__ usage.
// A function F gets candidate name S when F references an identifier-like string S
// (C identifier, >= 5 chars) AND F also references a format string containing "%s(" / "%s:" /
// "[%s]" / "<%s>" (the usual __func__ patterns), AND S is referenced by no other function.
// Output (arg 0): "0xADDR name   # current-name | format" lines for review; nothing is renamed.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.util.*;
import java.util.regex.Pattern;

public class FuncNameStrings extends GhidraScript {
    static final Pattern IDENT = Pattern.compile("[A-Za-z_][A-Za-z0-9_]{4,63}");
    static final Pattern FMT = Pattern.compile("%s\\s*\\(|%s\\s*:|\\[%s\\]|<%s>|^%s\\s");

    String strAt(Address a) {
        Data d = getDataAt(a);
        if (d == null) return null;
        Object v = d.getValue();
        if (v instanceof Address) { Data d2 = getDataAt((Address) v); v = d2 == null ? null : d2.getValue(); }
        return v instanceof String ? (String) v : null;
    }

    @Override
    public void run() throws Exception {
        Map<String, Set<Function>> users = new HashMap<>();
        Map<Function, List<String>> idents = new HashMap<>();
        Map<Function, String> fmt = new HashMap<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            for (Address a : f.getBody().getAddresses(true)) {
                for (var r : getReferencesFrom(a)) {
                    String s = strAt(r.getToAddress());
                    if (s == null) continue;
                    if (IDENT.matcher(s).matches()) {
                        users.computeIfAbsent(s, k -> new HashSet<>()).add(f);
                        idents.computeIfAbsent(f, k -> new ArrayList<>()).add(s);
                    } else if (FMT.matcher(s).find()) fmt.putIfAbsent(f, s.trim());
                }
            }
        }
        PrintWriter out = new PrintWriter(getScriptArgs()[0]);
        int n = 0;
        for (var e : idents.entrySet()) {
            Function f = e.getKey();
            if (!fmt.containsKey(f)) continue;
            List<String> c = new ArrayList<>();
            for (String s : new LinkedHashSet<>(e.getValue())) if (users.get(s).size() == 1) c.add(s);
            if (c.size() != 1) continue; // ambiguous or none
            out.printf("0x%08x %s   # %s | %s%n", f.getEntryPoint().getOffset(), c.get(0), f.getName(),
                       fmt.get(f).replace("\n", " ").replace("\r", ""));
            n++;
        }
        out.close();
        println("FuncNameStrings: candidates=" + n);
    }
}
