// NullSubs.java — read-only: emit "0xADDR nullsub_<addr>" for default-named functions whose body is
// a single return (bx lr). IDA-style honest placeholder; removes noise from decompiles.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import java.io.PrintWriter;

public class NullSubs extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[0]);
        int n = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (!f.getName().matches("FUN_[0-9a-f]+")) continue;
            Instruction i = getInstructionAt(f.getEntryPoint());
            if (i != null && f.getBody().getNumAddresses() == i.getLength()
                    && i.toString().equals("bx lr")) {
                out.printf("0x%08x nullsub_%08x%n", f.getEntryPoint().getOffset(), f.getEntryPoint().getOffset());
                n++;
            }
        }
        out.close();
        println("NullSubs: " + n);
    }
}
