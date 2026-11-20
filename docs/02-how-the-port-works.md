# How the port works

If you only read one doc, read this one.

## The problem

`SLUS_216.61` is machine code for the PlayStation 2's main CPU, the
**Emotion Engine** (EE): a modified MIPS with 128-bit registers, its own
SIMD instructions (MMI) and an attached vector unit (VU0). Your PC has an
x86-64 CPU, which can't run those instructions. The game also expects the
rest of the console:

* the **IOP**, a second CPU that handles the disc, controllers, memory card
  and sound, running its own modules (`.IRX`);
* the **vector units** VU0 and VU1, which run their own microprograms (VU1
  transforms the 3D geometry before drawing);
* the **GS** (Graphics Synthesizer), the graphics chip, with 4 MB of its own
  memory, fed with command packets;
* the **SPU2** (sound), **DMA**, timers and interrupts.

## Three ways to solve it

| Approach | What it means | Examples |
|---|---|---|
| **Emulation** | translate the instructions *while the game runs* | PCSX2 |
| **Decompilation** | people rewrite the whole game in C/C++, function by function | Super Mario 64, OpenGOAL (Jak) |
| **Static recompilation** | a tool translates *every* function into C++ *ahead of time*; the result compiles into a native `.exe` | Mind over Recomp, Gitaroo Man Native, **this project** |

We use **static recompilation**: the game runs natively early, and the
emulated pieces are then replaced with our own code over time (renderer,
sound, controllers, enhancements). A decompilation can come later, one
function at a time, on top of a game that already runs.

## The pipeline

```
 Your disc (.iso)
      |
      |  tools/collect_disc.ps1 (or bsdtar)
      v
 game/SLUS_216.61  (the executable)    game/MODULES/*.IRX, GAME.DIR, GAME.WAD
      |                                   |
      |  Ghidra + EE extension            |  read by the runtime while the
      |  + ApplyScriptApiNames.java      |  game runs, unchanged
      |  (tools/ghidra_analyze.*)         |
      v                                   |
 generated/ghidra/functions.csv           |
 (where each function starts and ends)    |
      |                                   |
      |  ps2_recomp + ben10_poe.toml      |
      |  (the translator, with our        |
      |   patches and bindings)           |
      v                                   |
 generated/default/*.cpp  (the game's code, as C++)
      |                                   |
      |  ordinary C++ compiler            |
      |  + PS2Recomp runtime              |
      |  + our code in src/               |
      v                                   |
 ben10_poe  (native program) <------------+
```

### What the translator does

1. Reads the ELF: one segment, loaded at `0x00100000` (findings/09).
2. For each function in the CSV, decodes every instruction and writes the
   equivalent C++. The EE registers become fields of a structure
   (`ctx->r[]`, 128 bits each); the PS2's 32 MB of memory is a byte array
   (`rdram`). For example `addu $v0, $a0, $a1` becomes a 32-bit add whose
   result is sign-extended to 64 bits, as on the PS2.
3. Branches with a *delay slot* (the instruction after a branch runs before
   the branch happens) become `goto` inside the function.
4. Jumps only known at run time (tables, function pointers) go through a
   dispatch table: PS2 address → already compiled C++ function. Nothing is
   interpreted.
5. `SYSCALL` becomes a call into the runtime, which answers like the PS2
   kernel.
6. Functions listed in `stubs` of `ben10_poe.toml` are replaced by a call to
   a runtime handler. That is how the Sony libraries are answered: the
   executable has no symbols, so each library function is recognised with
   evidence and bound by address (findings/15).

### What the runtime does

The PS2Recomp runtime is the "PS2" around the translated code: memory,
kernel (threads, semaphores), DMA, VIF/GIF, a software GS, VU0 and VU1, the
IOP and sound. Today almost all of it still imitates the hardware, and two
parts do it by interpretation (the IOP and the VUs: temporary debts T1 and
T2 in docs/KNOWN_ISSUES.md). The README's "Going native" board measures how
much has been replaced by the port's own code.

## How correctness is checked

- **Instruction by instruction**: the tests in `tests/` build a small ELF,
  run it through the same translator and compare the result with PS2
  behaviour (PCSX2 as the reference). They found the first 4 translator bugs
  (findings/14).
- **The whole game**, later: compare memory state and the image of the
  same frame with PCSX2.
