// MakeFuncs.java — create Thumb functions at the given addresses (args: 0xADDR ...).
// Used for task entry points reached only via function pointers. Program edit only.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import java.math.BigInteger;

public class MakeFuncs extends GhidraScript {
    @Override
    public void run() throws Exception {
        var tmode = currentProgram.getRegister("TMode");
        for (String s : getScriptArgs()) {
            Address x = toAddr(Long.decode(s) & ~1L);
            if (getFunctionAt(x) != null) continue;
            if (getInstructionAt(x) == null) {
                if (getInstructionContaining(x) != null || getDataContaining(x) != null) clearListing(x);
                currentProgram.getProgramContext().setValue(tmode, x, x, BigInteger.ONE);
                disassemble(x);
            }
            println((createFunction(x, null) != null ? "made " : "FAILED ") + x);
        }
    }
}
