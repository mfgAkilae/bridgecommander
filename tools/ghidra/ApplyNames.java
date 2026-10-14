// Create functions at listed addresses and apply names; then derive names of C++
// methods called by SWIG wrappers (py_*).
// @category Export
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.SourceType;

import java.io.BufferedReader;
import java.io.FileReader;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

public class ApplyNames extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String path = getScriptArgs()[0];
        FunctionManager fm = currentProgram.getFunctionManager();
        int created = 0, renamed = 0;
        try (BufferedReader br = new BufferedReader(new FileReader(path))) {
            String line;
            while ((line = br.readLine()) != null) {
                String[] p = line.trim().split("\\s+", 2);
                if (p.length < 2) continue;
                Address a = toAddr(Long.parseLong(p[0], 16));
                Function f = fm.getFunctionAt(a);
                if (f == null) {
                    disassemble(a);
                    CreateFunctionCmd cmd = new CreateFunctionCmd(a);
                    if (cmd.applyTo(currentProgram, monitor)) {
                        created++;
                    }
                    f = fm.getFunctionAt(a);
                }
                if (f != null && f.getName().startsWith("FUN_")) {
                    try {
                        f.setName(p[1], SourceType.USER_DEFINED);
                        renamed++;
                    } catch (Exception e) {
                        try {
                            f.setName(p[1] + "_" + p[0], SourceType.USER_DEFINED);
                            renamed++;
                        } catch (Exception e2) {
                            // ignore
                        }
                    }
                }
            }
        }
        println("created " + created + " renamed " + renamed);

        // Count how often each callee is used by py_ wrappers
        Map<Function, Integer> useCount = new HashMap<>();
        Map<Function, Set<Function>> wrapperCallees = new HashMap<>();
        for (Function f : fm.getFunctions(true)) {
            if (!f.getName().startsWith("py_")) continue;
            Set<Function> callees = f.getCalledFunctions(monitor);
            wrapperCallees.put(f, callees);
            for (Function c : callees) {
                useCount.merge(c, 1, Integer::sum);
            }
        }
        int derived = 0;
        for (Map.Entry<Function, Set<Function>> e : wrapperCallees.entrySet()) {
            Set<Function> rest = new HashSet<>();
            for (Function c : e.getValue()) {
                if (useCount.getOrDefault(c, 0) < 25) rest.add(c);
            }
            if (rest.size() == 1) {
                Function c = rest.iterator().next();
                if (c.getName().startsWith("FUN_")) {
                    String nm = e.getKey().getName().substring(3);
                    try {
                        c.setName("m_" + nm, SourceType.USER_DEFINED);
                        derived++;
                    } catch (Exception ex) {
                        // duplicate etc.
                    }
                }
            }
        }
        println("derived " + derived);
    }
}
