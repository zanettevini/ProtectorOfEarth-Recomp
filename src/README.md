# src/

The port's **own** native code: what replaces, piece by piece, the parts of
the PS2 that PS2Recomp still emulates. The executable uses PS2Recomp's
`main` and runtime; every `.cpp` under `src/` is built into it (CMake
globs the folder), and game overrides register themselves with
`PS2_REGISTER_GAME_OVERRIDE`.

Today:

| File | What |
|---|---|
| `src/iop/cdarb_native.cpp` | the EE's disc reads through CDARB, done natively from `GAME.WAD` (findings/17 s.7) |
| `src/audio/spu2_memory.cpp` | the SPU2's 2 MB sound RAM, filled by the native CDARB |
| `src/gs/gs_async_backend.cpp` | runs PS2Recomp's CPU rasteriser on its own thread, in order, behind a bounded queue; readers of GS memory wait for it (findings/20 s.4); `BEN10_GS_SYNC=1` turns it off |
| `src/diagnostics/frame_rate.cpp` | with `BEN10_FPS_LOG` set, logs the game's finished frames per second to stderr (`[fps] ...`), to measure speed; off by default |

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
