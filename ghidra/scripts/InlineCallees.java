// InlineCallees.java: read-only: for the functions at the given addresses, count inline-flagged callees.
// Args: <outfile> <file with one address per line>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.nio.file.*;
import java.util.*;

public class InlineCallees extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Map<String, Integer> cnt = new TreeMap<>();
        for (String l : Files.readAllLines(Path.of(a[1]))) {
            Function f = getFunctionAt(toAddr(l.trim()));
            if (f == null) continue;
            for (Function c : f.getCalledFunctions(monitor))
                if (c.isInline()) cnt.merge(c.getEntryPoint() + " " + c.getName(), 1, Integer::sum);
        }
        PrintWriter out = new PrintWriter(a[0]);
        cnt.entrySet().stream().sorted((x, y) -> y.getValue() - x.getValue())
            .forEach(e -> out.println(e.getValue() + " " + e.getKey()));
        out.close();
    }
}
