// Create functions at code addresses referenced by pointer tables (vtables etc.)
// that Ghidra did not turn into functions.
// @category Export
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.listing.FunctionManager;

import java.util.TreeSet;

public class CreateFromPointers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        MemoryBlock text = mem.getBlock(".text");
        long tlo = text.getStart().getOffset();
        long thi = text.getEnd().getOffset();
        TreeSet<Long> cands = new TreeSet<>();
        for (String bn : new String[] {".rdata", ".data"}) {
            MemoryBlock b = mem.getBlock(bn);
            if (b == null) continue;
            long lo = b.getStart().getOffset();
            long hi = b.getEnd().getOffset();
            for (long a = lo; a + 4 <= hi; a += 4) {
                int v;
                try {
                    v = mem.getInt(toAddr(a));
                } catch (Exception e) {
                    continue;
                }
                long t = v & 0xffffffffL;
                if (t < tlo || t > thi) continue;
                if (t >= 0x800000L && t < 0x800200L) continue; // event-type constants
                cands.add(t);
            }
        }
        int created = 0, skipped = 0;
        for (long t : cands) {
            Address a = toAddr(t);
            if (fm.getFunctionContaining(a) != null) continue;
            int prev;
            try {
                prev = mem.getByte(toAddr(t - 1)) & 0xff;
            } catch (Exception e) {
                continue;
            }
            boolean boundary = (t % 16 == 0) || prev == 0xC3 || prev == 0xCC || prev == 0x90
                || (t >= 3 && (mem.getByte(toAddr(t - 3)) & 0xff) == 0xC2);
            if (!boundary) {
                skipped++;
                continue;
            }
            disassemble(a);
            if (getInstructionAt(a) == null) {
                skipped++;
                continue;
            }
            CreateFunctionCmd cmd = new CreateFunctionCmd(a);
            if (cmd.applyTo(currentProgram, monitor)) created++;
        }
        println("pointer candidates " + cands.size() + " created " + created + " skipped " + skipped);
    }
}
