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
| `src/gs/gs_async_backend.cpp` | runs PS2Recomp's CPU rasteriser off the game thread, in order, behind a bounded queue, optionally in N row bands (`BEN10_GS_THREADS=N`); readers of GS memory wait for it (findings/20 s.4, s.7); `BEN10_GS_SYNC=1` turns it off |
| `src/overrides/draw_wait.cpp` | the game's frame wait loop (`FUN_0012e580`) natively, with a 1024-cycle time step per turn instead of 32 (findings/20 s.8); `BEN10_DRAW_WAIT_STEP` |
| `src/saves/save_location.cpp` | memory cards in `game/saves/` (or `BEN10_SAVE_DIR`) instead of directly next to the ELF, copying old cards there on first run (findings/18 s.14) |
| `src/vu/async_vu1_install.cpp` | `BEN10_ASYNC_VU1=1` (off by default): VIF1 and the VU1 programs run on a worker thread beside the game thread (patch 0034, findings/21 s.22); `BEN10_ASYNC_VU1_CYCLES=N` adds an EE-cycle limit for completion |
| `src/vu/vu1_native.h`, `vu1_native_resolver.cpp`, `vu1_native_install.cpp` | translated VU code (D14 phase 1, findings/21): the tables `tools/vu1_translate` generates into `generated/vu1/`, the resolvers that match them to the VU1 and VU0 code memory pair by pair (s.19), and the override that installs them (`BEN10_VU1_NATIVE=0` turns both off, `BEN10_VU0_NATIVE=0` only VU0) |
| `src/diagnostics/frame_rate.cpp` | with `BEN10_FPS_LOG` set, logs the game's finished frames per second to stderr (`[fps] ...`), to measure speed; off by default |
| `src/diagnostics/key_log.cpp` | with `BEN10_KEY_LOG` set, logs the mapped keys raylib sees as held and the window focus (`[keys] ...`), to diagnose keyboard input (findings/20 s.16); off by default |
| `src/diagnostics/sample_profiler.cpp` | Windows only: with `BEN10_PROFILE` set (to an output file, or `1` for `profile.txt`), samples the game thread's call stack about every millisecond (`BEN10_PROFILE_THREAD` picks another named thread); `tools/perf/win_profile.py` reads it with the build's `ben10_poe.map` (findings/21 s.7); off by default |

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
