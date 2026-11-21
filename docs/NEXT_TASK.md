# Next task

## For the agent

1. **Next blocker: Ben does not move in the first level** (KNOWN_ISSUES
   B12). Check what the game reads from the pad there (`padRead` buffer:
   digital buttons and the analog sticks; the keyboard fills the left stick
   only in analog mode) and whether the level is waiting for something
   first (intro, a prompt). Native over emulated: find the game's own
   decision, don't fake presses.
2. **Native CDARB, the rest (D12):** counts <= 0 and the init call
   (findings/17 s.7-8); then ALASD (the sound engine) natively, which also
   frees CDARB from the interpreter.
3. Keep going one blocker at a time; record each in findings/15. Update the
   "Boot" milestone in `tools/native_progress.py` when it changes.
4. More per-instruction tests for the game's instructions still untested
   (KNOWN_ISSUES R13): RSQRT.S, MADD.S/MSUB.S, C.*.S, LDL/LDR/SDL/SDR,
   QFSRV, PMFHL and the rest of VU0 macro mode.
5. Send patches 0002-0010 upstream to PS2Recomp.
6. Speed: the first level runs at about 2-3 fps under Xvfb with software
   OpenGL and a -O0 build of the generated code; measure an optimised build
   before profiling.

## For the maintainer (when testing on Windows)

1. Clone the repository with its submodules
   (`git clone --recursive ...`).
2. Follow [01-building.md](01-building.md), the Windows parts. None of it has
   been tested on Windows yet: if a step fails, keep the whole error
   message.
3. Copy `GAME.WAD` into `game\` (it is not copied by `collect_disc.ps1`).
