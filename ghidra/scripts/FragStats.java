// FragStats.java: read-only: classify default-named functions by how they are reached.
//   called   : at least one CALL reference (real function)
//   tail1    : only jump references, all from ONE other function   (mis-split fragment → merge candidate)
//   tailN    : only jump references, from several functions        (IAR shared cross-call tail)
//   orphan   : no code references at all (data refs / unreached)
// Args: <out.tsv>  (addr, class, nsrc, size, first source function)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.util.*;

public class FragStats extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[0]);
        Map<String, Integer> cnt = new TreeMap<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (!f.getName().matches("FUN_[0-9a-f]+") || f.isThunk()) continue;
            boolean call = false;
            Set<Function> srcs = new LinkedHashSet<>();
            int jumps = 0;
            for (var r : getReferencesTo(f.getEntryPoint())) {
                var t = r.getReferenceType();
                if (t.isCall()) { call = true; break; }
                if (t.isJump() || t.isFlow()) {
                    jumps++;
                    Function s = getFunctionContaining(r.getFromAddress());
                    if (s != null && s != f) srcs.add(s);
                }
            }
            String cls = call ? "called" : jumps == 0 ? "orphan" : srcs.size() == 1 ? "tail1" : srcs.isEmpty() ? "orphan" : "tailN";
            cnt.merge(cls, 1, Integer::sum);
            out.printf("0x%s\t%s\t%d\t%d\t%s%n", f.getEntryPoint(), cls, srcs.size(), f.getBody().getNumAddresses(),
                       srcs.isEmpty() ? "-" : srcs.iterator().next().getName());
        }
        out.close();
        println("FragStats: " + cnt);
    }
}
