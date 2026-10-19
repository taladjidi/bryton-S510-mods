// ExportChunks.java: read-only: export still-unnamed functions (FUN_*) in [START,END) to text chunks for
// offline review by agents. Each entry: address, size, callers, callees, string refs, decompiled C (capped).
// Args: <outdir> <0xSTART> <0xEND> [charBudgetPerChunk=110000] [maxCLines=90]
// Chunks are address-ordered so neighbouring (usually same-module) code stays together.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import java.io.File;
import java.io.PrintWriter;
import java.util.*;

public class ExportChunks extends GhidraScript {
    // local declarations ("  undefined4 uStack_10;") carry no meaning for naming; drop to save tokens
    static final java.util.regex.Pattern DECL = java.util.regex.Pattern.compile(
        "\\s+(undefined\\d*|u?int|u?short|u?longlong|char|byte|bool|float|double|code|[A-Za-z_]+_t)\\s+\\**\\w+(\\s*\\[\\d+\\])?;\\s*");
    String str(Address a) {
        Data d = getDataAt(a);
        if (d == null) return null;
        Object v = d.getValue();
        if (v instanceof Address) { Data d2 = getDataAt((Address) v); v = d2 == null ? null : d2.getValue(); }
        return v instanceof String ? (String) v : null;
    }

    String names(Collection<Function> fs, int max) {
        StringBuilder sb = new StringBuilder();
        int i = 0;
        for (Function f : fs) { if (i++ == max) { sb.append(" …+").append(fs.size() - max); break; } sb.append(' ').append(f.getName()); }
        return sb.toString();
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File dir = new File(a[0]); dir.mkdirs();
        long budget = a.length > 3 ? Long.parseLong(a[3]) : 110000;
        int maxLines = a.length > 4 ? Integer.parseInt(a[4]) : 90;
        DecompInterface di = new DecompInterface();
        { var o = new ghidra.app.decompiler.DecompileOptions(); o.grabFromProgram(currentProgram); o.setRespectReadOnly(true); di.setOptions(o); }
        di.openProgram(currentProgram);
        AddressSet range = new AddressSet(toAddr(a[1]), toAddr(Long.decode(a[2]) - 1));
        int chunk = 0, n = 0; long used = 0;
        PrintWriter out = null, idx = new PrintWriter(new File(dir, "index.tsv"));
        for (Function f : currentProgram.getFunctionManager().getFunctions(range, true)) {
            if (!f.getName().matches("FUN_[0-9a-f]+") || f.isThunk()) continue;
            StringBuilder e = new StringBuilder();
            e.append("=== ").append(f.getName()).append(" @0x").append(f.getEntryPoint())
             .append(" size=").append(f.getBody().getNumAddresses()).append('\n');
            Set<Function> callers = f.getCallingFunctions(monitor), callees = f.getCalledFunctions(monitor);
            e.append("callers(").append(callers.size()).append("):").append(names(callers, 8)).append('\n');
            e.append("callees(").append(callees.size()).append("):").append(names(callees, 16)).append('\n');
            LinkedHashSet<String> strs = new LinkedHashSet<>();
            for (Address ad : f.getBody().getAddresses(true))
                for (var r : getReferencesFrom(ad)) { String s = str(r.getToAddress()); if (s != null && s.trim().length() > 2) strs.add(s.trim().replace("\n", "\\n").replace("\r", "")); }
            if (!strs.isEmpty()) e.append("strings: ").append(String.join(" | ", strs)).append('\n');
            DecompileResults r = di.decompileFunction(f, 30, monitor);
            String c = r.decompileCompleted() ? r.getDecompiledFunction().getC() : "/* decompile failed */";
            String[] lines = c.split("\n");
            int kept = 0;
            for (String l : lines) {
                if (l.trim().isEmpty() || DECL.matcher(l).matches() || l.contains("WARNING:")) continue;
                if (kept++ == maxLines) { e.append("  /* … ").append(lines.length).append(" lines total, truncated */\n"); break; }
                e.append(l).append('\n');
            }
            if (out == null || used + e.length() > budget) {
                if (out != null) out.close();
                chunk++; used = 0;
                out = new PrintWriter(new File(dir, String.format("chunk_%03d.txt", chunk)));
            }
            out.print(e); used += e.length(); n++;
            idx.printf("%s\t0x%s\tchunk_%03d%n", f.getName(), f.getEntryPoint(), chunk);
        }
        if (out != null) out.close();
        idx.close(); di.dispose();
        println("ExportChunks: functions=" + n + " chunks=" + chunk);
    }
}
