# Next task

## For the agent

1. **Next: native VU1 phase 2 (decision D14, findings/21 s.3).** Phase 1
   is in: every VU pair is translated to a function running the
   interpreter's own step (`tools/vu1_translate`, `generated/vu1/`,
   `src/vu/`). Phase 2a-2d are in (patches 0019-0022: upper and lower
   executors folded per word, VF/ACC results written at issue; translated
   VU code 793 M -> 349 M instructions on the replay). Next, in order:
   static timing per block for the remaining queues (VI, flags, Q/P,
   stores; the pipelines are empty at every resume); flags only where
   read. After each step,
   `ben10_vu1_replay user/vu1_captures --sum --native` must print the same
   checksums as without `--native`; measure with `callgrind` on the replay
   and `tools/perf/cpu_per_frame.sh` in the game (the frame rate of the
   Linux test machine varies by up to 20% between runs). On Windows the
   game thread limits the first level (findings/21 s.7: one core at 100%),
   but the translated code saves only about a tenth of its time there with
   MSVC. The Windows profile (findings/21 s.8) gives the order: the FMAC
   model inline on MSVC (pick the opcode case at translation time), then
   the VU0 microprograms translated (a quarter of the game thread). Then play on (the maintainer reached the first Sumo
   Slammer card on Windows, findings/20 s.17) and fix the next thing that
   breaks. Key presses in scripted runs: hold 2.5 s.
2. **Native CDARB, the rest (D12):** counts <= 0 and the init call
   (findings/17 s.7-8); then ALASD (the sound engine) natively, which also
   frees CDARB from the interpreter.
3. Keep going one blocker at a time; record each in findings/15. Update the
   "Boot" milestone in `tools/native_progress.py` when it changes.
4. More per-instruction tests for the game's instructions still untested
   (KNOWN_ISSUES R13): RSQRT.S, MADD.S/MSUB.S, C.*.S, LDL/LDR/SDL/SDR,
   QFSRV, PMFHL and the rest of VU0 macro mode.
5. Send patches 0002-0024 upstream to PS2Recomp.

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
   - `BEN10_GS_THREADS=1`, `2`, `4`, `6`: GS row bands (findings/20 s.7;
     measured in s.18, 6 is now the default on 8+ hardware threads);
   - `BEN10_GS_SYNC=1`: the original synchronous rasteriser, to compare;
   - `BEN10_DRAW_WAIT_STEP=32`: the original wait-loop step (s.8).
   Note the frames per second in the first level for each.
6. Saves now live in `game\saves\mc0` (findings/18 s.14); the first run
   copies `game\mc0` there and keeps the old folder. Check that the save
   is found (the game goes from the autosave notice straight to the title
   screen) and that the log shows the `[saves] memory card 0: ...` line.
