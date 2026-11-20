# Known issues and risks

## Blockers

| # | Blocker | Blocks | See |
|---|---|---|---|
| B6 | ~~The boot does not reach the first frame yet~~ resolved: the first screen (memory card warning) is drawn; next, the game sees no memory card | | [findings/15](findings/15-first-boot.md) s.16 |
| B9 | ~~The game reports no memory card in slot 1~~ resolved: libmc bound to the runtime's memory card | | [findings/18](findings/18-memory-card-and-menus.md) |
| B10 | ~~The first level shows a solid blue picture~~ resolved: the main thread had stopped on a switch case and an empty virtual method missing from the function list | | [findings/18](findings/18-memory-card-and-menus.md) s.6-7 |
| B11 | The first level draws the HUD and one piece of ground over a black picture, at about 1 frame per second (textured models were black: fixed by patch 0007; the rest is not explained yet) | gameplay | [findings/18](findings/18-memory-card-and-menus.md) s.8-10 |
| B7 | Windows (MSVC) build untested | playing on Windows | [findings/14](findings/14-repository-build-and-tests.md) s.3 |
| B8 | ~~The test machine lacked the disc data files~~ resolved: `GAME.DIR`, `IOPRP300.IMG` and `GAME.WAD` (SHA-1 checked) are now in `game/` on the test machine | | [findings/15](findings/15-first-boot.md) s.8 |

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
| R15 | With MSVC, Debug builds keep the runtime and the IOP library unoptimised (`/O2` clashes with `/RTC1`), so the boot may be extremely slow there | the Windows boot looks frozen | build Windows as RelWithDebInfo, or adjust the flags for those two targets ([findings/15](findings/15-first-boot.md) s.12) |
| R16 | IOP module stop/unload (`0x001C9F88`, `0x001CA190`) stay game code with no runtime handler | if the game calls them, it spins in the loader bind | bind when a handler exists ([findings/15](findings/15-first-boot.md) s.9) |
| R17 | Native CDARB reads ignore requests with a count <= 0 and destination kind 8 (logged) | a load that never completes | read CDARB's other path ([findings/17](findings/17-cdarb-and-alasd.md) s.7) |
| R18 | Runtime handlers for GS image upload, the font library, IOP-host buffers and the libc `malloc` family still take guest memory from `guestMalloc`, which starts at the game's heap base | memory corruption in a game that also has its own `sbrk`, if one of those handlers is ever bound (none is for Ben 10) | give them kernel RAM like the SIF packets, or keep them unbound ([findings/15](findings/15-first-boot.md) s.15) |
| R19 | Main thread stack for `SetupThread(stack = -1)`: the runtime returns `sp = 0x02000000 - size`, so the stack is `0x01FE0000`-`0x01FF0000`; where the console kernel puts it was not checked | the heap ends 64 KB lower than it might on hardware | check against a kernel reference before changing it ([findings/15](findings/15-first-boot.md) s.15) |
| R20 | Callback stacks: 8 of 16 KB in kernel RAM (one per thread and nesting depth) | "EE invocation stack space exhausted" if a game nests more | enlarge the window in `ps2_kernel_ram.h` ([findings/15](findings/15-first-boot.md) s.15) |
| R21 | The runtime stores memory card 0 in `mc0/` next to the game's ELF (`game/mc0/`), not in a per-user folder | players' saves mixed with the disc files; lost if `game/` is replaced | move to a per-user folder, copying any existing save ([findings/18](findings/18-memory-card-and-menus.md) s.4) |
