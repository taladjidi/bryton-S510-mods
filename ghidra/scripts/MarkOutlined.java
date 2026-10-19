// MarkOutlined.java: find IAR compiler-outlined code stubs and (optionally) mark them inline so the decompiler
// folds them into their callers. A stub = default-named, no push/stack frame, <= 48 bytes, and its decompile reads
// caller registers ("unaff_r"). Name: outl_<tail-jump target>_<addr> (or outl_<addr>).
// Args: <out.txt> [APPLY]: without APPLY nothing is changed (dry run writes the candidate list).
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.SourceType;
import java.io.PrintWriter;

public class MarkOutlined extends GhidraScript {
    boolean WRAP;   // 3rd arg WRAP: also inline ABI-compliant tiny tail-call wrappers (named wrap_<target>_<addr>)
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        boolean apply = a.length > 1 && a[1].equals("APPLY");
        WRAP = a.length > 2 && a[2].equals("WRAP");
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        PrintWriter out = new PrintWriter(a[0]);
        int n = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            boolean dflt = f.getName().matches("FUN_[0-9a-f]+");
            // ABI violation (reads unpassed regs / clobbers unsaved r4-r11) is the test, so named functions qualify too;
            // only default names are replaced. Skip what is already inline.
            if (f.isInline() || f.isThunk() || f.getBody().getNumAddresses() > 48
                || f.getEntryPoint().getOffset() < 0x1000) continue;   // ITCM 0x0-0xfff is a data table
            boolean frame = false;
            String target = null;
            for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                String m = ins.getMnemonicString().toLowerCase();
                if (m.startsWith("push") || m.startsWith("stmdb") || m.startsWith("vpush") || m.startsWith("sub") && ins.toString().contains("sp,#")) frame = true;
                if ((m.equals("b") || m.equals("b.w")) && ins.getFlows().length == 1) {
                    Function t = getFunctionAt(ins.getFlows()[0]);
                    if (t != null && t != f) target = t.getName();
                }
            }
            if (frame) continue;
            boolean writesSaved = false;   // e.g. outlined prologue "mov r4,r0": clobbers caller's callee-saved regs
            for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                if (ins.getNumOperands() > 0 && ins.getRegister(0) != null &&
                    ins.getRegister(0).getName().matches("r(4|5|6|7|8|9|10|11)") &&
                    !ins.getMnemonicString().toLowerCase().matches("(str|cmp|tst|teq|cmn).*")) writesSaved = true;
            }
            // ABI-compliant tiny tail-call wrapper: <=16 bytes, only register/constant setup, ends in b/b.w to a function
            boolean wrapper = false;
            if (WRAP && dflt && target != null && f.getBody().getNumAddresses() <= 16) {
                wrapper = true;
                for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true))
                    if (!ins.getMnemonicString().toLowerCase().matches("(mov|movs|movw|movt|mov\\.w|add|adds|add\\.w|adr|ldr|ldr\\.w|subs|b|b\\.w)"))
                        wrapper = false;
            }
            DecompileResults r = wrapper ? null : di.decompileFunction(f, 20, monitor);
            if (!wrapper && (r == null || !r.decompileCompleted())) continue;
            if (!wrapper && !writesSaved && !r.getDecompiledFunction().getC().contains("unaff_r")) continue;
            String pfx = wrapper ? "wrap_" : "outl_";
            String name = (target != null && !target.startsWith("FUN_") ? pfx + target.replaceAll("^(outl|wrap)_", "") + "_" : pfx)
                          + f.getEntryPoint().toString();
            if (!dflt) name = f.getName();
            out.printf("0x%s %s%n", f.getEntryPoint(), name);
            if (apply) { f.setInline(true); if (dflt) f.setName(name, SourceType.ANALYSIS); }
            n++;
        }
        out.close(); di.dispose();
        println("MarkOutlined: " + n + (apply ? " marked inline+renamed" : " candidates (dry run)"));
    }
}
