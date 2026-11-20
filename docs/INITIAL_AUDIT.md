# Initial audit (phases 0 and 1)

Summary of what has been measured. The details and methods are in the
findings; this file is the report to read first.

## 1. Environment ([findings/07](findings/07-phase0-environment.md))

| Tool | Status |
|---|---|
| Git, CMake, Ninja, Python 3, Vulkan SDK | available on the development machine |
| MSVC (Build Tools 2022, C++ x64) | available; use the "x64 Native Tools Command Prompt for VS 2022" |
| JDK 21 + Ghidra 12.1.4 + EE extension | available |
| PCSX2 (reference) | not checked |

## 2. The disc ([findings/09](findings/09-executable.md) s.3)

SLUS-21661, revision 1.01, NTSC. One ELF (`SLUS_216.61`, 1.46 MB), 15 IRX
modules, `IOPRP300.IMG`, and the data in `GAME.DIR` (index) + `GAME.WAD`
(2.46 GB).

## 3. The executable ([findings/09](findings/09-executable.md))

- ELF32 MIPS R5900, entry point `0x00100008`, a single segment from
  `0x00100000` to `0x00285FCC`, no symbols.
- 278,200 code words; ≥2,293 functions (counting `JAL` targets only).
- Much 64-bit code, ~3,300 "likely" branches, 6,705 128-bit LQ/SQ, 768 MMI,
  6,596 COP2 operations (VU0 macro mode), 33,169 FPU instructions.
- **All EE code looks static**: no code overlays.
- ~585 data words inside `.text` (findings/12 s.6: all in one data table).
- 1,583 `ass_*` names (the engine's script API) name many functions.

## 4. IOP ([findings/10](findings/10-iop-modules.md))

8 modules are loaded: sio2man, multitap, padman, mcman, mcserv, libsd and
two of the engine's own, `ALASD` (an "Argonaut" audio driver) and `CDARB`
(disc arbitration).

## 5. VU ([findings/11](findings/11-vu-microprograms.md))

The microprograms are in `.vutext` (58 KB), with a table of 32 chunks (EE
source → VU destination). At least 4 large programs only fit in VU1. The EE
also starts VU0 microprograms (`VCALLMS`). **Static translation is
feasible.**

## 6. Hardware dependencies

| Piece | Used? | Evidence |
|---|---|---|
| EE + FPU + MMI + VU0 macro | yes | the counts in s.3 |
| VU0 micro mode | yes | 6 `VCALLMS` |
| VU1 | yes | programs up to 16 KB; `libgraph` messages about VU1 |
| VIF0, VIF1, GIF | yes | direct register and FIFO accesses |
| GS | yes | privileged registers (video setup) |
| DMAC | yes | `libdma`; channels 1 and 2 named |
| IPU | likely | `libipu` linked, IPU channel reset; videos not confirmed |
| Timers, scratchpad | yes | direct accesses |
| SPU2 | yes, through the IOP | `LIBSD` + `ALASD` |
| Memory card, controller, multitap | yes, through the IOP | loaded modules |
| USB / USB keyboard | probably not | modules on the disc, not referenced |

## 7. Ghidra and the first recompilation ([findings/12](findings/12-ghidra-analysis.md), [13](findings/13-first-recompilation.md))

- **4,420 functions**, 95.5% of `.text` covered. 845 of them appeared only
  by reading the script API table (1,586 of the game's own names).
- The 585 suspect words are all in a 20 KB data table outside every
  function.
- PS2Recomp: **0 unsupported instructions**, 0 failures; 2,016 indirect
  jumps are left to dispatch by address.

## 8. Since then

- Repository, full build, per-instruction tests and 4 translator fixes:
  [findings/14](findings/14-repository-build-and-tests.md).
- First boot, one blocker at a time: [findings/15](findings/15-first-boot.md).
