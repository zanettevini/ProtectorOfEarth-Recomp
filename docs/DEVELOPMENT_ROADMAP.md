# Development roadmap

The phases are the master prompt's ([MASTER_PROMPT.md](MASTER_PROMPT.md)
s.6). This page lists what each depends on and the first concrete step.

| Phase | Topic | Depends on | Next step | Status |
|---|---|---|---|---|
| 0 | Environment and repository | — | — | **done** |
| 1 | Executable analysis | disc, Ghidra | — | **done** ([INITIAL_AUDIT](INITIAL_AUDIT.md)) |
| 2 | Recompiler | 1 | more per-instruction tests (R13) | **in progress**: full build links; 33 tests pass; 4 translator bugs fixed ([findings/14](findings/14-repository-build-and-tests.md)) |
| 3 | EE + VU runtime | 2, D3 | VU microprogram census | not started |
| 4 | System services | 1, D2 | **in progress**: libsif, libcdvd and IOP reboot bound to the runtime ([findings/15](findings/15-first-boot.md)) | in progress |
| 5 | Vulkan graphics | 3, 4, D4 | integrate paraLLEl-GS; first frame | not started |
| 6 | Audio and input | 4 | SDL3 for input; SPU2 | not started |
| 7 | First execution | 3-6 | **in progress**: the boot runs, one blocker at a time ([findings/15](findings/15-first-boot.md)) | in progress |
| 8 | Compatibility | 7 | every level, bosses, cutscenes, comparison with PCSX2 | not started |
| 9 | PC enhancements | 8 | resolution, widescreen, options | not started |

## Immediate priorities

1. Keep the boot moving, one blocker at a time (findings/15).
2. A run with `GAME.WAD` on the test machine, or a Windows build on the
   maintainer's PC (B7, B8).
3. More per-instruction tests (R13).
