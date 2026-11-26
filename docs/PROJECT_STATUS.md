# Project status

**Phases 0 and 1: done. Phase 2 (recompiler): in progress. Phase 7 (first
execution): started.** The game's code is translated, compiles, links and
starts running on Linux and on Windows (MSVC Release, findings/19); the boot gets through SIF, RPC, CD init, the IOP
reboot, file I/O, IOP heap and module loading; it reads `GAME.DIR` and
`GAME.WAD`, loads the 8 IOP modules, sets the video mode, opens the pads,
reads the game's data through a native replacement of CDARB's EE reads,
finds the memory card (libmc bound to the runtime's card, findings/18),
creates its save, shows the title screen and the main menu, and loads the
first level, which now renders (findings/18 s.12): the last blocker was the
game's software skinning, broken by a wrong PROT3W translation and by VU0
macro-mode memory instructions that wrote EE RAM (patches 0009-0010), after
a VF0 fix (patch 0008). Ben walks from the keyboard (patch 0011). New Game reaches
the first level, which is playable (walking, combat, the Omnitrix). It runs
at about 9 fps in the menus and 3-4.4 fps in the first level on the 2-core
Linux test machine; the software GS renderer (on its own thread) and the
VU1 interpreter are the limits (findings/19-20, KNOWN_ISSUES B13). Saves
live in `game/saves/` (findings/18 s.14).
On the way, patch 0006 moved the runtime's own guest data to EE kernel RAM,
because the game's heap needs the memory the runtime kept at the top of RAM
(findings/15 s.15).

Read first: [INITIAL_AUDIT.md](INITIAL_AUDIT.md) (what is known about the
game) and the [README](../README.md#-going-native) board.

## Done

- Study, analysis of the disc, the ELF, the IRX modules and the VU
  microprograms ([findings/01](findings/01-ps2recomp.md)-[11](findings/11-vu-microprograms.md)).
- Headless Ghidra: 4,420 functions, script API named
  ([findings/12](findings/12-ghidra-analysis.md)).
- First recompilation: 0 unsupported instructions
  ([findings/13](findings/13-first-recompilation.md)).
- Repository in the Mind over Recomp format, PS2Recomp as a pinned
  submodule, 5 patches applied by CMake; full build; end-to-end
  per-instruction tests; 4 translator bugs found and fixed
  ([findings/14](findings/14-repository-build-and-tests.md)).
- First screen: the memory card warning, drawn by the game
  ([findings/15](findings/15-first-boot.md) s.16).
- First boot: 23 Sony library functions (libsif, libcdvd, IOP reboot)
  identified with evidence and bound to runtime handlers; the boot moved
  past 4 blockers ([findings/15](findings/15-first-boot.md)).
- Repository translated to English (D7).
- Main menu and the first level: memory card, menus, black models (patch
  0007), VF0 (0008), software skinning (0009-0010); the level renders
  ([findings/18](findings/18-memory-card-and-menus.md)).

## Tests run

| What | Where | Result |
|---|---|---|
| `ctest` (98 cases: 91 instruction + 2 memory layout active, 5 disabled on purpose, findings/14 s.8) | Linux, GCC 13 | **93/93 pass** |
| PS2Recomp's own `ps2x_tests` with patches 0001-0055 (built separately, Release) | Linux, GCC 13 | **454/454 pass** (run from `ps2xTest/` or the submodule root) |
| The same, before patches 0002-0005 | Linux | 24 passed, 8 failed (findings/14 s.5) |
| Full `ben10_poe` build, after each binding batch | Linux, GCC 13, `-O0` | compiles and links |
| Boot runs under Xvfb, sampled with gdb | Linux | moves past each bound blocker (findings/15) |
| `tools/ghidra_analyze.sh` | Linux | reproduces the same function list (20,234 identical entries) |
| Patches applied by CMake | Linux | 0001-0055 apply in order on a clean checkout of the pinned commit and give exactly the patched tree (`git apply` on a worktree); CMake resets the submodule and reapplies them all when the patch set changes (stamp file) |
| VU1 replay (`ben10_vu1_replay --sum`), 40 workloads from the first level | Linux | identical checksums before and after patch 0013 (findings/20 s.3) |
| VU replay after patches 0045-0046 (flag ring, flag results left out in blocks), 40 VU1 and 60 VU0 workloads | Linux | identical checksums interpreted, translated and translated without blocks; every flag read (cycle and value) the same interpreted and translated (findings/20 s.28) |
| GS replay (`ben10_gs_replay`), 15 s of the first level (695,840 backend calls) and random recordings (`ben10_gs_synth`) | Linux | 2, 3, 4, 6 and 16 row bands give the same GS memory as PS2Recomp's GSCpuBackend on the level and on eight random recordings; before the fixes of findings/20 s.21 and s.24 they did not, and differed run to run |
| Windows, first level with `BEN10_ASYNC_VU1=1`, after patches 0037-0038 | Windows, MSVC, Release | builds (translated blocks included); 16.5-27 game frames per second; the VIF1 worker is no longer the limit, the GS bands are next (findings/21 s.26) |
| First level, under Xvfb | Linux | renders (screenshot 06); Ben walks with W (screenshot 07); reached from New Game too (findings/19 s.7); attacks and jumps (screenshot 09, findings/20 s.9); Omnitrix and the first fight (screenshot 10, s.10) |
| Frame rate (`BEN10_FPS_LOG`) | Linux, game code `-O0` | menus: 3 fps before patch 0012, 8-9 now; first level: 1.4 before patch 0013, 4.4 now (findings/19-20) |
| Full `ben10_poe` build, `windows-release` | Windows, MSVC 17.14 | compiles and links; reaches the first level's Omnitrix tutorial (screenshot 08); 3-4 fps before patch 0012, 7 fps on the title screen after; New Game then stopped (B14, fixed since on Linux, not yet rebuilt on Windows) |
| `tools/collect_disc.ps1` (study version) | Windows | ok |

**Untested**: on Windows, `ghidra_analyze.ps1`, the repository version of
`collect_disc.ps1`, the `windows-debug` and `windows-tests` presets; the
first screen compared with PCSX2; the first level compared with PCSX2;
gameplay past the first steps.

## Unresolved

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md): temporary debts T1 (interpreted
IOP) and T2 (interpreted VU), risks R10-R24.

## Next task

See [NEXT_TASK.md](NEXT_TASK.md).
