# Next task

## For the agent

1. **Next boot blocker: `std::terminate` in the chunk parser.** The main
   thread exits while `FUN_001d7240` parses `99_99.pss`: a chunk handler
   with a pure-virtual method (findings/15 s.14). Break on `FUN_001d7240`
   with gdb, log the chunk IDs it reads, find the handler table and who
   fills it (C++ static constructors from crt0's `0x001A5BF8`?), and check
   that every constructor is in the function list.
2. **Native CDARB, the rest (D12):** counts <= 0 and the init call
   (findings/17 s.7-8); then ALASD (the sound engine) natively, which also
   frees CDARB from the interpreter.
3. Keep going one blocker at a time; record each in findings/15. Update the
   "Boot" milestone in `tools/native_progress.py` when it changes.
4. More per-instruction tests for the game's instructions still untested
   (KNOWN_ISSUES R13): RSQRT.S, MADD.S/MSUB.S, C.*.S, LDL/LDR/SDL/SDR,
   QFSRV, PMFHL and the rest of VU0 macro mode.
5. Send patches 0002-0005 upstream to PS2Recomp.

## For the maintainer (when testing on Windows)

1. Clone the repository with its submodules
   (`git clone --recursive ...`).
2. Follow [01-building.md](01-building.md), the Windows parts. None of it has
   been tested on Windows yet: if a step fails, keep the whole error
   message.
3. Copy `GAME.WAD` into `game\` (it is not copied by `collect_disc.ps1`).
