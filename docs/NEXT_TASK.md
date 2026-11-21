# Next task

## For the agent

1. **Next: native VU1 (decision D14, KNOWN_ISSUES T2), then play
   further.** The first level runs at about 4 fps (findings/20 s.5): the
   VU1 interpreter costs about 1,300 host instructions per VU cycle. Start
   with group F (findings/20 s.1, hot resume PCs in the table) and check
   every translation with `ben10_vu1_replay --sum` against the
   interpreter (tools/vu1_replay). Second option: more rasteriser threads
   (findings/20 s.6). Then play on in the first level (Cross to
   jump/attack, Z/C/V for the other buttons, the Omnitrix) and fix the next
   thing that breaks. Key presses in scripted runs: hold 2.5 s.
2. **Native CDARB, the rest (D12):** counts <= 0 and the init call
   (findings/17 s.7-8); then ALASD (the sound engine) natively, which also
   frees CDARB from the interpreter.
3. Keep going one blocker at a time; record each in findings/15. Update the
   "Boot" milestone in `tools/native_progress.py` when it changes.
4. More per-instruction tests for the game's instructions still untested
   (KNOWN_ISSUES R13): RSQRT.S, MADD.S/MSUB.S, C.*.S, LDL/LDR/SDL/SDR,
   QFSRV, PMFHL and the rest of VU0 macro mode.
5. Send patches 0002-0013 upstream to PS2Recomp.

## For the maintainer (when testing on Windows)

1. Clone the repository with its submodules
   (`git clone --recursive ...`).
2. Follow [01-building.md](01-building.md), the Windows parts, with the
   `windows-release` preset (tested, findings/19 s.1). Ghidra on Windows
   and the tests preset are not tested yet: if a step fails, keep the whole
   error message.
3. Copy `GAME.WAD` into `game\` (it is not copied by `collect_disc.ps1`).
