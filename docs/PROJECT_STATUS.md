# Project status

**Phases 0 and 1: done. Phase 2 (recompiler): in progress. Phase 7 (first
execution): started.** The game's code is translated, compiles, links and
starts running on Linux; the boot gets through SIF, RPC, CD init, the IOP
reboot and the file I/O library, reads `GAME.DIR`, and now waits for
`GAME.WAD` (findings/15 s.8, findings/16).

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
- First boot: 23 Sony library functions (libsif, libcdvd, IOP reboot)
  identified with evidence and bound to runtime handlers; the boot moved
  past 4 blockers ([findings/15](findings/15-first-boot.md)).
- Repository translated to English (D7).

## Tests run

| What | Where | Result |
|---|---|---|
| `ctest` (34 cases: 33 active, 1 disabled on purpose) | Linux, GCC 13 | **33/33 pass** |
| The same, before patches 0002-0005 | Linux | 24 passed, 8 failed (findings/14 s.5) |
| Full `ben10_poe` build, after each binding batch | Linux, GCC 13, `-O0` | compiles and links |
| Boot runs under Xvfb, sampled with gdb | Linux | moves past each bound blocker (findings/15) |
| `tools/ghidra_analyze.sh` | Linux | reproduces the same function list (20,234 identical entries) |
| Patches applied by CMake | Linux | 5 applied on a clean submodule; nothing re-applied on the 2nd configure |
| `tools/collect_disc.ps1` (study version) | Windows | ok |

**Untested**: everything on Windows except the study version of the disc
collection (MSVC, `ghidra_analyze.ps1`, the repository version of
`collect_disc.ps1`); an optimised build; a run with the full disc
(`GAME.WAD` is still missing on the test machine: KNOWN_ISSUES B8).

## Unresolved

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md): temporary debts T1 (interpreted
IOP) and T2 (interpreted VU), risks R10-R14.

## Next task

See [NEXT_TASK.md](NEXT_TASK.md).
