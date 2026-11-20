# Next task

## For the agent

1. **Next boot blocker: the pad library.** The game loops on
   `scePadGetState` (`FUN_001c1830`) waiting for a stable pad
   (findings/15 s.11). Identify the whole libpad family in
   `0x001C10C8`-`0x001C2088` (each RPC is function 1 with a 128-byte buffer:
   read the command number each function stores, compare with the PS2SDK's
   libpad) and bind it to the runtime's `scePad*` handlers, all together.
2. Keep going one blocker at a time; record each in findings/15. Update the
   "Boot" milestone in `tools/native_progress.py` when it changes.
3. More per-instruction tests for the game's instructions still untested
   (KNOWN_ISSUES R13): RSQRT.S, MADD.S/MSUB.S, C.*.S, LDL/LDR/SDL/SDR,
   QFSRV, PMFHL and the rest of VU0 macro mode.
4. Send patches 0002-0005 upstream to PS2Recomp.

## For the maintainer (when testing on Windows)

1. Clone the repository with its submodules
   (`git clone --recursive ...`).
2. Follow [01-building.md](01-building.md), the Windows parts. None of it has
   been tested on Windows yet: if a step fails, keep the whole error
   message.
3. Copy `GAME.WAD` into `game\` (it is not copied by `collect_disc.ps1`).
