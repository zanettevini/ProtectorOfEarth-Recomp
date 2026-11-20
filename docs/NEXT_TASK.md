# Next task

## For the agent

1. **Native CDARB (D12).** The boot stalls in disc reads done by the
   game's interpreted IOP modules (findings/15 s.12). Replace `CDARB.IRX`
   with a native service:
   - finish reading the protocol (findings/17 s.6): the request struct
     (`.text+0xDD4` queue insert, `.text+0x884` serve), the parameters of
     functions 2 and 7, the completion SIF command;
   - patch `ps2xIOP` with an extension point for game-registered native
     services, preferred over loading the IRX (with a test);
   - write the service in `src/iop/` (functions 0, 2, 7; reads from
     `GAME.WAD` into EE RAM; the completion command), plus native `CDArb:4`
     and `CDArb:6` for the still-interpreted ALASD;
   - run, and record the result in findings/17.
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
