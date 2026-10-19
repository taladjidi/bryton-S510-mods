// CallSeq.java — read-only: for the named functions (args after the outfile), print the ordered
// sequence of calls (thunks resolved to their target's name) and string literals referenced, in
// address order. Run on both the original image and a recompiled .o, then diff the outputs.
// Args: <outfile> <funcName|0xADDR|ALL> ...   (prefix outfile with '+' to append; ALL = every
// non-thunk function). Lines: "### <name> insns=N prog=<program>" then "  call X" / "  str "..."".
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import java.io.PrintWriter;

public class CallSeq extends GhidraScript {
    String str(Address a) {
        Data d = getDataAt(a);
        if (d == null) return null;
        Object v = d.getValue();
        if (v instanceof Address) { Data d2 = getDataAt((Address) v); v = d2 == null ? null : d2.getValue(); }
        return v instanceof String ? (String) v : null;
    }

    Function lookup(String sel) {
        if (sel.startsWith("0x")) return getFunctionAt(toAddr(sel));
        for (Function f : currentProgram.getFunctionManager().getFunctions(true))
            if (f.getName().equals(sel)) return f;
        return null;
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        boolean app = a[0].startsWith("+");
        PrintWriter out = new PrintWriter(new java.io.FileWriter(app ? a[0].substring(1) : a[0], app));
        java.util.List<Function> sel = new java.util.ArrayList<>();
        for (int i = 1; i < a.length; i++) {
            if (a[i].equals("ALL")) { for (Function g : currentProgram.getFunctionManager().getFunctions(true)) if (!g.isThunk()) sel.add(g); }
            else { Function g = lookup(a[i]); if (g == null) out.println("### " + a[i] + " NOT FOUND"); else sel.add(g); }
        }
        for (Function f : sel) {
            int ninsn = 0;
            StringBuilder sb = new StringBuilder();
            for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                ninsn++;
                for (var r : ins.getReferencesFrom()) {
                    if (r.getReferenceType().isCall()) {
                        Function c = getFunctionAt(r.getToAddress());
                        if (c != null && c.isThunk()) c = c.getThunkedFunction(true);
                        sb.append("  call ").append(c == null ? r.getToAddress().toString() : c.getName()).append('\n');
                    } else {
                        String s = str(r.getToAddress());
                        if (s != null) sb.append("  str  \"").append(s.trim().replace("\r", "").replace("\n", " ")).append("\"\n");
                    }
                }
            }
            out.println("### " + f.getName() + " insns=" + ninsn + " prog=" + currentProgram.getName() + " addr=" + f.getEntryPoint());
            out.print(sb);
        }
        out.close();
    }
}
