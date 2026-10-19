// InlineJumps.java: read-only: list inline-flagged functions containing a computed (non-return) jump.
// Args: <outfile> [file of function addresses: then scan those + their inline callees, recursively]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class InlineJumps extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(a[0]);
        java.util.Collection<Function> fs = new java.util.LinkedHashSet<>();
        if (a.length > 1) {
            java.util.Deque<Function> q = new java.util.ArrayDeque<>();
            for (String l : java.nio.file.Files.readAllLines(java.nio.file.Path.of(a[1]))) q.add(getFunctionAt(toAddr(l.trim())));
            while (!q.isEmpty()) { Function f = q.pop(); if (f != null && fs.add(f)) for (Function c : f.getCalledFunctions(monitor)) if (c.isInline()) q.add(c); }
        } else for (Function f : currentProgram.getFunctionManager().getFunctions(true)) if (f.isInline()) fs.add(f);
        for (Function f : fs) {
            for (var i : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                var ft = i.getFlowType();
                if (ft.isComputed() && ft.isJump())
                    out.println(f.getEntryPoint() + " " + f.getName() + " " + i.getAddress() + " " + i + " " + ft);
            }
        }
        out.close();
    }
}
