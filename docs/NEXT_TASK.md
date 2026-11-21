# Next task

## For the agent

1. **Next: play further into the first level, and speed.** Ben walks
   (findings/18 s.13). Play on (Cross to jump/attack, Z/C/V for the other
   buttons, the Omnitrix) and fix the next thing that breaks; measure the
   frame rate of an optimised build (`linux-release` or RelWithDebInfo)
   before profiling the -O0 one.
2. **Native CDARB, the rest (D12):** counts <= 0 and the init call
   (findings/17 s.7-8); then ALASD (the sound engine) natively, which also
   frees CDARB from the interpreter.
3. Keep going one blocker at a time; record each in findings/15. Update the
   "Boot" milestone in `tools/native_progress.py` when it changes.
4. More per-instruction tests for the game's instructions still untested
   (KNOWN_ISSUES R13): RSQRT.S, MADD.S/MSUB.S, C.*.S, LDL/LDR/SDL/SDR,
   QFSRV, PMFHL and the rest of VU0 macro mode.
5. Send patches 0002-0011 upstream to PS2Recomp.

## For the maintainer (when testing on Windows)

1. Clone the repository with its submodules
   (`git clone --recursive ...`).
2. Follow [01-building.md](01-building.md), the Windows parts. None of it has
   been tested on Windows yet: if a step fails, keep the whole error
   message.
3. Copy `GAME.WAD` into `game\` (it is not copied by `collect_disc.ps1`).
