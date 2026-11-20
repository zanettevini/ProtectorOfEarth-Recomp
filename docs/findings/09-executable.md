# 09: the main executable (SLUS_216.61)

## 1. The question

Phase 1 of the master prompt: the ELF's format, entry point, memory layout,
sections, instruction distribution and hardware dependencies.

## 2. How it was measured

- `tools/collect_disc.ps1` mounted the ISO read-only and copied
  `SYSTEM.CNF`, the ELF and the IRX into an extraction folder (outside git).
- `readelf -h -l -S` (binutils) for headers.
- `tools/analyzer/ps2_elf_audit.py` (written for this) for the VU overlay
  table, instruction classification and hardware references.
- ASCII string search (≥5 characters) by category. Only names and counts go
  into this finding.

Wrong turn: the first version of the script stripped the high bits of any
address and counted `0x5000000x` constants as timer registers (240 false
"accesses"). Fixed: only segments 0x1, KSEG0 (0x8/0x9), KSEG1 (0xA/0xB) and
the scratchpad (0x7) count as hardware. After the fix, TIMER fell to 28.

## 3. Disc identity

| Field | Value |
|---|---|
| ISO | 3,400,499,200 bytes, SHA-1 `D212E3CBE2CF234B98C4D0CB35E0EDC555B83D6E` (not yet compared with Redump) |
| `SYSTEM.CNF` | `BOOT2 = cdrom0:\SLUS_216.61;1`, `VER = 1.01`, `VMODE = NTSC` |
| Files at the root | `SLUS_216.61` (1.46 MB), `SYSTEM.CNF`, `GAME.DIR` (30 KB), `GAME.WAD` (2.46 GB), `IOPRP300.IMG` (275 KB), `MODULES\` (15 IRX) |

Disc revision: **1.01** (`VER` field). The SLUS-21661 serial is confirmed by
the ELF name.

## 4. Header and memory

| Field | Value |
|---|---|
| Format | ELF32, little-endian, `EXEC`, MIPS machine (8) |
| Flags | `0x20924001`: `noreorder`, `5900`, `eabi64`, `mips3` |
| Entry point | `0x00100008` |
| Segments | **a single** `LOAD`: vaddr `0x00100000`, 0x1553A8 bytes in the file, 0x185FCC in memory (RWE) |
| End of the image in RAM | `0x00285FCC` (~1.5 MB of the 32 MB) |
| `$gp` (`.reginfo`) | `0x0025D3F0` |
| Symbols | **none** (stripped); `.mdebug.eabi64` exists but has size 0 |

Loaded sections:

| Section | Address | Size | What it is |
|---|---|---|---|
| `.text` | 0x00100000 | 0x10FAE0 (1.06 MB) | EE code |
| `.vutext` | 0x0020FAE0 | 0x0E4F0 (58 KB) | **VU microprograms stored in EE RAM** ([11](11-vu-microprograms.md)) |
| `.data` | 0x0021E000 | 0x1AD98 | data |
| `.vudata` | 0x00238D98 | 0 | empty |
| `.rodata` | 0x00238E00 | 0x1C5A8 | constants and strings |
| `.sbss` / `.bss` | 0x00255400 / 0x00255480 | 0x34 / 0x30B4C | zeroed at boot |

Sections not loaded: `.DVP.ovlystrtab`, 31 × `.DVP.overlay..*`,
`.DVP.ovlytab` (microprogram metadata, see [11](11-vu-microprograms.md)).

## 5. Instruction distribution in `.text`

278,200 32-bit words.

| Category | Words | % |
|---|---|---|
| Plain MIPS (primary opcode) | 164,489 | 59.1 |
| SPECIAL (ALU, shifts, JR/JALR, SYSCALL) | 64,989 | 23.4 |
| FPU (COP1 + LWC1/SWC1) | 33,169 | 11.9 |
| LQ/SQ (128-bit load/store) | 6,705 | 2.4 |
| COP2 (VU0 macro mode + LQC2/SQC2/QMTC2/QMFC2) | 6,596 | 2.4 |
| MMI (128-bit SIMD) | 768 | 0.3 |
| REGIMM | 621 | 0.2 |
| COP0 | 278 | 0.1 |
| **Does not decode** | 585 | 0.2 |

Points that matter for the recompiler:

- **64-bit is common**: 11,969 `LD`, 12,409 `SD`, 19,226 `DADDU`;
  `LDL/LDR/SDL/SDR` ~800 each (unaligned access). 64-bit semantics must be
  right from the start.
- **"Likely" branches**: ~3,300 (`BEQL` 1,544, `BNEL` 1,550...). The delay
  slot only runs if the branch is taken.
- **VU0 macro mode**: 2,795 vector operations, 1,721 `QMTC2`, 819 `QMFC2`.
  PS2Recomp claims support; needs testing.
- **VCALLMS**: 6 occurrences. The game **starts microprograms on VU0**, not
  only on VU1. See [11](11-vu-microprograms.md).
- **MMI**: 768, concentrated in `POR` (112, usually used to copy 128-bit
  registers), `PCPYLD`/`PCPYUD`, `PEXTLB`, packed additions. About 60
  different mnemonics.
- **FPU**: dominated by `MUL.S` (3,825), `ADD.S`, `SUB.S`, comparisons;
  `DIV.S` 213, `SQRT.S` 85, `RSQRT.S` 36. Physics and camera depend on it:
  the EE FPU clamp (no infinity/NaN) is mandatory.
- **Indirect jumps**: 5,776 `JR` (returns + jump tables) and 1,568 `JALR`
  (calls through pointers: virtual tables and the script API, s.7).
- **Functions**: 2,293 unique `JAL` targets. That is a **lower bound**:
  functions called only through pointers do not show up. The real inventory
  comes from Ghidra.
- `BREAK` 165: in compiled MIPS code this usually comes from divide-by-zero
  checks; confirm in Ghidra.
- `SYSCALL` 159: 131 of them are the `libkernl` stubs (one per service
  number, 0 to 130, plus negative ones for the interrupt versions). The count
  does not say which services the game uses; that comes from the calls to
  those stubs.

### 5.1 The 585 words that do not decode

Most are isolated (550 runs, the longest 14 words), especially near the
start of `.text` (`0x00100328` onward). The patterns (opcode 0x13, COP0 with
invalid fields, SPECIAL with funct 0x01/0x0E) and the values look like
**data inside `.text`**, not instructions. Hypothesis to confirm in Ghidra
before phase 2: the recompiler **must not** translate these words as code.

## 6. Hardware referenced directly

`LUI` + offset pairs that point to registers (limit: this only catches
addresses built in the code itself; accesses through pointer tables do not
show up).

| Block | Accesses | Note |
|---|---|---|
| TIMER | 28 | counters 0, 1, 3 |
| Privileged GS (`0x12000000`) | 28 | PMODE, DISPFB/DISPLAY, CSR... (video setup) |
| Scratchpad | 17 | direct use of `0x70000000` |
| VIF1 (regs) | 15 | |
| VIF1 FIFO | 6 | direct writes to the VIF1 FIFO |
| GIF (regs) | 6 | |
| VIF0 (regs / FIFO) | 3 / 1 | |

No direct access to the DMAC channels through `LUI` (they should go through
`libdma`, via tables). Strings confirm channels 1 and 2 (VIF1 and GIF) are
used and the IPU channels are reset.

## 7. Libraries and strings

Sony library version tags found (format `PsIIlibXXXX NNNN`): `libdma`,
`libpad`, `libmc`, `libvu0`, `libcdvd`, `libkernl`, `libscf`, `libipu`.
There are also `libgraph` messages (`sceGsPutDrawEnv`, `sceGsSyncPath`).

Relevant strings (described, not copied in full):

- Paths: `cdrom0:\GAME.DIR`, `\GAME.WAD`, `cdrom0:\MODULES\%s`,
  `cdrom0:\IOPRP300.IMG`, and a path to an ELF in an `SCE_DD` folder **that
  does not exist on this disc** (dead code or a demo-disc feature;
  investigate whether it is called).
- Debug options confirmed: `DEBUG_CHEATS`, `DEBUG_TEXT`, `DEBUG_FLY`,
  `DEBUG_SOUND`, `LEVEL_SELECT`, `DEMO_LEVEL_SELECT`, `START_LEVEL`,
  `LOAD_TO_START_LEVEL`, `LEVEL_WALK`.
- **1,583 `ass_*` names**, for example `ass_SpawnFirst`,
  `ass_SetDialogTextScale`, `ass_GetEngineVersion`, `ass_GetBuildIdent`, 67
  `ass_net_*`. They look like the **API exposed to the engine's script
  system** (the game mentions "Strats" and map and strat "WAD" versions). A
  name→pointer table would name up to 1,583 functions at once. To
  investigate in Ghidra.
- `ass_WiiDisplayHomeMenuBanned` exists: the code is **shared with the Wii
  version**.
- `GAME.WAD` has a version checked by the engine ("wad not supported by
  this engine build").
- No video strings (`.pss`, MPEG). `libipu` is linked, but it is still
  unknown whether there are videos (probably inside `GAME.WAD`).

## 8. Code loaded at run time

- The ELF has **a single segment** and there are no EE code overlays (no
  overlay sections besides the VU ones).
- The only hint of loading another executable is the `SCE_DD` path, whose
  file is not on the disc.
- Provisional conclusion: **all EE code is static**. "Conflict B" in
  [06](06-master-prompt-conflicts.md) probably does not exist in this game.
  Confirm by looking for code copies into RAM and `FlushCache` followed by a
  jump into data.

## 9. Open

- Compare the SHA-1 with Redump.
- ~~Function inventory~~, ~~585 words~~, ~~`ass_*` table~~: done in
  [12](12-ghidra-analysis.md).
- See whether the `SCE_DD` path is reachable.
- Which kernel services are called (calls to the syscall stubs).
