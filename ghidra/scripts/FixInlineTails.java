// FixInlineTails.java — inline-flagged functions (IAR outlined stubs/wrappers) that are not disassembled, or that end
// in a plain branch out of their body (tail call), break the decompiler when inlined ("Bad partial clone").
// Disassemble them and mark the outgoing branch CALL_RETURN, which is how Ghidra models a tail call.
// Args: <report file> [APPLY]   — without APPLY only reports.
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;

public class FixInlineTails extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        boolean apply = a.length > 1 && a[1].equals("APPLY");
        PrintWriter out = new PrintWriter(a[0]);
        Listing L = currentProgram.getListing();
        int undis = 0, tails = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (!f.isInline()) continue;
            if (L.getInstructionAt(f.getEntryPoint()) == null) {
                undis++;
                out.println("undisassembled " + f.getEntryPoint() + " " + f.getName());
                if (!apply) continue;
                var cmd = new DisassembleCommand(f.getEntryPoint(), null, true);   // follows flow until the branch
                cmd.applyTo(currentProgram, monitor);
                var end = cmd.getDisassembledAddressSet().getMaxAddress();
                Function nx = getFunctionAfter(f.getEntryPoint());   // multi-entry stubs fall into the next stub
                if (nx != null && nx.getEntryPoint().compareTo(end) <= 0) end = nx.getEntryPoint().previous();
                try { f.setBody(new AddressSet(f.getEntryPoint(), end)); }
                catch (Exception x) { out.println("  body kept (" + x.getMessage() + ")"); }
            }
            // walk from the entry along fall-through (may run into the next stub) to the first unconditional exit
            Instruction i = L.getInstructionAt(f.getEntryPoint());
            for (int n = 0; i != null && n < 32; n++, i = i.getFallThrough() == null ? null : L.getInstructionAt(i.getFallThrough())) {
                var ft = i.getFlowType();
                if (!ft.isJump() || ft.isConditional() || ft.isComputed() || i.getFlowOverride() == FlowOverride.CALL_RETURN) continue;
                var to = i.getFlows();
                if (to.length != 1 || f.getBody().contains(to[0]) || getFunctionAt(to[0]) == null) continue;
                tails++;
                out.println("tail " + f.getEntryPoint() + " " + f.getName() + " " + i.getAddress() + " -> " + getFunctionAt(to[0]).getName());
                if (apply) i.setFlowOverride(FlowOverride.CALL_RETURN);
            }
        }
        out.println("# undisassembled " + undis + ", tail branches " + tails + (apply ? " (fixed)" : " (dry run)"));
        out.close();
    }
}
