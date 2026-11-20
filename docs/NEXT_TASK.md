# Next task

## For the agent

1. **Next boot blocker: VBlank interrupts stop after 3 frames.** The game
   waits in `FUN_0012e580` for a frame to finish; its VBlank handler
   (`FUN_0012e5f8`, frame counter at `0x0026854C`) ran 3 times and then
   was never called again (findings/15 s.12). Study how the PS2Recomp EE
   scheduler delivers INTC/DMAC handlers (`EeScheduler::processEvent`, the
   pending invocation queue) and fix the cause with a patch in
   `patches/ps2recomp/` plus a test.
2. Keep going one blocker at a time; record each in findings/15. Update the
   "Boot" milestone in `tools/native_progress.py` when it changes.
3. More per-instruction tests for the game's instructions still untested
   (KNOWN_ISSUES R13): RSQRT.S, MADD.S/MSUB.S, C.*.S, LDL/LDR/SDL/SDR,
   QFSRV, PMFHL and the rest of VU0 macro mode.
4. Send patches 0002-0005 upstream to PS2Recomp.

## For the maintainer (when testing on Windows)

1. Clone the repository with its submodules
   (`git clone --recursive ...`).
2. Follow [01-building.md](01-building.md), the Windows parts. None of it has
   been tested on Windows yet: if a step fails, keep the whole error
   message.
3. Copy `GAME.WAD` into `game\` (it is not copied by `collect_disc.ps1`).
