# Known issues and risks

## Blockers

| # | Blocker | Blocks | See |
|---|---|---|---|
| B6 | The boot does not reach the first frame yet | phases 3-7 | [findings/15](findings/15-first-boot.md) |
| B7 | Windows (MSVC) build untested | playing on Windows | [findings/14](findings/14-repository-build-and-tests.md) s.3 |
| B8 | The test machine has `SLUS_216.61`, `SYSTEM.CNF`, the IRX modules, `GAME.DIR` and `IOPRP300.IMG`, but not `GAME.WAD` (2.4 GB) | the boot waits in `sceCdSearchFile("\\GAME.WAD;1")` | [findings/15](findings/15-first-boot.md) s.8 |

## Temporary debts (they break the master prompt on purpose, for now)

| # | Debt | Why | Way out |
|---|---|---|---|
| T1 | IOP run by `ps2xIOP` (R3000A interpretation) | fastest way to a boot (D2) | native services for the 8 modules ([findings/10](findings/10-iop-modules.md)) |
| T2 | VU1/VU0 micro mode may run on an interpreter | first frame (D3) | static translation from `.DVP.ovlytab` ([findings/11](findings/11-vu-microprograms.md)) |

## Technical risks

| # | Risk | Why it matters | Mitigation |
|---|---|---|---|
| R1 | VU1 only through interpretation in PS2Recomp | almost every PS2 game draws through VU1 | static microprogram translation (the `vu1lift`/`vurecomp` approach), with a runtime census to confirm the list |
| R2 | paraLLEl-GS crashes on AMD drivers on Windows | reported on AMD Polaris/GCN GPUs by BT3-Recomp; other AMD generations untested | test early; keep the renderer behind an interface |
| R3 | The EE FPU is not IEEE | physics and collision can diverge without correct clamping | per-instruction tests compared with PCSX2 |
| R4 | Code loaded at run time | conflicts with "no interpreter" | measured: none found ([findings/09](findings/09-executable.md) s.8); to confirm in Ghidra |
| R5 | Format of `GAME.DIR`/`GAME.WAD` (Quantum3 engine) unknown | file access | the runtime reads through the game's own file calls; the format is not needed at first |
| R6 | PS2Recomp is experimental | recompiler bugs become silent game bugs | per-instruction tests, unresolved-call logs, documented patches |
| R7 | 20 KB data table inside `.text` (`0x001002E0`) | translated as code it would produce garbage | mitigated: outside every function, never recompiled ([findings/12](findings/12-ghidra-analysis.md) s.6) |
| R8 | Third-party audio driver (`ALASD`, "Argonaut") and `CDARB` with no documentation | native audio services need its protocol | start from the EE-side RPC calls (custom SIDs 0x1212121/0x1212122, [findings/15](findings/15-first-boot.md) s.5) |
| R9 | 2,016 indirect jumps not resolved statically | a target missing from the dispatch table stops the game | check at run time ([findings/13](findings/13-first-recompilation.md) s.3) |
| R10 | Denormal operand in ADD/SUB/MUL.S does not become zero (documented by the PS2Recomp runtime) | tiny results where the PS2 gives 0 | test `DISABLED_MulsDenormalOperandIsZero` ready; fix in the runtime ([findings/14](findings/14-repository-build-and-tests.md) s.7) |
| R11 | LQC2/SQC2 do not align the address (PCSX2 does not either) | 1,235 uses in the game | decide with hardware evidence |
| R12 | SQRT.S of -0: PCSX2 gives -0, the runtime gives +0 | rare | no arbiter yet |
| R13 | ~38 R5900-specific instructions used by the game still untested | silent errors like the 4 already found | keep writing tests ([findings/14](findings/14-repository-build-and-tests.md) s.7) |
| R14 | A Sony library function bound to the wrong handler | wrong behaviour that looks like a game bug | bind only with evidence, a family at a time, each entry commented ([findings/15](findings/15-first-boot.md)) |
