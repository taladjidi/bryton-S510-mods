// PatchCheck.java: read-only (run with -readOnly): write patch bytes into memory, disassemble them, print the result,
// so hand-encoded Thumb instructions are checked by Ghidra's own decoder before flashing. Nothing is saved.
// Args: <outfile> 0xADDR:HEXBYTES [0xADDR:HEXBYTES ...] [D0xADDR ...] (D: decompile patched code at ADDR)   (shows every patched instruction + context)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class PatchCheck extends GhidraScript {
    @Override public void run() throws Exception {
        String[] a = getScriptArgs();
        try (java.io.PrintWriter out = new java.io.PrintWriter(a[0])) {
            var di = new ghidra.app.decompiler.DecompInterface();
            for (int i = 1; i < a.length; i++) {
                if (a[i].startsWith("D")) {                 // D0xADDR: decompile the patched code at ADDR
                    Address f = toAddr(a[i].substring(1));
                    var fn = getFunctionAt(f) != null ? getFunctionAt(f) : createFunction(f, null);
                    if (!di.openProgram(currentProgram)) di.openProgram(currentProgram);
                    out.println("### decompile " + f);
                    out.println(fn == null ? "  (no function)" : di.decompileFunction(fn, 60, monitor).getDecompiledFunction().getC());
                    continue;
                }
                String[] p = a[i].split(":");
                Address at = toAddr(p[0]);
                byte[] b = new byte[p[1].length() / 2];
                for (int k = 0; k < b.length; k++) b[k] = (byte) Integer.parseInt(p[1].substring(2 * k, 2 * k + 2), 16);
                clearListing(at, at.add(b.length + 15));
                setBytes(at, b);
                var tmode = currentProgram.getProgramContext().getRegister("TMode");   // all S510 code is Thumb
                currentProgram.getProgramContext().setValue(tmode, at, at.add(b.length - 1), java.math.BigInteger.ONE);
                disassemble(at);
                out.println("### " + a[i]);
                Address cur = at;
                for (int n = 0; n < 8 || cur.compareTo(at.add(b.length)) < 0; n++) {   // all patched + context
                    var ins = getInstructionAt(cur);
                    if (ins == null) { out.println("  " + cur + "  (not disassembled)"); break; }
                    out.println("  " + cur + "  " + ins + (cur.compareTo(at.add(b.length)) < 0 ? "   <- patched" : ""));
                    cur = cur.add(ins.getLength());
                }
            }
        }
    }
}
