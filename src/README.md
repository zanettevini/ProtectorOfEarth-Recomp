# src/

The port's **own** native code: what replaces, piece by piece, the parts of
the PS2 that PS2Recomp still emulates. For now it is empty: the executable
uses PS2Recomp's `main` and runtime.

Each subject goes in its own folder once it exists, as in Mind over Recomp,
for example:

| Folder (planned) | What |
|---|---|
| `src/iop/` | native services that answer for the IOP modules (findings/10) |
| `src/vu/` | VU microprograms translated at build time (findings/11) |
| `src/gs/` | the Vulkan renderer |
| `src/input/`, `src/audio/`, `src/saves/` | pads (SDL3), sound, saves on the PC |
| `src/overrides/` | game functions replaced with native code, by address |

Every time a piece becomes code here, update `tools/native_progress.py` and
run `python3 tools/native_progress.py --readme` (AGENTS.md s.6).
