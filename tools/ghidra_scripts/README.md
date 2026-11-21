# Ghidra scripts

| File | Origin | What it is for |
|---|---|---|
| `ApplyScriptApiNames.java` | this project | creates and names the functions in the engine's script API table (findings/12 s.4) |
| `CreatePointerTargetFunctions.java` | this project | creates the functions reached only through pointers in data (virtual tables, callbacks): rules a-e (findings/15 s.10, findings/18 s.7, findings/20 s.17) |
| `ExportPS2Functions.java` | copy from PS2Recomp (`ps2xRecomp/tools/ghidra/`, commit `e563698`), GPL-3.0, with Ben 10 port additions: split-switch case entries (findings/18 s.6, findings/19 s.7) | exports the function CSV and the config TOML that `ps2_recomp` reads |

Order of use: auto-analysis → `ApplyScriptApiNames.java` → `CreatePointerTargetFunctions.java` → re-analysis →
`ExportPS2Functions.java`. `tools/ghidra_analyze.ps1` (Windows) and
`tools/ghidra_analyze.sh` (Linux) run them all in sequence.
