# 05: Vulkan renderer for the GS

## 1. The question

Write a GS renderer from scratch in Vulkan, or reuse one?

## 2. Options found

| Option | What it is | Licence | Note |
|---|---|---|---|
| **paraLLEl-GS** | emulates the GS with Vulkan compute shaders, by Hans-Kristian Arntzen (Themaister) / Arntzen Software | LGPL-3.0 (per BT2 Recomp's credits) | already used by BT2 Recomp; supports up to 16x supersampling; does not aim for bit-exactness, because not even PCSX2's software renderer is exact |
| PCSX2's renderer (GSdx) | GPU (Vulkan, D3D, GL) and software renderers | GPL-3.0 | heavily tested, but large and coupled to the emulator |
| Iris | PS2 emulator with its own Vulkan renderer and SDL3 | to check | useful reading |
| Our own | write it from scratch | ours | the master prompt says "do not assume that translating GS commands straight into draw calls is enough" |

## 3. Recommendation

Start with **paraLLEl-GS** as the Vulkan backend:

- Meets the Vulkan requirement.
- LGPL-3.0 is compatible with a GPL-3.0 project.
- BT2 Recomp proves it works integrated into a PS2Recomp port on Windows.
- It treats the GS as hardware (compute), so it reproduces hard behaviours
  (memory reinterpretation, feedback) that a naive draw-call translator gets
  wrong.

PC improvements (resolution, widescreen) come later, behind options.

## 4. Risks

- BT3-Recomp reports paraLLEl-GS hangs during shader compilation with AMD's
  proprietary driver on Polaris/GCN GPUs on Windows. RDNA GPUs are not
  mentioned. Test early on AMD GPUs. See [KNOWN_ISSUES](../KNOWN_ISSUES.md).
- Performance: a compute GS is heavy; measure.

## 5. Open

- Confirm the licence in paraLLEl-GS's official repository (the source
  consulted was a mirror and BT2's credits).
- Read the integration interface (how BT2 feeds GIF packets to it).
