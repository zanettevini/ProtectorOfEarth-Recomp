# Next task

## For the agent

1. **Next blocker: the world is still mostly black** (KNOWN_ISSUES B11,
   findings/18 s.10). Textured models now get their colours (patch 0007).
   Dump the frame buffer (page 80) and the Z buffer (page 220) of one frame
   to images; check the Z clear (640x224 into page 220) against the 3D
   draws (640x448, Z GEQUAL), and the VU1 packets that still carry RGB = 0
   (the clipping program around 0x2000). `tools/analyzer/vu_disasm.py`
   reads VU micro memory dumps.
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
