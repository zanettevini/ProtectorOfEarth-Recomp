# Next task

## For the agent

1. **Next: speed (KNOWN_ISSUES B13), then play further.** In the first
   level the VU1 interpreter is the largest cost (findings/19 s.8-9: 43 of
   60 samples after patch 0013), then the software GS renderer. Options:
   native VU1 microprograms (T2: translate the game's VU1 programs to C++
   like the EE code; start by listing the programs the game uploads and
   their sizes), or first a GS renderer on the GPU or on more threads.
   Then play on in the first level (Cross to jump/attack, Z/C/V for the
   other buttons, the Omnitrix) and fix the next thing that breaks.
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
