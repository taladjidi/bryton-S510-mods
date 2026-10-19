// FailScan.java: read-only: decompile every function whose name matches a regex, list failures + error.
// Args: <outfile> <regex>
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class FailScan extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(a[0]);
        DecompInterface di = new DecompInterface();
        var opts = new ghidra.app.decompiler.DecompileOptions();
        opts.grabFromProgram(currentProgram);
        opts.setRespectReadOnly(true);
        di.setOptions(opts);
        di.openProgram(currentProgram);
        int n = 0, bad = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (!f.getName().matches(a[1])) continue;
            n++;
            var r = di.decompileFunction(f, 60, monitor);
            if (r.decompileCompleted()) continue;
            bad++;
            out.println(f.getEntryPoint() + " " + f.getName() + " | " + r.getErrorMessage().replace("\n", " ").trim());
        }
        out.println("# " + bad + " / " + n + " failed");
        out.close();
    }
}
