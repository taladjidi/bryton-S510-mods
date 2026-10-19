// InlineBisect.java: run with -readOnly (changes are discarded): for each failing function, binary-search the set of
// ALL inline-flagged functions for a small subset whose un-inlining makes the decompile succeed.
// Args: <outfile> <file of function addresses>
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.nio.file.*;
import java.util.*;

public class InlineBisect extends GhidraScript {
    DecompInterface di;

    boolean okWithout(Function f, List<Function> un) {
        for (Function c : un) c.setInline(false);
        boolean ok = di.decompileFunction(f, 60, monitor).decompileCompleted();
        for (Function c : un) c.setInline(true);
        return ok;
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(a[0]);
        di = new DecompInterface();
        di.openProgram(currentProgram);
        List<Function> all = new ArrayList<>();
        for (Function c : currentProgram.getFunctionManager().getFunctions(true)) if (c.isInline()) all.add(c);
        for (String l : Files.readAllLines(Path.of(a[1]))) {
            Function f = getFunctionAt(toAddr(l.trim()));
            List<Function> s = all;
            while (s.size() > 1) {                  // keep the half that still fixes it; stop if neither half alone does
                List<Function> lo = s.subList(0, s.size() / 2), hi = s.subList(s.size() / 2, s.size());
                if (okWithout(f, lo)) s = lo; else if (okWithout(f, hi)) s = hi; else break;
            }
            out.print(f.getEntryPoint() + " " + f.getName() + " -> " + s.size() + ":");
            for (Function c : s.subList(0, Math.min(5, s.size()))) out.print(" " + c.getEntryPoint() + "/" + c.getName());
            out.println();
            out.flush();
        }
        out.close();
    }
}
