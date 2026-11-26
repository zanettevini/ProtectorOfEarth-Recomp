# Next task

## For the agent

1. **Next: faster translated VU code (decision D14, findings/21 s.11).**
   Every VU1 pair is translated (`tools/vu1_translate`, `generated/vu1/`,
   `src/vu/`); patches 0019-0026 brought the translated code from 793 M to
   159 M instructions on the replay (patches 0027-0031: about 290 per VU cycle). On Windows the
   game thread limits the first level (findings/21 s.7: one core at 100%,
   9-10 game frames per second), and a profile (s.8) shows it is almost all
   VU time. The target agreed with the maintainer: test again when the
   first level can reach about 25-30 frames per second. Next, in order
   (s.11): flags as a history read on demand; no per-cycle commit when
   nothing is due; PATH1 progressed in batches; straight-line pair code;
   then the GS bands (they alone would cap the level near 20 per second).
   Done: the VU0 microprograms run translated too (s.19, patch 0032; game
   thread 86 -> 75 ms per frame on Linux); their per-call overhead is the
   next thing to look at there. FMAC flags four lanes at a time (s.20,
   patch 0033, -4% VU). VIF1/VU1 on a worker thread, opt-in
   (`BEN10_ASYNC_VU1=1`, s.22, patch 0034): the game thread drops from
   about 76 to 31 ms of CPU per frame on Linux, the worker takes 40 ms
   beside it; the 2-core Linux machine cannot show the frame-rate gain.
   On Windows (s.22): 9-18 -> 12.7-23.9 game frames per second in the
   first level; the worker (VU1) is now the limit when enemies are on
   screen, then the busiest GS band. Patch 0035 (s.23): VU work -14%
   since patch 0032 on the replay. GS (findings/20 s.20): the
   dispatcher's page sets memoised (the busiest band), `lround` inline in
   the bilinear filter (patch 0036). Translated blocks (s.24, patch
   0037): VU work 20.0 M host instructions on the replay, -30% since
   s.20; patch 0038 (s.25): 16.95 M, -40%. On Windows (s.26): 16.5-27
   game frames per second; the worker no longer limits, the game thread
   waits for it at every other VBlank because it waits for the GS queue;
   the six GS bands run at about 80% each. GS (findings/20 s.21): a
   `[gs-async]` line every 10 s says what the bands and producers wait
   for; `BEN10_GS_RECORD` and `tools/gs_replay` record and replay the GS
   command stream (every GS change must give the same hashes as `--sync`);
   a race between bands fixed (one area used as Z buffer and colour
   buffer in the same segment); transfers that touch no page in use no
   longer end the segment. Windows with those counters (findings/20
   s.22): the GS holds the producers 33-53% of the time in the level; the
   dispatcher thread (band 0 plus dispatching) is the busiest; nearly all
   GS work is per pixel. Patch 0039: the bilinear filter four channels at
   once (same bits; -16% on the GS replay); measured on Windows in a
   heavier fight: 15.7-22.8 fps, the GS still the limit. Patch 0040 and
   texture rows from texture coordinates (findings/20 s.23): about -10%
   per pixel, texture barriers -79%, segments -47% on the replay;
   Windows: 22.9-25.7 fps with enemies. Random recordings
   (`ben10_gs_synth`, s.24) found four holes in the band rule, closed
   (draws that overlap themselves are made whole on the main backend);
   patch 0041: triangle edges four pixels at a time (-10%); patch 0042
   (s.25): per-primitive setup, per-pixel steps inlined, -22.5% of the
   drawing instructions. Windows after 0042 (s.26): 28-32 fps when quiet,
   19-26 with enemies: there the VIF1 worker (VU1) passes the 33 ms
   budget again. Done: the GS queue no longer allocates per draw; the fps
   log shows frame pacing; the worker is named (patch 0043). The worker
   in a fight (s.27): VU1 code and its flag helpers about 55% of its
   samples, UNPACK 6.5% (patch 0044: fast paths), the push into the GS
   queue 11% (the queue limit). Done (s.28): a compact flag ring (patch
   0045) and FMAC flag results left out in translated blocks where no
   flag instruction can read them (patch 0046 and the translator's step
   5): VU work on the replay -10.6%, FMAC entries queued -67%; to be
   measured on Windows in a fight (`run-worker.bat`). Patch 0047 (s.29):
   block bodies without budget checks, -1.8%. Patch 0048 (findings/21
   s.27): the `[vu-native]` line of the frame-rate log counts the VU
   pairs still run by the interpreter (0 on the replay). Next on the VU
   side: the rest of the per-FMAC cost (operand normalisation and the
   fast-path checks, s.28), the block bodies' per-pair bookkeeping;
   on the GS side
   the VRAM accesses (one address per pixel for the frame and one for Z,
   in place of four), the remaining transfer barriers (uploads into
   pages queued draws still read: copy those pages for the queued draws
   instead of waiting), maybe a dispatcher that draws no band itself;
   measure with `ben10_gs_replay --limit N` under callgrind; check every
   GS change with `ben10_gs_replay --sync` hashes on the level and on
   random recordings, and the band counts against it;
   and on the VU side the upper words (about 50 per VU cycle),
   PATH1 in batches (s.11 item 3) and more static scheduling in blocks;
   the maintainer decides later whether the async option becomes the
   default. Profile the game thread with
   `perf` (`linux-tools`): `perf record -t <GameThread tid>`. VU0 checks: `capture_vu0.gdb` and
   `ben10_vu1_replay user/vu0_captures --vu0 --sum [--native]`. After each step
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
5. Send patches 0002-0048 upstream to PS2Recomp.

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
