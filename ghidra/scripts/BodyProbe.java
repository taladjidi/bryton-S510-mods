// BodyProbe.java — read-only probe: body ranges of a function, and decompile result with all inline callees un-inlined.
// Args: <outfile> <addr>
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class BodyProbe extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(a[0]);
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        Function f = getFunctionAt(toAddr(a[1]));
        out.println("body " + f.getBody());
        for (Function c : f.getCalledFunctions(monitor)) if (c.isInline()) c.setInline(false);
        var r = di.decompileFunction(f, 60, monitor);
        out.println("all-uninlined ok=" + r.decompileCompleted() + " " + r.getErrorMessage());
        if (r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
        out.close();
    }
}
