// IapSetup.java — one-time setup of the IAP (bootloader) project: create functions at every Cortex-M vector
// table entry (reset, faults, IRQs) so analysis reaches the boot code. Re-run with analyzeHeadless ... -postScript.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class IapSetup extends GhidraScript {
    @Override public void run() throws Exception {
        Address base = currentProgram.getMinAddress();
        int made = 0;
        for (int i = 1; i < 166; i++) {                       // STM32H747: 16 core + 150 IRQ vectors
            long v = getInt(base.add(4L * i)) & 0xffffffffL;
            if ((v & 1) == 0 || v < 0x08000000L || v >= 0x08020000L) continue;
            Address a = toAddr(v & ~1L);
            if (getFunctionAt(a) == null) {
                if (getInstructionAt(a) != null && getInstructionAt(a).getLength() == 4
                        && currentProgram.getProgramContext().getValue(currentProgram.getRegister("TMode"), a, false) == null)
                    clearListing(a, a.add(3));                  // auto-analysis guessed ARM mode here
                try {
                    currentProgram.getProgramContext().setValue(currentProgram.getRegister("TMode"), a, a, java.math.BigInteger.ONE);
                } catch (ghidra.program.model.listing.ContextChangeException e) { /* already Thumb */ }
                disassemble(a);
                if (createFunction(a, i == 1 ? "Reset_Handler" : null) != null) made++;
            }
        }
        analyzeAll(currentProgram);
        println("IapSetup: created " + made + " vector functions, total " + currentProgram.getFunctionManager().getFunctionCount());
    }
}
