// FileMap.java: read-only: for each string naming a source file (*.c/*.cpp/*.h), list how many
// functions reference it and their address span. Gives a rough module -> address-range map.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.util.*;
import java.util.regex.Pattern;

public class FileMap extends GhidraScript {
    @Override
    public void run() throws Exception {
        PrintWriter out = new PrintWriter(getScriptArgs()[0]);
        Pattern p = Pattern.compile("([A-Za-z0-9_\\-]+\\.(c|cpp|h))\\b");
        Map<String, TreeSet<Long>> m = new TreeMap<>();
        for (Data d : currentProgram.getListing().getDefinedData(true)) {
            if (!(d.getValue() instanceof String)) continue;
            var mt = p.matcher((String) d.getValue());
            if (!mt.find()) continue;
            String file = mt.group(1);
            List<Address> froms = new ArrayList<>();
            for (var r : getReferencesTo(d.getAddress())) {
                froms.add(r.getFromAddress());
                for (var r2 : getReferencesTo(r.getFromAddress())) froms.add(r2.getFromAddress());
            }
            for (Address a : froms) {
                Function f = getFunctionContaining(a);
                if (f != null) m.computeIfAbsent(file, k -> new TreeSet<>()).add(f.getEntryPoint().getOffset());
            }
            m.computeIfAbsent(file, k -> new TreeSet<>());
        }
        for (var e : m.entrySet()) {
            var s = e.getValue();
            out.printf("%-28s funcs=%-4d %s%n", e.getKey(), s.size(),
                s.isEmpty() ? "-" : String.format("0x%08x..0x%08x", s.first(), s.last()));
        }
        out.close();
    }
}
