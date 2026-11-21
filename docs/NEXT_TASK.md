# Next task

## For the agent

1. **Next: native VU1 (decision D14, KNOWN_ISSUES T2), then play
   further.** The first level runs at about 4 fps (findings/20 s.5): the
   VU1 interpreter costs about 1,300 host instructions per VU cycle. Start
   with group F (findings/20 s.1, hot resume PCs in the table) and check
   every translation with `ben10_vu1_replay --sum` against the
   interpreter (tools/vu1_replay). Also: measure `BEN10_GS_THREADS` (row bands, findings/20 s.7) on a
   machine with more than 2 cores and make the best value the default. Then play on past the first fight
   (findings/20 s.10: Cross jumps and closes tips, Z/V attack, Right Shift
   picks an alien, E transforms) and fix the next
   thing that breaks. Key presses in scripted runs: hold 2.5 s.
2. **Native CDARB, the rest (D12):** counts <= 0 and the init call
   (findings/17 s.7-8); then ALASD (the sound engine) natively, which also
   frees CDARB from the interpreter.
3. Keep going one blocker at a time; record each in findings/15. Update the
   "Boot" milestone in `tools/native_progress.py` when it changes.
4. More per-instruction tests for the game's instructions still untested
   (KNOWN_ISSUES R13): RSQRT.S, MADD.S/MSUB.S, C.*.S, LDL/LDR/SDL/SDR,
   QFSRV, PMFHL and the rest of VU0 macro mode.
5. Send patches 0002-0015 upstream to PS2Recomp.

## For the maintainer (when testing on Windows)

1. Clone the repository with its submodules
   (`git clone --recursive ...`).
2. Follow [01-building.md](01-building.md), the Windows parts, with the
   `windows-release` preset (tested, findings/19 s.1). Ghidra on Windows
   and the tests preset are not tested yet: if a step fails, keep the whole
   error message.
3. Copy `GAME.WAD` into `game\` (it is not copied by `collect_disc.ps1`).
4. After pulling these changes, the function list must be exported again
   (the Ghidra export gained a rule, findings/19 s.7; without it New Game
   stops): run `tools\ghidra_analyze.ps1`, or copy a `functions.csv`
   exported on Linux into `generated\ghidra\`, then build `ben10_codegen`
   and `ben10_poe` again. Configure resets and repatches the submodule by
   itself (patch stamp, findings/20).
5. Speed settings to try, from a prompt in the game folder (one run each,
   with `BEN10_FPS_LOG=1` to print frames per second):
   - `BEN10_GS_THREADS=1`, `2`, `4`, `6`: GS row bands (findings/20 s.7);
   - `BEN10_GS_SYNC=1`: the original synchronous rasteriser, to compare;
   - `BEN10_DRAW_WAIT_STEP=32`: the original wait-loop step (s.8).
   Note the frames per second in the first level for each.
