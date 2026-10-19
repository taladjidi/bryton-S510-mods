// ListNames.java — read-only: "0xADDR name" for every function (current project state). Arg: outfile.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class ListNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[0]);
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) if (!f.isThunk())
            out.printf("0x%08x %s %d%n", f.getEntryPoint().getOffset(), f.getName(), f.getBody().getNumAddresses());
        out.close();
    }
}
