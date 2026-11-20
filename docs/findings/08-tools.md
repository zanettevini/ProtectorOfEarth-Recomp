# 08: analysis and reference tools

## 1. Ghidra + Emotion Engine: Reloaded

- Extension [chaoticgd/ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded)
  (Apache-2.0). Based on the old `beardypig/ghidra-emotionengine`, which was
  archived.
- Disassembles and decompiles EE-specific instructions (MMI, VU0 macro mode).
- Recovers types, functions and globals when the ELF has a `.mdebug` section
  (STABS analyser).
- **Imports PCSX2 save states**: lets you look at the memory of the running
  game, including code loaded after boot.
- An R5900 constant-reference analyser fixes references to globals.
- The extension version must match the Ghidra version.
- PS2Recomp ships the `ExportPS2Functions.java` script, which exports
  Ghidra's function list for the recompiler ([01](01-ps2recomp.md)).

## 2. PCSX2 as a reference

- Reference only (master prompt s.2). Uses: comparing CPU and memory state,
  GS captures (GS frame dumps), save states for Ghidra, memory card saves to
  reach later levels.
- Gitaroo Man uses a **PINE** client (PCSX2's IPC protocol) to read the
  memory of the running game. Worth copying the idea.
- GPL-3.0 licence: reading its code as a reference is fine; copying code
  requires keeping the licence and the credit.

## 3. Extracting the disc

- The ISO is a plain ISO 9660 / UDF file system: 7-Zip opens it. The
  `SYSTEM.CNF` at the root names the boot ELF.
- The result goes into an ignored folder and is **never** modified.
- Tool: `tools/collect_disc.ps1` extracts what is needed, records the ISO's
  hash and writes a manifest (names, sizes, hashes) into `out/`, with no game
  content.
