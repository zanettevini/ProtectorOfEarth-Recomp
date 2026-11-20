# 04: map of the PS2 hardware and what each part demands

## 1. The question

Which PS2 parts must the runtime reproduce, and what is the hard part of
each?

## 2. Source

General, public knowledge about the PS2. The numbers are the widely known
ones; **before implementing, confirm every detail in PCSX2's code** (the
master prompt's reference) and note the file it came from.

## 3. The parts

| Part | What it is | In the port | Difficulty |
|---|---|---|---|
| **EE (R5900)** | main CPU, modified MIPS III, 128-bit registers, ~295 MHz | recompiled (PS2Recomp) | 128-bit MMI instructions, non-IEEE FPU, delay slots, indirect jumps |
| **FPU (COP1)** | 32-bit floating point | recompiled | has no infinity/NaN and rounds differently from x86: needs clamp emulation on every operation |
| **COP0** | system control, exceptions, counters | native runtime | only what the game uses |
| **RAM** | 32 MB, seen through several address mirrors | runtime | correct address masking |
| **Scratchpad** | 16 KB of fast memory at `0x70000000` | runtime | trivial |
| **VU0** | vector unit; macro mode (COP2, instructions in the EE stream) and micro mode | macro: PS2Recomp; micro: to check | if the game uses VU0 micro mode, its microprograms need static translation |
| **VU1** | vector unit that usually transforms vertices and sends geometry to the GS | **static translation of the microprograms** | the biggest challenge: pipeline, flags, branches, sync with VIF1/GIF |
| **VIF0/VIF1** | unpack data and microprograms for the VUs | native runtime | unpack formats, masks |
| **GIF** | data path to the GS (PATH1/2/3) | native runtime | arbitration between paths |
| **GS** | rasteriser with 4 MB of eDRAM | Vulkan ([05](05-vulkan-renderer.md)) | texture formats, memory reinterpretation, blending, framebuffer feedback |
| **DMAC** | 10 DMA channels | native runtime | tag chains, order and timing |
| **IPU** | MPEG-2 decoder for videos | native runtime (or a native video decoder) | only if the game uses MPEG videos |
| **IOP** | second CPU (R3000A) with 2 MB of RAM, controls disc, pad, memory card, sound | **native HLE services**, without running IRX ([06](06-master-prompt-conflicts.md)) | finding which RPC services the game calls |
| **SIF** | EE↔IOP bridge (RPC) | native runtime | RPC protocol |
| **SPU2** | sound chip, 48 voices, 2 MB of sound memory, ADPCM | native | streaming and timing |
| **Timers / VSync / INTC** | counters and interrupts | native runtime | games often depend on the VBlank rhythm |

## 4. What this says about the order of the work

- Phase 1 must measure, in the ELF: MMI use, COP2 use (VU0 macro), presence
  of VU microprograms (look for VIF `MPG` uploads), IPU use, the list of IRX
  and RPCs. Only then decide what to implement (master prompt phase 4: "do
  not implement whole subsystems without evidence").
- The natural order: CPU/memory → kernel syscalls and threads → SIF/IOP
  services (file, pad) → DMA/VIF/GIF → GS → VU1 → audio → video.

## 5. Open

- ~~Take the s.4 measurements on the real ELF~~: [09](09-executable.md).
- Note, for each item, the reference files in PCSX2's code.
