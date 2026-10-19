// PatchCheck.java — read-only (run with -readOnly): write patch bytes into memory, disassemble them, print the result,
// so hand-encoded Thumb instructions are checked by Ghidra's own decoder before flashing. Nothing is saved.
// Args: <outfile> 0xADDR:HEXBYTES [0xADDR:HEXBYTES ...]   (shows a few instructions of context after each)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class PatchCheck extends GhidraScript {
    @Override public void run() throws Exception {
        String[] a = getScriptArgs();
        try (java.io.PrintWriter out = new java.io.PrintWriter(a[0])) {
            for (int i = 1; i < a.length; i++) {
                String[] p = a[i].split(":");
                Address at = toAddr(p[0]);
                byte[] b = new byte[p[1].length() / 2];
                for (int k = 0; k < b.length; k++) b[k] = (byte) Integer.parseInt(p[1].substring(2 * k, 2 * k + 2), 16);
                clearListing(at, at.add(b.length + 15));
                setBytes(at, b);
                disassemble(at);
                out.println("### " + a[i]);
                Address cur = at;
                for (int n = 0; n < 8; n++) {
                    var ins = getInstructionAt(cur);
                    if (ins == null) { out.println("  " + cur + "  (not disassembled)"); break; }
                    out.println("  " + cur + "  " + ins + (cur.compareTo(at.add(b.length)) < 0 ? "   <- patched" : ""));
                    cur = cur.add(ins.getLength());
                }
            }
        }
    }
}
