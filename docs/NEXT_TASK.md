# Next task

## For the agent

1. **First: New Game stops (KNOWN_ISSUES B14, findings/19 s.7).** Add the
   second rule to `collectSplitSwitchEntries` in
   `tools/ghidra_scripts/ExportPS2Functions.java` (case targets outside
   the chunk of the switch's `jr` become entries), re-run
   `tools/ghidra_analyze.sh`, the codegen and the build, and check that
   New Game reaches the first level. Check that the 4 switches of s.7 got
   their entries.
2. **Then: speed (KNOWN_ISSUES B13), then play further.** After patch 0012
   the game runs at 5-6 fps and 24 of 30 stack samples are in the software
   GS renderer (`GSCpuBackend`, findings/19 s.5). Options: a GPU renderer
   behind PS2Recomp's `GSRasterBackend` (the plan's renderer), or first a
   faster CPU path (draw on another thread). Then play on in the first
   level (Cross to jump/attack, Z/C/V for the other buttons, the Omnitrix)
   and fix the next thing that breaks.
3. **Native CDARB, the rest (D12):** counts <= 0 and the init call
   (findings/17 s.7-8); then ALASD (the sound engine) natively, which also
   frees CDARB from the interpreter.
4. Keep going one blocker at a time; record each in findings/15. Update the
   "Boot" milestone in `tools/native_progress.py` when it changes.
5. More per-instruction tests for the game's instructions still untested
   (KNOWN_ISSUES R13): RSQRT.S, MADD.S/MSUB.S, C.*.S, LDL/LDR/SDL/SDR,
   QFSRV, PMFHL and the rest of VU0 macro mode.
6. Send patches 0002-0012 upstream to PS2Recomp.

## For the maintainer (when testing on Windows)

1. Clone the repository with its submodules
   (`git clone --recursive ...`).
2. Follow [01-building.md](01-building.md), the Windows parts, with the
   `windows-release` preset (tested, findings/19 s.1). Ghidra on Windows
   and the tests preset are not tested yet: if a step fails, keep the whole
   error message.
3. Copy `GAME.WAD` into `game\` (it is not copied by `collect_disc.ps1`).
