# 02: PS2 ports already done and what to learn from them

## 1. The question

Has anyone already taken PS2Recomp to a playable game? Which architecture
worked?

## 2. How it was studied

Reading the public READMEs. None was built. The numbers below are what each
project claims, not our measurements.

## 3. Gitaroo Man Native (SLUS-20294)

[erinxocon/gitaroo-man-native](https://github.com/erinxocon/gitaroo-man-native)

- PS2Recomp fork as a submodule (`external/PS2Recomp`, its own branch).
- Functions found by Ghidra; names in `config/symbols/` (`sdk.txt`,
  `manual.txt`, `agent.txt`); config in `config/gitaroo.toml`.
- **VU1**: the mesh renderer is native C++; the rest of the microprogram is
  recompiled to C++ by its own tool (`tools/vu1lift`), based on the Ace
  Combat 5 port's `vurecomp`, using a run-time *census* and PCSX2 captures.
  Claims bit-identical results to the interpreter over more than 10 million
  executions.
- **GS**: OpenGL, plus a CPU rasteriser as a reference; compares every GPU
  frame with the CPU one.
- **Emulated IOP**; EE kernel in HLE.
- **EE code generated at run time falls back to an EE interpreter** (can be
  turned off). Our master prompt forbids this.
- State: reaches the tutorial and the first stage, 60 fps, audio, optional
  widescreen.
- Useful tools: scripted input (`PS2X_INPUT_SCRIPT`), frame dumps,
  per-subsystem traces (`PS2X_TRACE_GS`, `_VIF`, `_VU1`, `_RPC`, `_PAD`,
  `_SPU2`...), a PINE client for PCSX2.
- Tested on Linux only.

## 4. BT2 Recomp, Dragon Ball Z Budokai Tenkaichi 2 (SLUS-21441)

[oric212/BT2-Recomp](https://github.com/oric212/BT2-Recomp)

- **Windows x64, C++20, MSVC + CMake**: the same combination we want.
- Its own runtime: `runtime/ee/` (memory, kernel, threads, timers, DMA, IPU,
  VU1), `runtime/services/`, `runtime/gs/` (GS, GIF, VIF1), `runtime/host/`.
- **The IOP is not emulated**: a layer of native services answers the
  requests the game makes to the IOP (disc, files, pad, memory card, sound,
  CRI middleware). **No IOP code runs.** This is the model compatible with
  the master prompt.
- **Checked boundaries**: unsupported behaviour stops execution instead of
  being guessed.
- Claims 11,139 EE functions plus 494 from an overlay, all compiled natively,
  nothing interpreted.
- **GS in Vulkan via paraLLEl-GS** (LGPL-3.0). See [05](05-vulkan-renderer.md).
- Verification against PCSX2: boot states equal to captures, logic/drawing/AI
  identical frame by frame in battle, saves identical byte for byte.
- Docs: `docs/guides/`, `docs/PROG.md`, `docs/progress/`, `docs/project/`
  (plan and legal notes), `docs/reference/`.
- Marks AI-made work as unverified until it passes tests.

## 5. BT3-Recomp (SLUS-21678)

[z3xox/BT3-Recomp](https://github.com/z3xox/BT3-Recomp)

- Also built on PS2Recomp; Windows, Linux and macOS (arm64).
- OpenGL renderer by default, paraLLEl-GS experimental.
- **Relevant warning**: on Windows paraLLEl-GS runs on *lavapipe* (software
  Vulkan) because some vendors' Vulkan drivers, AMD's proprietary driver on
  Polaris/GCN GPUs in particular, hang compiling shaders. See
  [KNOWN_ISSUES](../KNOWN_ISSUES.md).
- Needs ~16 GB of RAM and ~10 GB of disk for the build.

## 6. What to adopt

| From | What |
|---|---|
| BT2 | IOP through native HLE services, without running IRX; Windows/MSVC; paraLLEl-GS; "stop instead of guessing" |
| Gitaroo | `config/` + `config/symbols/` layout; VU1 lifter from census + captures; per-subsystem trace variables; scripted input |
| Gitaroo / BT2 | verify every replacement against a reference (PCSX2, or the original recompiled code) |
| MoR | AGENTS.md, numbered findings, heavy comments, legal rules |

## 7. What not to adopt

- The EE interpreter for run-time generated code (Gitaroo).
- The emulated IOP (Gitaroo, and the default `ps2xIOP`).
- A VU1 interpreter in the final executable. See
  [06](06-master-prompt-conflicts.md).

## 8. Open

- Read BT2's runtime code (`runtime/services/`) to see how many IOP services
  a real game needs.
- Read the Ace Combat 5 port's `vurecomp` (link cited by Gitaroo Man Native:
  `sal063/Ace-Combat-5-Static-recompilation`).
- Check whether Ben 10 uses middleware BT2 already handles (CRI, for
  example). Depends on the disc: [03](03-the-game.md).
