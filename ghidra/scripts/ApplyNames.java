// ApplyNames.java — bulk-rename functions in the current program from a text file.
// Input (script arg 0): lines of "0xADDRESS name". Renames the function at ADDRESS,
// or creates a user label if no function is there. Symbol-naming only; it does not
// read, modify, or interact with any device. Used for the Bryton S510 reconstruction.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.io.BufferedReader;
import java.io.FileReader;

public class ApplyNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) { println("ApplyNames: need a names-file argument"); return; }
        BufferedReader br = new BufferedReader(new FileReader(args[0]));
        int renamed = 0, labeled = 0, failed = 0;
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] p = line.split("\\s+", 2);
            if (p.length < 2) continue;
            String hex = p[0].replaceFirst("0[xX]", "");
            String name = p[1].trim();
            Address addr = currentProgram.getAddressFactory()
                               .getDefaultAddressSpace().getAddress(Long.parseLong(hex, 16));
            try {
                Function f = getFunctionAt(addr);
                if (f != null) {
                    f.setName(name, SourceType.USER_DEFINED);
                    renamed++;
                } else {
                    createLabel(addr, name, true, SourceType.USER_DEFINED);
                    labeled++;
                    println("no function at " + addr + " -> label " + name);
                }
            } catch (Exception e) {
                failed++;
                println("FAILED " + name + " @ " + addr + ": " + e.getMessage());
            }
        }
        br.close();
        println("ApplyNames done: renamed=" + renamed + " labeled=" + labeled + " failed=" + failed);
    }
}
