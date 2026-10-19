// TopCallees.java: read-only: default-named (FUN_) functions ranked by number of call sites.
// Args: <outfile> [N=200]. Line: "0xADDR callers=K insns=M"
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.util.*;

public class TopCallees extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        int n = a.length > 1 ? Integer.parseInt(a[1]) : 200;
        List<long[]> rows = new ArrayList<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (!f.getName().matches("FUN_[0-9a-f]+")) continue;
            int calls = 0;
            for (var r : getReferencesTo(f.getEntryPoint())) if (r.getReferenceType().isCall()) calls++;
            Address[] th = f.getFunctionThunkAddresses(true);
            if (th != null) for (var t : th) for (var r : getReferencesTo(t)) if (r.getReferenceType().isCall()) calls++;
            rows.add(new long[]{f.getEntryPoint().getOffset(), calls, currentProgram.getListing().getNumInstructions() > 0 ? f.getBody().getNumAddresses() : 0});
        }
        rows.sort((x, y) -> Long.compare(y[1], x[1]));
        PrintWriter out = new PrintWriter(a[0]);
        for (int i = 0; i < Math.min(n, rows.size()); i++)
            out.printf("0x%08x callers=%d bytes=%d%n", rows.get(i)[0], rows.get(i)[1], rows.get(i)[2]);
        out.close();
    }
}
