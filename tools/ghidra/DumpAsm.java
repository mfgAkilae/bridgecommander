// Dump disassembly (address, bytes, text) of the functions containing the given addresses.
// Args: <outfile> <hexaddr> [<hexaddr> ...]
// @category Export
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpAsm extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter pw = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 1; i < args.length; i++) {
                Address a = toAddr(Long.parseLong(args[i].replace("0x", ""), 16));
                Function f = getFunctionContaining(a);
                if (f == null) {
                    pw.println(";; no function at " + a);
                    continue;
                }
                pw.println(";;==== " + f.getName() + " @ " + f.getEntryPoint());
                InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (it.hasNext()) {
                    Instruction ins = it.next();
                    StringBuilder hex = new StringBuilder();
                    for (byte b : ins.getBytes()) {
                        hex.append(String.format("%02x", b & 0xff));
                    }
                    pw.println(String.format("%s  %-20s %s", ins.getAddress(), hex, ins.toString()));
                }
                pw.println();
            }
        }
        println("asm written");
    }
}
