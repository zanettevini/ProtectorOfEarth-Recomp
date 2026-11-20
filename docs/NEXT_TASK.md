# Next task

## For the agent

1. **Next boot blocker: `GAME.WAD`.** The boot reads `GAME.DIR` and then
   waits in `sceCdSearchFile("\\GAME.WAD;1")` (findings/15 s.8). With
   `GAME.WAD` in `game/`, run again and find the next stop. The archive
   layout is in findings/16.
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
3. Copying `GAME.WAD` into `game\` lets a run go past the file system init
   (KNOWN_ISSUES B8).
