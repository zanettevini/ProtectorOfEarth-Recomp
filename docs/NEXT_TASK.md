# Next task

## For the agent

1. **Next: faster translated VU code (decision D14, findings/21 s.11).**
   Every VU1 pair is translated (`tools/vu1_translate`, `generated/vu1/`,
   `src/vu/`); patches 0019-0026 brought the translated code from 793 M to
   169 M instructions on the replay (patches 0027-0030: about 305 per VU cycle). On Windows the
   game thread limits the first level (findings/21 s.7: one core at 100%,
   9-10 game frames per second), and a profile (s.8) shows it is almost all
   VU time. The target agreed with the maintainer: test again when the
   first level can reach about 25-30 frames per second. Next, in order
   (s.11): flags as a history read on demand; no per-cycle commit when
   nothing is due; PATH1 progressed in batches; straight-line pair code;
   the VU0 microprograms translated; then the GS bands (they alone would
   cap the level near 20 per second). After each step
   `ben10_vu1_replay user/vu1_captures --sum --native` must print the same
   checksums as without `--native`; measure with `callgrind` on the replay
   and `tools/perf/cpu_per_frame.sh` in the game (the Linux test machine's
   frame rate varies by up to 20% between runs); on Windows with
   `BEN10_PROFILE` and `tools/perf/win_profile.py`. Patch 0025 (MSVC) is not
   built on Windows yet; the maintainer's build script stops any compiler
   process above 6 GB. Then play on (the maintainer reached the first Sumo
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
5. Send patches 0002-0030 upstream to PS2Recomp.

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
