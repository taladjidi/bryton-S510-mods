// GlobalRefs.java: read-only: memory blocks, then rank RAM addresses by how many functions load them from a
// literal pool (ldr rX,[pc,#..] -> 32-bit word whose value points outside the image). Args: <outfile> [top N]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.io.PrintWriter;
import java.util.*;

public class GlobalRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        int top = a.length > 1 ? Integer.parseInt(a[1]) : 300;
        PrintWriter out = new PrintWriter(a[0]);
        for (var b : currentProgram.getMemory().getBlocks())
            out.println("block " + b.getName() + " " + b.getStart() + "-" + b.getEnd() + " " + (b.isInitialized() ? "init" : "uninit"));
        Map<Long, Set<Function>> users = new HashMap<>();
        Listing L = currentProgram.getListing();
        for (Instruction i : L.getInstructions(true)) {
            if (!i.getMnemonicString().startsWith("ldr")) continue;
            for (var r : i.getReferencesFrom()) {
                var to = r.getToAddress();
                if (!to.isMemoryAddress() || !currentProgram.getMemory().contains(to)) continue;
                long v;
                try { v = currentProgram.getMemory().getInt(to) & 0xffffffffL; } catch (Exception x) { continue; }
                if (v >= 0xd0000000L && v < 0xd03d7000L) continue;          // points into the image itself
                if (v < 0x10000L || (v >= 0x08000000L && v < 0x08200000L)) continue;  // ITCM code / flash
                if (!(v >= 0x20000000L && v < 0x40000000L) && !(v >= 0xc0000000L && v < 0xe0000000L)) continue;
                Function f = getFunctionContaining(i.getAddress());
                if (f != null) users.computeIfAbsent(v, k -> new HashSet<>()).add(f);
            }
        }
        List<Map.Entry<Long, Set<Function>>> es = new ArrayList<>(users.entrySet());
        es.sort((x, y) -> y.getValue().size() - x.getValue().size());
        out.println("# distinct RAM addresses " + es.size());
        for (var e : es.subList(0, Math.min(top, es.size()))) {
            var sym = getSymbolAt(toAddr(e.getKey()));
            List<String> ns = new ArrayList<>();
            for (Function f : e.getValue()) if (!f.getName().startsWith("FUN_") && ns.size() < 6) ns.add(f.getName());
            out.printf("%08x %4d %s | %s%n", e.getKey(), e.getValue().size(), sym == null ? "-" : sym.getName(), String.join(" ", ns));
        }
        out.close();
    }
}
