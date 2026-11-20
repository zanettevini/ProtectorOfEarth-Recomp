# Ghidra scripts

| File | Origin | What it is for |
|---|---|---|
| `ApplyScriptApiNames.java` | this project | creates and names the functions in the engine's script API table (findings/12 s.4) |
| `ExportPS2Functions.java` | unmodified copy from PS2Recomp (`ps2xRecomp/tools/ghidra/`, commit `e563698`), GPL-3.0 | exports the function CSV and the config TOML that `ps2_recomp` reads |

Order of use: auto-analysis → `ApplyScriptApiNames.java` → re-analysis →
`ExportPS2Functions.java`. `tools/ghidra_analyze.ps1` (Windows) and
`tools/ghidra_analyze.sh` (Linux) run them all in sequence.
