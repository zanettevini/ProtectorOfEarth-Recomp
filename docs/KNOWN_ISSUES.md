# Known issues and risks

## Blockers

| # | Blocker | Blocks | See |
|---|---|---|---|
| B6 | ~~The boot does not reach the first frame yet~~ resolved: the first screen (memory card warning) is drawn; next, the game sees no memory card | | [findings/15](findings/15-first-boot.md) s.16 |
| B9 | ~~The game reports no memory card in slot 1~~ resolved: libmc bound to the runtime's memory card | | [findings/18](findings/18-memory-card-and-menus.md) |
| B10 | ~~The first level shows a solid blue picture~~ resolved: the main thread had stopped on a switch case and an empty virtual method missing from the function list | | [findings/18](findings/18-memory-card-and-menus.md) s.6-7 |
| B11 | ~~The first level draws the HUD over a black picture~~ resolved: the giant triangles came from the game's software skinning (wrong PROT3W, VU0 macro memory ops writing EE RAM) and VF0 could be overwritten; the level now renders | | [findings/18](findings/18-memory-card-and-menus.md) s.8-12 |
| B12 | ~~Ben did not move in the first level~~ resolved: the keyboard now drives the left stick (patch 0011); the speed is B13 | | [findings/18](findings/18-memory-card-and-menus.md) s.13 |
| B13 | The game runs at about 9 frames per second in the menus and 3-4.4 in the first level (2-core Linux test machine; the same binary varies by up to 20% between runs): the software GS renderer, on its own thread, limits it there (the game thread waits for it a third of the time), then the VU1 interpreter (T2) | playing at full speed | [findings/19](findings/19-windows-build-and-speed.md) s.5, s.8-9; [findings/20](findings/20-vu1-census-and-gs-thread.md) s.11-15 |
| B14 | ~~New Game stopped the game thread at the switch at `0x00185BFC` (resumed through the narrower entry `entry_00185bc8`)~~ resolved: the export adds the switch's case targets as entries; New Game reaches the first level | | [findings/19](findings/19-windows-build-and-speed.md) s.7 |
| B15 | ~~Face buttons opened the co-op pause menu in the level~~ resolved: every pad port mirrored player 1's input; only port 0 has a controller now (patch 0015) | | [findings/20](findings/20-vu1-census-and-gs-thread.md) s.9 |
| B16 | ~~The game stopped in a fight in the first level (indirect call to `0x0020F648`)~~ resolved: 11 virtual methods hidden behind stray words between functions were missing from the function list; two new rules in `CreatePointerTargetFunctions.java` find them | | [findings/20](findings/20-vu1-census-and-gs-thread.md) s.17 |
| B7 | ~~Windows (MSVC) build untested~~ resolved: the MSVC Release build (`windows-release`) compiles, links and reaches the first level; the Windows tests preset is still untested | | [findings/19](findings/19-windows-build-and-speed.md) s.1 |
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
| R13 | R5900-specific instructions used by the game still untested: most VU0 macro operations | silent errors like the ones found so far (4 in s.5, the 128-bit HI/LO and FPU compare bugs in s.8) | keep writing tests ([findings/14](findings/14-repository-build-and-tests.md) s.7-8) |
| R14 | A Sony library function bound to the wrong handler | wrong behaviour that looks like a game bug | bind only with evidence, a family at a time, each entry commented ([findings/15](findings/15-first-boot.md)) |
| R15 | With MSVC, Debug builds keep the runtime and the IOP library unoptimised (`/O2` clashes with `/RTC1`), so the boot may be extremely slow there | the Windows boot looks frozen | build Windows as RelWithDebInfo, or adjust the flags for those two targets ([findings/15](findings/15-first-boot.md) s.12) |
| R16 | IOP module stop/unload (`0x001C9F88`, `0x001CA190`) stay game code with no runtime handler | if the game calls them, it spins in the loader bind | bind when a handler exists ([findings/15](findings/15-first-boot.md) s.9) |
| R17 | Native CDARB reads ignore requests with a count <= 0 and destination kind 8 (logged) | a load that never completes | read CDARB's other path ([findings/17](findings/17-cdarb-and-alasd.md) s.7) |
| R18 | Runtime handlers for GS image upload, the font library, IOP-host buffers and the libc `malloc` family still take guest memory from `guestMalloc`, which starts at the game's heap base | memory corruption in a game that also has its own `sbrk`, if one of those handlers is ever bound (none is for Ben 10) | give them kernel RAM like the SIF packets, or keep them unbound ([findings/15](findings/15-first-boot.md) s.15) |
| R19 | Main thread stack for `SetupThread(stack = -1)`: the runtime returns `sp = 0x02000000 - size`, so the stack is `0x01FE0000`-`0x01FF0000`; where the console kernel puts it was not checked | the heap ends 64 KB lower than it might on hardware | check against a kernel reference before changing it ([findings/15](findings/15-first-boot.md) s.15) |
| R20 | Callback stacks: 8 of 16 KB in kernel RAM (one per thread and nesting depth) | "EE invocation stack space exhausted" if a game nests more | enlarge the window in `ps2_kernel_ram.h` ([findings/15](findings/15-first-boot.md) s.15) |
| R21 | ~~The runtime stores memory card 0 in `mc0/` next to the game's ELF, among the disc files~~ resolved: the cards live in `game/saves/` (or `BEN10_SAVE_DIR`); old cards are copied there on first run | | [findings/18](findings/18-memory-card-and-menus.md) s.14 |
| R22 | `VCALLMS` runs the VU0 microprogram to the end at once; on the console VU0 runs alongside the EE, and code that reads results with `qmfc2.ni` while a program runs, or writes inputs it still reads, can see other values | wrong results in other VU0 micro-mode code | right for Ben 10's skinning (findings/18 s.12); model VU0 timing if a case shows up |
| R23 | MMI PREVH reverses all 8 halfwords; it is believed to reverse within each 64-bit half | wrong results in a game that uses it (Ben 10 does not: no PREVH in the generated code) | check against a reference, then fix like PROT3W (patch 0009) |
| R24 | With a real disc image configured, the EE's `sceCdSearchFile` still gives pseudo LSNs while the IOP reads the image's own sectors | the same failed IOP reads as before patch 0012, if the port is run from an `.iso` | read the image's ISO 9660 directory on the EE side ([findings/19](findings/19-windows-build-and-speed.md) s.6) |
| R25 | On MSVC Release, PS2Recomp's runtime (the VU interpreter included) is compiled with `/fp:fast /arch:AVX2` (`ps2xRuntime/cmake/ReleaseMode.cmake`), the translated VU1 code and the port with the default `/fp:precise`: the compiler may fuse multiply-adds and reorder float math in the interpreter, so VU float results can differ between the interpreter and the translated code on Windows, and from Linux | small differences in VU results (positions, colours) on Windows; the build also needs an AVX2 CPU | build the VU code with `/fp:precise` (and without contraction) on every target, then compare Windows replay checksums with Linux (findings/21 s.10) |
| R26 | The runtime does not keep FCR31's cause and sticky flags (O, U, I, D); only C is kept | a game that reads FCR31 with CFC1 sees 0 (Ben 10 has no CFC1) | test `DISABLED_RsqrtsNegativeSetsInvalid` ready; model the flags if a game needs them ([findings/14](findings/14-repository-build-and-tests.md) s.8) |
| R27 | Exponent-255 floats: PCSX2 clamps them to `0x7F7FFFFF` (and RSQRT.S by zero takes ft's sign), the runtime treats them as numbers and saturates to `0x7FFFFFFF` (fs's sign), which it says matches hardware (ps2autotests) | results differ only after an overflow or a division by zero | 3 disabled tests ready; settle with the ps2autotests results ([findings/14](findings/14-repository-build-and-tests.md) s.8) |
