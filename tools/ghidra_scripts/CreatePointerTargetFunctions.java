// CreatePointerTargetFunctions.java: creates the functions that are only
// reached through pointers stored in data (virtual tables, callback tables).
//
// What it is for
//   Ghidra's auto-analysis finds functions by following calls. A function
//   that is never called directly, only through a pointer kept in .data or
//   .rodata (a C++ virtual table, a callback table), is easy to miss. If the
//   function list misses it, PS2Recomp generates no code for it and the
//   runtime stops on the first indirect call to it ("guest-branch:
//   missing-target", findings/15 s.10).
//
// What it does
//   1. Scans every initialised, non-executable block 4 bytes at a time.
//   2. Keeps the words that point, 4-byte aligned, into an executable block,
//      at an address that is not already inside a function.
//   3. Accepts a target only if it looks like a function start:
//      a. its first instruction is a stack-frame prologue,
//         "addiu $sp, $sp, -N" (0x27BDxxxx with a negative immediate): how
//         the game's compiler starts every non-leaf function; or
//      b. it follows a "jr $ra" and its delay slot, with at most 3 zero
//         words (nop padding) in between: a leaf function (no stack frame)
//         placed right after the previous function's return.
//      Anything else is rejected: pointers that land in a switch's jump
//      targets, in the data table at the start of .text (findings/12 s.6),
//      or that are just numbers.
//   4. Disassembles the target if needed and creates a function there.
//   5. Prints a summary, and the targets it rejected (not in a function, but
//      no prologue), so leaf functions can be checked by hand.
//
// Nothing from the game is stored in this script: it only reads the user's
// own executable.
//
// @category Ben10Recomp

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.util.TreeSet;

public class CreatePointerTargetFunctions extends GhidraScript {

    @Override
    protected void run() throws Exception {
        Memory mem = currentProgram.getMemory();

        // Distinct candidate targets, sorted, so the log is stable run to run.
        TreeSet<Long> targets = new TreeSet<>();
        for (MemoryBlock blk : mem.getBlocks()) {
            // Only data the game actually has in RAM: skip non-loaded blocks
            // (ELF headers, the .DVP overlay sections, debug sections).
            if (!blk.isInitialized() || blk.isExecute()
                    || !blk.getStart().getAddressSpace().isLoadedMemorySpace()) {
                continue;
            }
            Address base = blk.getStart();
            long size = blk.getSize();
            for (long off = (4 - (base.getOffset() & 3)) & 3; off + 4 <= size; off += 4) {
                long v;
                try {
                    v = mem.getInt(base.add(off)) & 0xFFFFFFFFL;
                } catch (Exception e) {
                    break; // unreadable block: nothing to scan
                }
                if ((v & 3) != 0) {
                    continue;
                }
                // A 32-bit value read from data is an address in the default
                // (RAM) space; Ghidra's toAddr() maps it there.
                MemoryBlock tb = mem.getBlock(toAddr(v));
                if (tb == null || !tb.isExecute() || !tb.isInitialized()) {
                    continue;
                }
                targets.add(v);
            }
        }

        int created = 0, already = 0, rejected = 0, failed = 0;
        for (long v : targets) {
            Address target = toAddr(v);
            if (getFunctionContaining(target) != null) {
                already++;
                continue;
            }
            int first;
            try {
                first = mem.getInt(target);
            } catch (Exception e) {
                failed++;
                continue;
            }
            // addiu $sp, $sp, imm: opcode 0x09, rs = rt = 29 -> 0x27BD in the
            // top half; a frame allocation has a negative immediate.
            boolean prologue = (first >>> 16) == 0x27BD && (first & 0x8000) != 0;
            if (!prologue && !followsReturn(mem, v)) {
                rejected++;
                println(String.format("rejected (no function start): 0x%08X", v));
                continue;
            }
            if (getInstructionAt(target) == null) {
                new DisassembleCommand(target, null, true).applyTo(currentProgram, monitor);
            }
            Function f = createFunction(target, null);
            if (f == null) {
                failed++;
                printerr(String.format("failed to create a function at 0x%08X", v));
                continue;
            }
            created++;
        }
        println(String.format("pointer targets=%d created=%d already_in_function=%d rejected=%d failed=%d",
                targets.size(), created, already, rejected, failed));
    }

    // True when "jr $ra" (0x03E00008) sits 2..5 words before v, and every word
    // between its delay slot and v is zero (nop padding between functions).
    private boolean followsReturn(Memory mem, long v) {
        try {
            for (long back = 8; back <= 20; back += 4) {
                if (mem.getInt(toAddr(v - back)) != 0x03E00008) {
                    continue;
                }
                boolean padding = true;
                for (long a = v - back + 8; a < v; a += 4) {
                    if (mem.getInt(toAddr(a)) != 0) {
                        padding = false;
                        break;
                    }
                }
                if (padding) {
                    return true;
                }
            }
        } catch (Exception e) {
            return false;
        }
        return false;
    }
}
