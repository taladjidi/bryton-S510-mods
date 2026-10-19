// RamMap.java — map the STM32H747 RAM regions as uninitialised blocks and make literal-pool words that point into
// them constant pointers, so the decompiler shows the global itself (DAT_2406cac0) instead of a pool load.
// Args: <report> [APPLY]   — run with -readOnly to test in memory.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;

public class RamMap extends GhidraScript {
    static final Object[][] BLOCKS = {
        {"DTCM", 0x20000000L, 0x20000L}, {"AXI_SRAM", 0x24000000L, 0x80000L}, {"SRAM123", 0x30000000L, 0x48000L},
        {"SRAM4", 0x38000000L, 0x10000L}, {"BKP_SRAM", 0x38800000L, 0x1000L},
        {"SDRAM_RW", 0xd03d7000L, 0x1c29000L},   // ponytail: SDRAM size assumed 32 MB (image + heap/framebuffers seen up to ~0xd1f..)
    };

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(a[0]);
        var mem = currentProgram.getMemory();
        for (Object[] b : BLOCKS) {
            if (mem.getBlock(toAddr((Long) b[1])) != null) { out.println("exists " + b[0]); continue; }
            var blk = mem.createUninitializedBlock((String) b[0], toAddr((Long) b[1]), (Long) b[2], false);
            blk.setRead(true); blk.setWrite(true); blk.setExecute(false);
            out.println("block " + b[0]);
        }
        // The image is never written at run time (.data is copied out to SRAM by IAR startup; the 23 WRITE refs into
        // it are bad guesses into literal pools, see ImageWrites.java). Read-only lets the decompiler fold pool loads.
        mem.getBlock("ram").setWrite(false);
        Listing L = currentProgram.getListing();
        DataType ptr = new PointerDataType();
        int n = 0, skip = 0;
        for (Instruction i : L.getInstructions(true)) {
            if (!i.getMnemonicString().startsWith("ldr")) continue;
            for (var r : i.getReferencesFrom()) {
                var pool = r.getToAddress();
                var blk = mem.getBlock(pool);
                if (blk == null || !blk.getName().equals("ram") || !blk.isInitialized()) continue;
                long v;
                try { v = mem.getInt(pool) & 0xffffffffL; } catch (Exception x) { continue; }
                var tb = mem.getBlock(toAddr(v));
                if (tb == null || tb.getName().equals("ram") || tb.getName().equals("ITCM")) continue;
                Data d = L.getDataAt(pool);
                if (d != null && d.isPointer()) continue;
                if (L.getInstructionContaining(pool) != null) { skip++; continue; }
                try {
                    L.clearCodeUnits(pool, pool.add(3), false);
                    d = L.createData(pool, ptr);
                    MutabilitySettingsDefinition.DEF.setChoice(d, MutabilitySettingsDefinition.CONSTANT);
                    n++;
                } catch (Exception x) { skip++; }
            }
        }
        out.println("# pool pointers " + n + ", skipped " + skip);
        out.close();
    }
}
