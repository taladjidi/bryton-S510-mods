// GlobalCtx.java: read-only: for the most-used RAM globals (still unnamed DAT_*), print their named users and the
// decompiled lines that touch them, as evidence for naming. Args: <outfile> [top N] [users decompiled per global]
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;
import java.util.*;

public class GlobalCtx extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        int top = a.length > 1 ? Integer.parseInt(a[1]) : 120, per = a.length > 2 ? Integer.parseInt(a[2]) : 3;
        var mem = currentProgram.getMemory();
        Map<Long, Set<Function>> users = new HashMap<>();
        for (Instruction i : currentProgram.getListing().getInstructions(true)) {
            if (!i.getMnemonicString().startsWith("ldr")) continue;
            for (var r : i.getReferencesFrom()) {
                long v;
                try { v = mem.getInt(r.getToAddress()) & 0xffffffffL; } catch (Exception x) { continue; }
                var b = mem.getBlock(toAddr(v));
                if (b == null || b.getName().equals("ram") || b.getName().equals("ITCM")) continue;
                Function f = getFunctionContaining(i.getAddress());
                if (f != null) users.computeIfAbsent(v, k -> new HashSet<>()).add(f);
            }
        }
        List<Long> ks = new ArrayList<>(users.keySet());
        ks.removeIf(k -> { var s = getSymbolAt(toAddr(k)); return s != null && !s.getName().startsWith("DAT_"); });
        ks.sort((x, y) -> users.get(y).size() - users.get(x).size());
        DecompInterface di = new DecompInterface();
        var opts = new DecompileOptions();
        opts.grabFromProgram(currentProgram);
        opts.setRespectReadOnly(true);
        di.setOptions(opts);
        di.openProgram(currentProgram);
        PrintWriter out = new PrintWriter(a[0]);
        for (long k : ks.subList(0, Math.min(top, ks.size()))) {
            String tag = String.format("%08x", k);
            List<Function> fs = new ArrayList<>(users.get(k));
            fs.sort(Comparator.comparing(f -> f.getName().startsWith("FUN_")));
            out.println("=== " + tag + " " + mem.getBlock(toAddr(k)).getName() + " users=" + fs.size());
            StringBuilder names = new StringBuilder();
            for (Function f : fs) if (!f.getName().startsWith("FUN_")) names.append(f.getName()).append(' ');
            out.println("  " + names);
            for (Function f : fs.subList(0, Math.min(per, fs.size()))) {
                var r = di.decompileFunction(f, 30, monitor);
                if (!r.decompileCompleted()) continue;
                int n = 0;
                for (String l : r.getDecompiledFunction().getC().split("\n"))
                    if (l.contains(tag) && n++ < 4) out.println("  [" + f.getName() + "] " + l.trim());
            }
        }
        out.close();
    }
}
