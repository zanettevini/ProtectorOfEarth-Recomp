# Next task

## For the agent

1. **Next blocker: giant triangles over the scene** (KNOWN_ISSUES B11,
   findings/18 s.10). The scene is drawn, then covered by triangles with
   vertices behind the camera. Check the VU1 clip path: CLIP/FCGET/FCAND
   results and the clipping routine around VU address 0x2000 (dump VU1
   state before and after one clipped triangle; `tools/analyzer/vu_disasm.py`
   reads the code). The fix may be in the VU1 interpreter (T2), or the
   start of the native VU1 translation.
2. **Native CDARB, the rest (D12):** counts <= 0 and the init call
   (findings/17 s.7-8); then ALASD (the sound engine) natively, which also
   frees CDARB from the interpreter.
3. Keep going one blocker at a time; record each in findings/15. Update the
   "Boot" milestone in `tools/native_progress.py` when it changes.
4. More per-instruction tests for the game's instructions still untested
   (KNOWN_ISSUES R13): RSQRT.S, MADD.S/MSUB.S, C.*.S, LDL/LDR/SDL/SDR,
   QFSRV, PMFHL and the rest of VU0 macro mode.
5. Send patches 0002-0007 upstream to PS2Recomp.

## For the maintainer (when testing on Windows)

1. Clone the repository with its submodules
   (`git clone --recursive ...`).
2. Follow [01-building.md](01-building.md), the Windows parts. None of it has
   been tested on Windows yet: if a step fails, keep the whole error
   message.
3. Copy `GAME.WAD` into `game\` (it is not copied by `collect_disc.ps1`).
