// Dump.java: read-only helper: writes decompiler output to a text file.
// Args: <outfile> then any of
//   d:0xADDR   decompile the function at ADDR
//   c:0xADDR   for every caller of ADDR, print caller name + the decompiled lines that call it
//   x:0xADDR   list callers of ADDR (incl. via thunks), names + addresses only
//   r:0xADDR   strings referenced by the function and its callees (3 levels deep)
//   R:0xSTART:0xEND  decompile every function whose entry is in [START,END)
//   L:0xSTART:0xEND  Ghidra disassembly listing of [START,END)
//   s:0xADDR   decompile every function referencing ADDR (follows one literal-pool hop)
//   u:0xADDR   decompile with ALL inline flags cleared (in memory only), for the ~35 EW __Init that fail with
//              "Bad partial clone": they tail-call by falling through into the next function, whose inlined
//              outlined-stubs then break jumptable recovery
// Read-only: no renames, no program changes.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.util.Set;

public class Dump extends GhidraScript {
    DecompInterface di;

    String decomp(Function f) {
        DecompileResults r = di.decompileFunction(f, 60, monitor);
        return r.decompileCompleted() ? r.getDecompiledFunction().getC() : "/* decompile failed: " + r.getErrorMessage() + " */";
    }

    void strs(Function f, int depth, java.util.Set<Function> seen, PrintWriter out) throws Exception {
        if (f == null || !seen.add(f) || depth > 3) return;
        if (f.isThunk()) { strs(f.getThunkedFunction(true), depth, seen, out); return; }
        for (Address a : f.getBody().getAddresses(true)) {
            for (var r : getReferencesFrom(a)) {
                var d = getDataAt(r.getToAddress());
                if (d == null) continue;
                Object v = d.getValue();
                if (v instanceof ghidra.program.model.address.Address) { var d2 = getDataAt((Address) v); if (d2 != null) v = d2.getValue(); }
                if (v instanceof String && ((String) v).length() > 3)
                    out.println("  ".repeat(depth) + f.getName() + ": " + ((String) v).replace("\r","").replace("\n"," ").trim());
            }
        }
        for (Function c : f.getCalledFunctions(monitor)) strs(c, depth + 1, seen, out);
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(a[0]);
        di = new DecompInterface();
        var opts = new ghidra.app.decompiler.DecompileOptions();
        opts.grabFromProgram(currentProgram);
        opts.setRespectReadOnly(true);                 // image block is read-only (RamMap.java): fold literal pools
        di.setOptions(opts);
        di.openProgram(currentProgram);
        for (int i = 1; i < a.length; i++) {
            String mode = a[i].substring(0, 1);
            if (mode.equals("L")) {
                String[] p = a[i].split(":");
                out.println("### listing " + p[1] + ".." + p[2]);
                var it = currentProgram.getListing().getCodeUnits(
                    new ghidra.program.model.address.AddressSet(toAddr(p[1]), toAddr(Long.decode(p[2]) - 1)), true);
                for (var cu : it) {
                    Function f = getFunctionAt(cu.getAddress());
                    if (f != null) out.println(f.getName() + ":");
                    out.println("  " + cu.getAddress() + "  " + cu.toString());
                }
                continue;
            }
            if (mode.equals("R")) {
                String[] p = a[i].split(":");
                for (Function f : currentProgram.getFunctionManager().getFunctions(
                        new ghidra.program.model.address.AddressSet(toAddr(p[1]), toAddr(Long.decode(p[2]) - 1)), true)) {
                    out.println("### " + f.getName() + " @ " + f.getEntryPoint());
                    out.println(decomp(f));
                }
                continue;
            }
            Address addr = toAddr(a[i].substring(2));
            if (mode.equals("u")) {
                Function f = getFunctionAt(addr);
                java.util.List<Function> un = new java.util.ArrayList<>();
                for (Function c : currentProgram.getFunctionManager().getFunctions(true)) if (c.isInline()) { c.setInline(false); un.add(c); }
                out.println("### " + f.getName() + " @ " + f.getEntryPoint() + " (un-inlined " + un.size() + ")");
                out.println(decomp(f));
                for (Function c : un) c.setInline(true);
                continue;
            }
            if (mode.equals("s")) {
                java.util.Set<Function> fs = new java.util.LinkedHashSet<>();
                for (var r : getReferencesTo(addr)) {
                    Function g = getFunctionContaining(r.getFromAddress());
                    if (g != null) fs.add(g);
                    else for (var r2 : getReferencesTo(r.getFromAddress())) {
                        Function h = getFunctionContaining(r2.getFromAddress());
                        if (h != null) fs.add(h);
                    }
                }
                out.println("### refs to " + addr + " -> " + fs.size() + " functions");
                for (Function g : fs) { out.println("## " + g.getName() + " @ " + g.getEntryPoint()); out.println(decomp(g)); }
                continue;
            }
            if (mode.equals("r")) {
                Function f = getFunctionAt(addr);
                out.println("### strings under " + f.getName());
                strs(f, 0, new java.util.HashSet<>(), out);
                continue;
            }
            if (mode.equals("x")) {
                Function f = getFunctionAt(addr);
                java.util.Set<Function> cs = new java.util.TreeSet<>((p, q) -> p.getEntryPoint().compareTo(q.getEntryPoint()));
                cs.addAll(f.getCallingFunctions(monitor));
                Address[] th = f.getFunctionThunkAddresses(true);
                if (th != null) for (Address t : th) { Function tf = getFunctionAt(t); cs.remove(tf); cs.addAll(tf.getCallingFunctions(monitor)); }
                out.println("### callers of " + f.getName() + " (" + cs.size() + ")");
                for (Function c : cs) out.println(c.getEntryPoint() + " " + c.getName() + " size=" + c.getBody().getNumAddresses());
                continue;
            }
            Function f = mode.equals("d") ? getFunctionContaining(addr) : getFunctionAt(addr);
            if (f == null) { out.println("### no function at " + addr); continue; }
            if (mode.equals("d")) {
                out.println("### " + f.getName() + " @ " + f.getEntryPoint());
                out.println(decomp(f));
            } else {
                Set<Function> callers = new java.util.LinkedHashSet<>(f.getCallingFunctions(monitor));
                Address[] th = f.getFunctionThunkAddresses(true);
                java.util.List<String> names = new java.util.ArrayList<>(java.util.List.of(f.getName()));
                if (th != null) for (Address t : th) {
                    Function tf = getFunctionAt(t); callers.remove(tf); names.add(tf.getName());
                    callers.addAll(tf.getCallingFunctions(monitor));
                }
                out.println("### callers of " + f.getName() + " (" + callers.size() + ")");
                for (Function c : callers) {
                    out.println("-- " + c.getName() + " @ " + c.getEntryPoint());
                    for (String line : decomp(c).split("\n"))
                        for (String n : names) if (line.contains(n + "(")) { out.println("   " + line.trim()); break; }
                }
            }
        }
        out.close();
        di.dispose();
    }
}
