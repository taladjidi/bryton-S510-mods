// BsimMatch.java: read-only: query every function of the current program against a BSim
// database of compiled open-source libraries; write the best matches as TSV for review.
// Args: <bsimURL e.g. file:/.../libs> <out.tsv> [simThreshold=0.7] [0xADDR,0xADDR,... only these] [ALLNOTES]
// ALLNOTES: also write <out.tsv>.all with every (addr, lib_func, sim, signif) note (max 50 per function).
// Columns: addr, current_name, lib_exe, lib_func, similarity, significance, runner_up, ru_sim
// Nothing is renamed here; accepted matches go through names/*.txt + ApplyNames.java.
import ghidra.app.script.GhidraScript;
import ghidra.features.bsim.query.*;
import ghidra.features.bsim.query.description.*;
import ghidra.features.bsim.query.protocol.*;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.util.*;

public class BsimMatch extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        double thresh = a.length > 2 ? Double.parseDouble(a[2]) : 0.7;
        PrintWriter out = new PrintWriter(a[1]);
        boolean all = a.length > 4 && a[4].equals("ALLNOTES");
        PrintWriter allOut = all ? new PrintWriter(a[1] + ".all") : null;
        out.println("addr\tcurrent\tlib\tfunc\tsim\tsignif\trunner_up\tru_sim");
        try (FunctionDatabase db = BSimClientFactory.buildClient(BSimClientFactory.deriveBSimURL(a[0]), false)) {
            if (!db.initialize()) { println(db.getLastError().message); return; }
            List<Function> funcs = new ArrayList<>();
            if (a.length > 3) for (String x : a[3].split(",")) funcs.add(getFunctionAt(toAddr(x)));
            else for (Function f : currentProgram.getFunctionManager().getFunctions(true))
                if (!f.isThunk() && f.getBody().getNumAddresses() >= 16) funcs.add(f);
            int batch = 500, hits = 0;
            for (int i = 0; i < funcs.size(); i += batch) {
                GenSignatures gs = new GenSignatures(false);
                try {
                    gs.setVectorFactory(db.getLSHVectorFactory());
                    gs.openProgram(currentProgram, null, null, null, null, null);
                    gs.scanFunctions(funcs.subList(i, Math.min(i + batch, funcs.size())).iterator(),
                                     funcs.size(), monitor);
                    QueryNearest q = new QueryNearest();
                    q.manage = gs.getDescriptionManager();
                    q.max = all ? 50 : 3; q.thresh = thresh; q.signifthresh = 0.0;
                    ResponseNearest resp = q.execute(db);
                    if (resp == null) { println(db.getLastError().message); return; }
                    for (SimilarityResult sim : resp.result) {
                        List<SimilarityNote> notes = new ArrayList<>();
                        sim.iterator().forEachRemaining(notes::add);
                        if (notes.isEmpty()) continue;
                        if (all) for (SimilarityNote nt : notes)
                            allOut.printf("0x%08x\t%s\t%.3f\t%.1f%n", sim.getBase().getAddress(),
                                nt.getFunctionDescription().getFunctionName(), nt.getSimilarity(), nt.getSignificance());
                        notes.sort((x, y) -> Double.compare(y.getSimilarity(), x.getSimilarity()));
                        SimilarityNote b = notes.get(0);
                        FunctionDescription base = sim.getBase();
                        String ru = notes.size() > 1 ? notes.get(1).getFunctionDescription().getFunctionName() : "-";
                        String ruSim = notes.size() > 1 ? String.format("%.3f", notes.get(1).getSimilarity()) : "-";
                        out.printf("0x%08x\t%s\t%s\t%s\t%.3f\t%.1f\t%s\t%s%n", base.getAddress(), base.getFunctionName(),
                            b.getFunctionDescription().getExecutableRecord().getNameExec(),
                            b.getFunctionDescription().getFunctionName(), b.getSimilarity(), b.getSignificance(), ru, ruSim);
                        hits++;
                    }
                } finally { gs.dispose(); }
                println("BsimMatch: " + Math.min(i + batch, funcs.size()) + "/" + funcs.size() + " hits=" + hits);
            }
        }
        out.close();
        if (allOut != null) allOut.close();
    }
}
