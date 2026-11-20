# generated/

Everything here is made on your machine from YOUR disc and is **never
versioned** (`.gitignore`):

| Folder | What it is | Made by |
|---|---|---|
| `generated/ghidra/` | the Ghidra project and `functions.csv`, the function list the recompiler reads | `tools/ghidra_analyze.sh` / `.ps1` |
| `generated/default/` | the generated C++: the game's code, translated, ~20 thousand files | the CMake target `ben10_codegen` |

To redo it from scratch, delete the folder and run both steps again
(docs/01-building.md).
