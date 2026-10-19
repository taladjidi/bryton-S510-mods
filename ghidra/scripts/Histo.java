// Histo.java — read-only: function count and code bytes per 256 KB bucket (layout overview).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.util.TreeMap;

public class Histo extends GhidraScript {
    @Override
    public void run() throws Exception {
        TreeMap<Long, long[]> b = new TreeMap<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            long k = f.getEntryPoint().getOffset() & ~0x3ffffL;
            long[] v = b.computeIfAbsent(k, x -> new long[2]);
            v[0]++; v[1] += f.getBody().getNumAddresses();
        }
        PrintWriter out = new PrintWriter(getScriptArgs()[0]);
        for (var e : b.entrySet())
            out.printf("0x%08x  funcs=%-5d codeKB=%d%n", e.getKey(), e.getValue()[0], e.getValue()[1] / 1024);
        out.close();
    }
}
