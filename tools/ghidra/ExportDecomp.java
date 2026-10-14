// Decompile every function in the program into one text file (parallel).
// @category Export
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public class ExportDecomp extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args.length > 0 ? args[0] : "decomp.c";
        int threads = args.length > 1 ? Integer.parseInt(args[1]) : 8;

        List<Function> funcs = new ArrayList<>();
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
            funcs.add(it.next());
        }
        println("functions: " + funcs.size());

        ConcurrentHashMap<Long, String> results = new ConcurrentHashMap<>();
        AtomicInteger done = new AtomicInteger();
        ExecutorService pool = Executors.newFixedThreadPool(threads);
        int chunk = (funcs.size() + threads - 1) / threads;
        for (int t = 0; t < threads; t++) {
            final int from = t * chunk;
            final int to = Math.min(funcs.size(), from + chunk);
            pool.submit(() -> {
                DecompInterface di = new DecompInterface();
                DecompileOptions opts = new DecompileOptions();
                di.setOptions(opts);
                di.toggleCCode(true);
                di.toggleSyntaxTree(false);
                di.openProgram(currentProgram);
                ConsoleTaskMonitor mon = new ConsoleTaskMonitor();
                for (int i = from; i < to; i++) {
                    Function f = funcs.get(i);
                    String text;
                    try {
                        DecompileResults r = di.decompileFunction(f, 60, mon);
                        if (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null) {
                            text = r.getDecompiledFunction().getC();
                        } else {
                            text = "/* decompile failed: " + (r == null ? "null" : r.getErrorMessage()) + " */\n";
                        }
                    } catch (Exception e) {
                        text = "/* exception: " + e + " */\n";
                    }
                    results.put(f.getEntryPoint().getOffset(),
                        "//==== " + f.getName() + " @ " + f.getEntryPoint() + "\n" + text + "\n");
                    int d = done.incrementAndGet();
                    if (d % 1000 == 0) {
                        System.out.println("decompiled " + d);
                    }
                }
                di.dispose();
            });
        }
        pool.shutdown();
        pool.awaitTermination(10, TimeUnit.HOURS);

        try (PrintWriter pw = new PrintWriter(new FileWriter(new File(outPath)))) {
            for (Function f : funcs) {
                String s = results.get(f.getEntryPoint().getOffset());
                if (s != null) {
                    pw.print(s);
                }
            }
        }
        println("wrote " + outPath);
    }
}
