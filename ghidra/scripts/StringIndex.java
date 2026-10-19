// StringIndex.java: read-only: string cross-reference index. One TSV row per defined string that some function uses
// (directly or through one literal-pool pointer): address, string, users. Strings Ghidra never defined are picked up
// from literal-pool words that point at >=4 printable bytes + NUL in the image. Args: <out.tsv>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;
import java.util.*;

public class StringIndex extends GhidraScript {
    void users(ghidra.program.model.address.Address a, Set<String> out, int depth) {
        for (var r : getReferencesTo(a)) {
            Function f = getFunctionContaining(r.getFromAddress());
            if (f != null) out.add(f.getName());
            else if (depth == 0) users(r.getFromAddress(), out, 1);      // pool word -> its loaders
        }
    }

    @Override
    public void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[0]);
        out.println("addr\tstring\tusers");
        int n = 0;
        for (Data d : currentProgram.getListing().getDefinedData(true)) {
            if (!d.hasStringValue()) continue;
            Set<String> us = new TreeSet<>();
            users(d.getAddress(), us, 0);
            if (us.isEmpty()) continue;
            String s = String.valueOf(d.getValue()).replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n");
            out.println(d.getAddress() + "\t" + s + "\t" + String.join(" ", us));
            n++;
        }
        // undefined strings reached through literal pools
        var mem = currentProgram.getMemory();
        Map<Long, Set<String>> extra = new TreeMap<>();
        for (Instruction i : currentProgram.getListing().getInstructions(true)) {
            boolean ldr = i.getMnemonicString().startsWith("ldr");
            for (var r : i.getReferencesFrom()) {
                if (!r.getToAddress().isMemoryAddress()) continue;
                long[] cand = {r.getToAddress().getOffset(), -1};           // adr/add-pc target itself, or pool value
                if (ldr) try { cand[1] = mem.getInt(r.getToAddress()) & 0xffffffffL; } catch (Exception x) { }
                for (long v : cand) {
                    if (v < 0xd0000000L || v >= 0xd03d7000L) continue;
                    if (getInstructionContaining(toAddr(v)) != null) continue;
                    Data d = getDataAt(toAddr(v));
                    if (d != null && d.hasStringValue()) continue;        // already indexed above
                    Function f = getFunctionContaining(i.getAddress());
                    if (f != null) extra.computeIfAbsent(v, k -> new TreeSet<>()).add(f.getName());
                }
            }
        }
        for (var e : extra.entrySet()) {
            StringBuilder sb = new StringBuilder();
            boolean ok = false;
            for (int k = 0; k < 200; k++) {
                int c = mem.getByte(toAddr(e.getKey() + k)) & 0xff;
                if (c == 0) { ok = sb.length() >= 4; break; }
                if (c == 9) sb.append("\\t"); else if (c == 10) sb.append("\\n"); else if (c == 13) sb.append("\\r");
                else if (c >= 0x20 && c < 0x7f) sb.append((char) c); else break;
            }
            if (!ok) continue;
            out.println(toAddr(e.getKey()) + "\t" + sb + "\t" + String.join(" ", e.getValue()));
            n++;
        }
        out.close();
        println("StringIndex rows " + n);
    }
}
