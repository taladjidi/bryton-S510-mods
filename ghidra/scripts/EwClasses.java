// EwClasses.java — read-only: walk Embedded Wizard class descriptors ("CLAS" magic) and emit
// candidate names as "0xADDR name" lines (arg 0 = names file, arg 1 = class table csv).
//   desc[1] = "Unit::Class"  -> C prefix "UnitClass" (EW's own generated-code convention)
//   desc[11..13] = _Init / _ReInit / _Done  -> <Prefix>__Init / __ReInit / __Done (original names)
//   desc[14..]   = virtual slots; a slot that differs from the ancestor's is named <Prefix>_vm<k>
// Optional arg 2: slots TSV (class, ancestor, slot index k (0=_Init), function address) for vm renaming.
// Nothing is renamed here; feed the output to ApplyNames.java.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import java.io.PrintWriter;
import java.util.*;

public class EwClasses extends GhidraScript {
    static final int MAGIC = 0x434c4153; // "SALC" little-endian
    Memory mem;

    long u32(long a) throws Exception { return mem.contains(toAddr(a)) ? mem.getInt(toAddr(a)) & 0xffffffffL : 0; }

    boolean isCode(long v) { // Thumb code pointer (odd) in SDRAM code or ITCM
        return (v & 1) == 1 && ((v >= 0xd0100000L && v < 0xd02c0000L) || (v >= 0xd0340000L && v < 0xd03d8000L) || (v >= 0x1000L && v < 0xfff0L));  // ITCM 0x0-0xfff is a data table
    }

    String cstr(long a) throws Exception {
        if (!mem.contains(toAddr(a))) return null;
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 96; i++) { int c = mem.getByte(toAddr(a + i)) & 0xff; if (c == 0) return sb.toString(); sb.append((char) c); }
        return null;
    }

    long[] trim(long a, Map<Long, long[]> raw, Map<Long, Long> anc, Map<Long, long[]> memo) {
        if (memo.containsKey(a)) return memo.get(a);
        long[] r = raw.get(a);
        long p = anc.getOrDefault(a, 0L);
        int n;
        if (raw.containsKey(p) && p != a) {
            n = Math.min(r.length, trim(p, raw, anc, memo).length);
            while (n < r.length && r[n] != 0 && isCode(r[n])) n++;
        } else {                       // root class (base is EW's built-in XObject): keep, drop trailing zeros
            n = r.length;
            while (n > 3 && r[n - 1] == 0) n--;
        }
        long[] t = Arrays.copyOf(r, n);
        memo.put(a, t);
        return t;
    }

    @Override
    public void run() throws Exception {
        mem = currentProgram.getMemory();
        String[] args = getScriptArgs();
        Map<Long, String> prefix = new HashMap<>();
        Map<Long, Long> anc = new HashMap<>();
        Map<Long, long[]> slots = new HashMap<>();  // raw, untrimmed
        List<Long> order = new ArrayList<>();
        long start = 0xd0000000L, end = 0xd03d7000L;
        for (long a = start; a < end; a += 4) {
            if (mem.getInt(toAddr(a)) != MAGIC) continue;
            String nm = cstr(u32(a + 4));
            if (nm == null || !nm.contains("::")) continue;
            prefix.put(a, nm.replace("::", ""));
            anc.put(a, u32(a + 8));
            List<Long> s = new ArrayList<>();
            for (int k = 11; k < 11 + 200; k++) {
                long v = u32(a + 4L * k);
                if (k > 13 && v != 0 && !isCode(v)) break;
                if (k <= 13 && !isCode(v)) break;
                s.add(v);
            }
            slots.put(a, s.stream().mapToLong(Long::longValue).toArray());
            order.add(a);
        }
        // Trim each vtable: inherit the ancestor's slot count; beyond it, only consecutive non-zero code
        // pointers are new methods (zeros / data after the table were being read as slots).
        Map<Long, long[]> trimmed = new HashMap<>();
        for (long a : order) trim(a, slots, anc, trimmed);
        slots = trimmed;
        PrintWriter out = new PrintWriter(args[0]);
        PrintWriter tbl = new PrintWriter(args[1]);
        tbl.println("descriptor,class,ancestor,init,slots");
        Set<Long> named = new HashSet<>();
        String[] base = {"__Init", "__ReInit", "__Done"};
        int n = 0;
        for (long a : order) {
            String p = prefix.get(a);
            long[] s = slots.get(a);
            long[] as = slots.get(anc.get(a));
            out.printf("0x%08x EwClass_%s%n", a, p);
            tbl.printf("0x%08x,%s,%s,0x%08x,%d%n", a, p, prefix.getOrDefault(anc.get(a), String.format("0x%x", anc.get(a))),
                       s.length > 0 ? s[0] & ~1L : 0, s.length);
            for (int k = 0; k < s.length; k++) {
                long f = s[k] & ~1L;
                if (s[k] == 0) continue;
                boolean inherited = as != null && k < as.length && as[k] == s[k];
                if (inherited || !named.add(f)) continue;
                String name = k < 3 ? p + base[k] : p + "_vm" + (k - 3);
                out.printf("0x%08x %s%n", f, name);
                n++;
            }
        }
        if (args.length > 2) {
            PrintWriter st = new PrintWriter(args[2]);
            for (long a : order) {
                long[] sl = slots.get(a);
                for (int k = 0; k < sl.length; k++)
                    st.printf("%s\t%s\t%d\t0x%08x%n", prefix.get(a), prefix.getOrDefault(anc.get(a), "-"), k, sl[k] & ~1L);
            }
            st.close();
        }
        out.close(); tbl.close();
        println("EwClasses: classes=" + order.size() + " function names=" + n);
    }
}
