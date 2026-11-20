# Next task

## For the agent

1. **Next boot blocker: the file I/O server.** The boot now waits in
   `FUN_001c8de0` (loop at `0x001C8E70`), which binds RPC server
   `0x80000001`, the IOP file I/O server (`sceOpen`, `sceRead`...). Identify
   the fileio family with `tools/analyzer/function_map.py` and
   `tools/analyzer/rpc_calls.py` against the PS2SDK (`ee/kernel`/`ee/rpc`
   fileio), bind it as a family, rebuild, run (findings/15 s.7).
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
3. Copying `GAME.WAD` into `game\` lets a run go past the first file reads
   (KNOWN_ISSUES B8).
