// Unname.java — undo bad generated names. Args: <file of "0xADDR name" lines>. For each line, if the
// symbol at ADDR has exactly that name: remove the function created there (if any) and the label,
// then clear the bogus disassembly so the bytes revert to undefined data. Touches nothing else.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import java.io.BufferedReader;
import java.io.FileReader;

public class Unname extends GhidraScript {
    @Override
    public void run() throws Exception {
        BufferedReader br = new BufferedReader(new FileReader(getScriptArgs()[0]));
        int n = 0;
        for (String line; (line = br.readLine()) != null; ) {
            String[] p = line.trim().split("\\s+");
            if (p.length < 2 || !p[0].startsWith("0x")) continue;
            Address a = toAddr(Long.decode(p[0]));
            Function f = getFunctionAt(a);
            if (f != null && f.getName().equals(p[1])) {
                Address end = f.getBody().getMaxAddress();
                removeFunction(f);
                clearListing(a, end);
                n++;
            }
            for (Symbol s : currentProgram.getSymbolTable().getSymbols(a))
                if (s.getName().equals(p[1])) { s.delete(); n++; }
        }
        br.close();
        println("Unname done: " + n);
    }
}
