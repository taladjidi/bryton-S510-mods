// EwStructs.java — build EW_<Class> structures for the Embedded Wizard classes and type `this` of their methods.
//   size     = descriptor word[4]
//   super    = EW_<Ancestor> at offset 0
//   members  = "<M>__Init(param_1 + 0xOFF, ..." calls in <Class>__Init (also via "x = param_1 + 0xOFF;")
//   methods  = functions named <Class>__* / <Class>_* (longest class-prefix wins): param 0 -> EW_<Class> *
// The ~35 __Init that fail with "Bad partial clone" are decompiled with all inline flags cleared (see Dump.java u:).
// Args: <report file> <EW_CLASSES.csv> [APPLY]   — without APPLY nothing is changed (run with -readOnly anyway).
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.Function;
import ghidra.program.model.pcode.HighFunctionDBUtil;
import ghidra.program.model.symbol.SourceType;
import java.io.PrintWriter;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;

public class EwStructs extends GhidraScript {
    Map<String, String> anc = new HashMap<>();
    Map<String, Integer> size = new HashMap<>();
    Map<String, TreeMap<Integer, String>> members = new HashMap<>();
    Map<String, Structure> built = new HashMap<>();
    DataTypeManager dtm;
    CategoryPath cat = new CategoryPath("/EW");
    PrintWriter out;

    // in source order: "x = param_1 + N;" assignments and "[outl_|wrap_]<M>__Init[_dADDR](arg," calls
    static final Pattern TOK = Pattern.compile(
        "(\\w+) = (?:\\(\\w+ \\*\\))?param_1 \\+ (0x[0-9a-f]+|\\d+);"
        + "|(?:outl_|wrap_)?(\\w+?)__Init(?:_[0-9a-f]{8})?\\((?:param_1 \\+ (0x[0-9a-f]+|\\d+)|(\\w+)),");
    static final Pattern SIG = Pattern.compile("\\((\\w+) \\*param_1[,)]");

    void parse(String cls, String c) {
        int scale = 1;                                   // param_1 typed int* / short* -> offsets are scaled
        Matcher t = SIG.matcher(c);
        if (t.find()) scale = t.group(1).matches("int|uint|undefined4") ? 4 : t.group(1).matches("short|ushort|undefined2") ? 2 : 1;
        Map<String, Integer> vars = new HashMap<>();
        TreeMap<Integer, String> m = new TreeMap<>();
        Matcher k = TOK.matcher(c);
        while (k.find()) {
            if (k.group(1) != null) { vars.put(k.group(1), Integer.decode(k.group(2)) * scale); continue; }
            Integer off = k.group(4) != null ? Integer.valueOf(Integer.decode(k.group(4)) * scale) : vars.get(k.group(5));
            if (off != null && off > 0 && size.containsKey(k.group(3))) m.put(off, k.group(3));
        }
        members.put(cls, m);
    }

    Structure build(String cls) {
        if (built.containsKey(cls)) return built.get(cls);
        Structure s = new StructureDataType(cat, "EW_" + cls, size.get(cls), dtm);
        built.put(cls, s);                               // placeholder against cycles
        String a = anc.get(cls);
        if (size.containsKey(a) && size.get(a) <= size.get(cls))
            s.replaceAtOffset(0, build(a), size.get(a), "super", null);
        for (var e : members.get(cls).entrySet()) {
            Structure ms = build(e.getValue());
            int off = e.getKey();
            if (off + ms.getLength() > s.getLength()) { out.println("  skip " + cls + "+" + off + " " + e.getValue() + " (too big)"); continue; }
            try { s.replaceAtOffset(off, ms, ms.getLength(), "m" + Integer.toHexString(off) + "_" + e.getValue(), null); }
            catch (Exception x) { out.println("  skip " + cls + "+" + off + " " + e.getValue() + " " + x.getMessage()); }
        }
        return s;
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        out = new PrintWriter(a[0]);
        boolean apply = a.length > 2 && a[2].equals("APPLY");
        dtm = currentProgram.getDataTypeManager();
        Map<String, String> init = new LinkedHashMap<>();
        for (String l : Files.readAllLines(Path.of(a[1])).subList(1, Files.readAllLines(Path.of(a[1])).size())) {
            String[] p = l.split(",");
            anc.put(p[1], p[2]);
            init.put(p[1], p[3]);
            size.put(p[1], getInt(toAddr(p[0]).add(16)));
        }
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        List<String> failed = new ArrayList<>();
        for (var e : init.entrySet()) {
            Function f = getFunctionAt(toAddr(e.getValue()));
            if (f == null) { members.put(e.getKey(), new TreeMap<>()); out.println("no __Init function: " + e.getKey()); continue; }
            var r = di.decompileFunction(f, 60, monitor);
            if (r.decompileCompleted()) parse(e.getKey(), r.getDecompiledFunction().getC()); else failed.add(e.getKey());
        }
        for (String cls : failed) {                       // un-inline only what the fall-through target pulls in
            Function f = getFunctionAt(toAddr(init.get(cls)));
            Deque<Function> q = new ArrayDeque<>();
            for (var rg : f.getBody()) { Function n = getFunctionAt(rg.getMaxAddress().next()); if (n != null && n != f) q.add(n); }
            Set<Function> seen = new HashSet<>();
            List<Function> un = new ArrayList<>();
            while (!q.isEmpty()) {
                Function g = q.pop();
                if (!seen.add(g)) continue;
                for (Function c : g.getCalledFunctions(monitor)) {
                    if (c.isInline() && !f.getCalledFunctions(monitor).contains(c)) { c.setInline(false); un.add(c); }
                    q.add(c);
                }
            }
            var r = di.decompileFunction(f, 60, monitor);
            for (Function c : un) c.setInline(true);
            if (!r.decompileCompleted()) {                  // fallback: everything un-inlined (loses outlined args)
                for (Function c : currentProgram.getFunctionManager().getFunctions(true)) if (c.isInline()) { c.setInline(false); un.add(c); }
                r = di.decompileFunction(f, 60, monitor);
                for (Function c : un) c.setInline(true);
                out.println("init needed full un-inline: " + cls);
            }
            if (r.decompileCompleted()) parse(cls, r.getDecompiledFunction().getC());
            else { members.put(cls, new TreeMap<>()); out.println("init still fails: " + cls); }
        }

        int nm = 0;
        for (String cls : init.keySet()) { build(cls); nm += members.get(cls).size(); }
        out.println("# structs " + built.size() + ", members " + nm + ", __Init retried un-inlined " + failed.size());
        for (String cls : init.keySet()) {
            out.println(cls + " size=0x" + Integer.toHexString(size.get(cls)) + " super=" + anc.get(cls));
            for (var e : members.get(cls).entrySet()) out.println("  +0x" + Integer.toHexString(e.getKey()) + " " + e.getValue());
        }

        // methods: longest class prefix followed by "_"
        List<String> byLen = new ArrayList<>(init.keySet());
        byLen.sort((x, y) -> y.length() - x.length());
        Map<Function, String> methods = new LinkedHashMap<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f.isThunk() || f.isInline()) continue;
            for (String cls : byLen) if (f.getName().startsWith(cls + "_")) { methods.put(f, cls); break; }
        }
        out.println("# methods to type: " + methods.size());
        if (!apply) { out.close(); return; }

        for (String cls : init.keySet()) dtm.addDataType(built.get(cls), DataTypeConflictHandler.REPLACE_HANDLER);
        int ok = 0;
        for (var e : methods.entrySet()) {
            Function f = e.getKey();
            DataType ptr = new PointerDataType(dtm.getDataType(cat, "EW_" + e.getValue()), dtm);
            try {
                if (f.getParameterCount() == 0) {
                    var r = di.decompileFunction(f, 60, monitor);
                    if (!r.decompileCompleted()) { out.println("no decompile: " + f.getName()); continue; }
                    HighFunctionDBUtil.commitParamsToDatabase(r.getHighFunction(), true,
                        HighFunctionDBUtil.ReturnCommitOption.NO_COMMIT, SourceType.ANALYSIS);
                }
                if (f.getParameterCount() == 0) { out.println("no params: " + f.getName()); continue; }
                f.getParameter(0).setDataType(ptr, SourceType.USER_DEFINED);
                f.getParameter(0).setName("_this", SourceType.USER_DEFINED);
                ok++;
            } catch (Exception x) { out.println("fail " + f.getName() + ": " + x.getMessage()); }
        }
        out.println("# typed " + ok);
        out.close();
    }
}
