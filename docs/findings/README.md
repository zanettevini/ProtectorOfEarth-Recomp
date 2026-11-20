# Findings: what is known so far

Reverse-engineering notes about the game and the port, one file per
investigation, in the order they happened. Like almost all of the project,
they were written with an AI assistant; every conclusion was checked with
tools (readelf, Ghidra, the recompiler itself, tests), and the wrong turns
stay in the text because they are part of how the answer was found.
Corrections are welcome.

Findings 01-13 were written in the study phase, before the repository
existed: an extraction folder or "study folder" there corresponds today to
`game\` and `out\disc\` (see docs/01-building.md).

Each file is one investigation. Look here before investigating something
again. Format of each finding: see [AGENTS.md](../../AGENTS.md) s.3.

| # | Subject | State |
|---|---|---|
| [01](01-ps2recomp.md) | PS2Recomp: what it does, what is stubbed, licence, fit with the master prompt | study, later checked against the code |
| [02](02-reference-projects.md) | PS2 ports already done with PS2Recomp (Gitaroo Man, DBZ BT2, DBZ BT3) and what to copy from them | study |
| [03](03-the-game.md) | The game: versions, serials, Quantum3 engine, `GAME.DIR`, debug options | study + disc confirmed |
| [04](04-ps2-hardware.md) | Map of the PS2 hardware and what each part demands from the runtime | study |
| [05](05-vulkan-renderer.md) | Vulkan renderer options for the GS (paraLLEl-GS and alternatives) | study |
| [06](06-master-prompt-conflicts.md) | Where the master prompt conflicts with what the other ports do | decided (temporary) |
| [07](07-phase0-environment.md) | Phase 0: development environment and what is missing | measured |
| [08](08-tools.md) | Tools: Ghidra + EE extension, PCSX2 as a reference, ISO extraction | study |
| [09](09-executable.md) | The `SLUS_216.61` ELF: layout, sections, instruction distribution, hardware, strings, `ass_*` API | measured |
| [10](10-iop-modules.md) | The disc's 15 IRX, the 8 the game loads, `IOPRP300.IMG` | measured |
| [11](11-vu-microprograms.md) | VU microprograms in `.vutext` and the `.DVP.ovlytab` table | measured |
| [12](12-ghidra-analysis.md) | Headless Ghidra: 4,420 functions, the script API table (1,586), the 20 KB data region | measured |
| [13](13-first-recompilation.md) | First recompilation with PS2Recomp: 0 unsupported instructions, the C++ compiles (sample) | measured |
| [14](14-repository-build-and-tests.md) | Repository, full build (links), per-instruction tests; 4 translator bugs fixed | measured |
| [15](15-first-boot.md) | First boot: Sony libraries identified by RPC SID and bound to runtime handlers, one blocker at a time | in progress |
| [16](16-game-dir-and-wad.md) | `GAME.DIR` / `GAME.WAD`: the index layout (423 entries of 72 bytes), sector-aligned archive, what is inside | measured |
| [17](17-cdarb-and-alasd.md) | The game's IOP modules: CDARB (disc streaming, SID 0x01212122) and ALASD (sound engine, SID 0x01212121); CDARB's RPC table; what a native replacement needs | in progress |
