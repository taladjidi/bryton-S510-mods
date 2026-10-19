// MapITCM.java — maps the ITCM kernel copy (exec 0x00000000, load 0xd0351a68, 64 KB) as a
// byte-mapped block, then replaces Ghidra's EXT_FUN_xxxx placeholders with real Thumb functions
// and re-points their thunks. Program-structure edit only; touches no device.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import java.math.BigInteger;
import java.util.ArrayList;
import java.util.List;

public class MapITCM extends GhidraScript {
    @Override
    public void run() throws Exception {
        Address zero = toAddr(0);
        if (getMemoryBlock(zero) == null) {
            MemoryBlock b = currentProgram.getMemory().createByteMappedBlock(
                "ITCM", zero, toAddr(0xd0351a68L), 0x10000, false);
            b.setPermissions(true, false, true);
            println("created ITCM block");
        }
        Register tmode = currentProgram.getRegister("TMode");
        var fm = currentProgram.getFunctionManager();
        List<Function> exts = new ArrayList<>();
        for (Function e : fm.getExternalFunctions()) exts.add(e);
        int made = 0, rethunk = 0, fail = 0;
        for (Function e : exts) {
            Address x = toAddr(e.getExternalLocation().getAddress().getOffset() & ~1L);
            try {
                Function real = getFunctionAt(x);
                if (real == null) {
                    if (getInstructionAt(x) == null) {
                        if (getInstructionContaining(x) != null) clearListing(x);
                        currentProgram.getProgramContext().setValue(tmode, x, x, BigInteger.ONE);
                        disassemble(x);
                    }
                    real = createFunction(x, null);
                }
                if (real == null) { fail++; println("no func at " + x); continue; }
                made++;
                for (Function t : fm.getFunctions(true)) {
                    if (t.isThunk() && e.equals(t.getThunkedFunction(false))) {
                        t.setThunkedFunction(real); rethunk++;
                    }
                }
            } catch (Exception ex) { fail++; println("FAILED " + x + ": " + ex.getMessage()); }
        }
        println("MapITCM done: funcs=" + made + " thunks_repointed=" + rethunk + " failed=" + fail);
    }
}
