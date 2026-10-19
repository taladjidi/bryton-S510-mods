// ImageWrites.java — read-only: WRITE references whose target lies in the image block "ram" (is the image read-only?).
// Args: <outfile>
import ghidra.app.script.GhidraScript;
import java.io.PrintWriter;

public class ImageWrites extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[0]);
        var blk = currentProgram.getMemory().getBlock("ram");
        int n = 0;
        for (var a : currentProgram.getReferenceManager().getReferenceDestinationIterator(new ghidra.program.model.address.AddressSet(blk.getStart(), blk.getEnd()), true))
            for (var r : getReferencesTo(a))
                if (r.getReferenceType().isWrite()) {
                    n++;
                    var f = getFunctionContaining(r.getFromAddress());
                    out.println(a + " <- " + r.getFromAddress() + " " + (f == null ? "?" : f.getName()));
                }
        out.println("# write refs " + n);
        out.close();
    }
}
