// Coverage.java — read-only: count named vs default-named (FUN_/thunk_FUN_) functions per region.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.util.TreeMap;

public class Coverage extends GhidraScript {
    @Override
    public void run() throws Exception {
        TreeMap<String, int[]> m = new TreeMap<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            long a = f.getEntryPoint().getOffset();
            String r = a < 0x10000 ? "ITCM" : String.format("0x%08x", a & ~0x3ffffL);
            int[] v = m.computeIfAbsent(r, k -> new int[2]);
            int[] t = m.computeIfAbsent("TOTAL", k -> new int[2]);
            boolean dflt = f.getName().matches("(thunk_)?FUN_[0-9a-f]+");
            v[0]++; t[0]++; if (!dflt) { v[1]++; t[1]++; }
        }
        PrintWriter out = new PrintWriter(getScriptArgs()[0]);
        for (var e : m.entrySet())
            out.printf("%-12s funcs=%-6d named=%-6d %5.1f%%%n", e.getKey(), e.getValue()[0], e.getValue()[1], 100.0 * e.getValue()[1] / e.getValue()[0]);
        out.close();
    }
}
