// ApplyScriptApiNames.java: creates and names the engine's script API functions.
//
// What it is for
//   The Ben 10: Protector of Earth (SLUS-21661) executable has a table of
//   {pointer to name, pointer to function} pairs that the engine uses to tie
//   script names ("ass_...") to native functions (findings/12).
//   Those functions are only called through pointers, so Ghidra's
//   auto-analysis misses many of them. Without them, PS2Recomp generates
//   unresolved calls.
//
// What it does
//   1. Searches the loaded data blocks for the longest run of pairs
//      (pointer to a string starting with "ass", pointer into an executable
//      block). Requires at least MIN_RUN pairs, so it does not pick up junk.
//   2. For each pair: if there is no function at the target, disassembles
//      and creates one.
//   3. Gives the function the string's name (source USER_DEFINED, only if
//      the current name is automatic, like FUN_xxxxxxxx, so a name given by
//      hand is never erased).
//   4. Writes a summary to the log: pairs, functions created, renamed.
//
// The names come from the user's own executable: nothing from the game is
// stored in this script.
//
// @category Ben10Recomp

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class ApplyScriptApiNames extends GhidraScript {

    // Fewer than this is not the script table (the real table has ~1,600 pairs).
    private static final int MIN_RUN = 100;

    @Override
    protected void run() throws Exception {
        Memory mem = currentProgram.getMemory();

        long bestStart = -1;
        int bestLen = 0;

        // Scans the initialised blocks that are not executable (.data/.rodata).
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isInitialized() || blk.isExecute()) {
                continue;
            }
            long start = blk.getStart().getOffset();
            long end = blk.getEnd().getOffset();
            // 4-byte aligned: the table is made of 32-bit words.
            for (long a = (start + 3) & ~3L; a + 8 <= end; a += 4) {
                int len = 0;
                long p = a;
                while (p + 8 <= end && isPair(p)) {
                    len++;
                    p += 8;
                }
                if (len > bestLen) {
                    bestLen = len;
                    bestStart = a;
                }
                if (len > 0) {
                    a = p - 4; // skip the run already measured
                }
            }
        }

        if (bestLen < MIN_RUN) {
            printerr("Script API table not found (longest run: " + bestLen + ")");
            return;
        }
        println(String.format("Script API table at 0x%08X, %d pairs", bestStart, bestLen));

        int created = 0, renamed = 0, kept = 0, failed = 0;
        for (int i = 0; i < bestLen; i++) {
            Address pairAddr = toAddr(bestStart + 8L * i);
            Address nameAddr = toAddr(mem.getInt(pairAddr) & 0xFFFFFFFFL);
            Address target = toAddr(mem.getInt(pairAddr.add(4)) & 0xFFFFFFFFL);
            String name = readCString(nameAddr);

            Function f = getFunctionAt(target);
            if (f == null) {
                // A function may have "swallowed" this address by mistake;
                // we do not touch it here, we only log it.
                Function containing = getFunctionContaining(target);
                if (containing != null) {
                    println(String.format("warning: 0x%08X (%s) is inside %s",
                            target.getOffset(), name, containing.getName()));
                }
                if (getInstructionAt(target) == null) {
                    new DisassembleCommand(target, null, true).applyTo(currentProgram, monitor);
                }
                f = createFunction(target, null);
                if (f == null) {
                    failed++;
                    printerr(String.format("failed to create a function at 0x%08X (%s)", target.getOffset(), name));
                    continue;
                }
                created++;
            }
            SourceType src = f.getSymbol().getSource();
            if (src == SourceType.DEFAULT || src == SourceType.ANALYSIS) {
                f.setName(name, SourceType.USER_DEFINED);
                renamed++;
            } else {
                kept++;
            }
        }
        println(String.format("created=%d renamed=%d kept=%d failed=%d",
                created, renamed, kept, failed));
    }

    // A valid pair: +0 points to a string starting with "ass",
    // +4 points into an executable block.
    private boolean isPair(long a) {
        try {
            Memory mem = currentProgram.getMemory();
            long namePtr = mem.getInt(toAddr(a)) & 0xFFFFFFFFL;
            long codePtr = mem.getInt(toAddr(a + 4)) & 0xFFFFFFFFL;
            MemoryBlock cb = mem.getBlock(toAddr(codePtr));
            if (cb == null || !cb.isExecute() || (codePtr & 3) != 0) {
                return false;
            }
            byte[] b = new byte[3];
            mem.getBytes(toAddr(namePtr), b);
            return b[0] == 'a' && b[1] == 's' && b[2] == 's';
        } catch (Exception e) {
            return false;
        }
    }

    private String readCString(Address a) throws Exception {
        StringBuilder sb = new StringBuilder();
        Memory mem = currentProgram.getMemory();
        for (int i = 0; i < 256; i++) {
            byte c = mem.getByte(a.add(i));
            if (c == 0) {
                break;
            }
            sb.append((char) c);
        }
        return sb.toString();
    }
}
