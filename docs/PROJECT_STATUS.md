# Project status

**Phases 0 and 1: done. Phase 2 (recompiler): in progress. Phase 7 (first
execution): started.** The game's code is translated, compiles, links and
starts running on Linux; the boot gets through SIF, RPC, CD init, the IOP
reboot, file I/O, IOP heap and module loading; it reads `GAME.DIR` and
`GAME.WAD`, loads the 8 IOP modules, sets the video mode, opens the pads,
reads the game's data through a native replacement of CDARB's EE reads,
finds the memory card (libmc bound to the runtime's card, findings/18),
creates its save, shows the title screen and the main menu, and loads the
first level, which now renders (findings/18 s.12): the last blocker was the
game's software skinning, broken by a wrong PROT3W translation and by VU0
macro-mode memory instructions that wrote EE RAM (patches 0009-0010), after
a VF0 fix (patch 0008). Ben walks from the keyboard (patch 0011); it runs at
about 2-3 fps.
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
| `ctest` (41 cases: 38 instruction + 2 memory layout active, 1 disabled on purpose) | Linux, GCC 13 | **40/40 pass** |
| PS2Recomp's own `ps2x_tests` with patches 0001-0011 (built separately) | Linux, GCC 13 | **450/450 pass** (run from `ps2xTest/`) |
| The same, before patches 0002-0005 | Linux | 24 passed, 8 failed (findings/14 s.5) |
| Full `ben10_poe` build, after each binding batch | Linux, GCC 13, `-O0` | compiles and links |
| Boot runs under Xvfb, sampled with gdb | Linux | moves past each bound blocker (findings/15) |
| `tools/ghidra_analyze.sh` | Linux | reproduces the same function list (20,234 identical entries) |
| Patches applied by CMake | Linux | 0001-0011 apply in order on a clean checkout of the pinned commit and give exactly the patched tree (`git apply` on a worktree); each reverse-applies on the patched tree |
| First level, under Xvfb | Linux | renders (screenshot 06); Ben walks with W (screenshot 07) |
| `tools/collect_disc.ps1` (study version) | Windows | ok |

**Untested**: everything on Windows except the study version of the disc
collection (MSVC, `ghidra_analyze.ps1`, the repository version of
`collect_disc.ps1`); an optimised build; the first screen compared with
PCSX2; the first level compared with PCSX2; gameplay.

## Unresolved

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md): temporary debts T1 (interpreted
IOP) and T2 (interpreted VU), risks R10-R23.

## Next task

See [NEXT_TASK.md](NEXT_TASK.md).
