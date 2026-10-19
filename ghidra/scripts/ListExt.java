// ListExt.java — read-only: list external (unmapped-address) functions with caller counts.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class ListExt extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[0]);
        for (Function f : currentProgram.getFunctionManager().getExternalFunctions()) {
            out.println(f.getName() + " " + f.getExternalLocation().getAddress()
                + " refs=" + getReferencesTo(f.getEntryPoint()).length);
        }
        out.close();
    }
}
