# Next task

## For the agent

1. **Next boot blocker: an IOP disc read that never finishes.** After
   ~30 frames the game waits in its draw wait (`FUN_0012e580`) while the
   IOP interpreter spends its time in `IopCdvd::Impl::readSectors` /
   `IopMemory::ownsRamRange` (findings/15 s.12). Trace the read results
   (lsn, sector count, destination, success) and the IOP module's retry
   loop; fix with a PS2Recomp patch or move `CDARB`/`ALASD` to native
   services (D2).
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
