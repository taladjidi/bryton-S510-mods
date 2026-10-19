// DeadFuncs.java: read-only: list functions that nothing reaches; no references to the entry (or entry|1), the
// previous instruction cannot fall through into it, and no 32-bit word anywhere in memory equals entry|1, as
// candidate free space for code patches. Args: <outfile> [minBytes=64]. Verify each candidate by hand.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

public class DeadFuncs extends GhidraScript {
    @Override public void run() throws Exception {
        String[] a = getScriptArgs();
        int min = a.length > 1 ? Integer.parseInt(a[1]) : 64;
        java.util.Set<Long> words = new java.util.HashSet<>();
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (!b.isInitialized()) continue;
            byte[] buf = new byte[(int) b.getSize()];
            b.getBytes(b.getStart(), buf);
            for (int i = 0; i + 4 <= buf.length; i += 2)
                words.add((buf[i] & 0xffL) | (buf[i + 1] & 0xffL) << 8 | (buf[i + 2] & 0xffL) << 16 | (buf[i + 3] & 0xffL) << 24);
        }
        try (java.io.PrintWriter out = new java.io.PrintWriter(a[0])) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                Address e = f.getEntryPoint();
                long size = f.getBody().getNumAddresses();
                if (size < min || f.isThunk()) continue;
                if (getReferencesTo(e).length > 0 || getReferencesTo(e.add(1)).length > 0) continue;
                if (words.contains(e.getOffset() | 1) || words.contains(e.getOffset())) continue;
                var prev = getInstructionBefore(e);
                if (prev != null && prev.getFlowType().isFallthrough()) continue;
                if (f.getBody().getNumAddressRanges() != 1) continue;
                out.printf("%s %5d %s%n", e, size, f.getName());
            }
        }
    }
}
