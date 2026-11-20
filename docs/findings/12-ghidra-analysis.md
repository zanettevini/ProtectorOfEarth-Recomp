# 12: Ghidra analysis and function inventory

## 1. The question

Phase 1: the function inventory of `SLUS_216.61`, and whether the ~585
suspicious words in `.text` ([09](09-executable.md) s.5.1) are data.

## 2. How it was done

- Ghidra 12.1.4 + ghidra-emotionengine-reloaded 12.1.4 (the same versions
  the owner downloaded), in headless mode (`analyzeHeadless`), JDK 21. Run
  on Linux; the Windows equivalent is `tools/ghidra_analyze.ps1` (**not
  tested**). The Linux one is `tools/ghidra_analyze.sh`.
- Processor: `r5900:LE:32:default` (the only one the extension defines).
- Default auto-analysis (42 s). The extension ran its own R5900 analysers
  (constant references, unaligned-access fix, DVP overlays).
- New script `tools/ghidra_scripts/ApplyScriptApiNames.java` (s.4).
- Export with PS2Recomp's `ExportPS2Functions.java`.

Wrong turn: the first pass of `ApplyScriptApiNames.java` used
`-noanalysis`. The new functions were created, but the calls leaving them
were not followed. A second pass with analysis did not change the count
(4,420), so nothing was lost; but the right procedure is to re-analyse after
creating functions.

## 3. Auto-analysis results

| Measure | Value |
|---|---|
| Functions (auto-analysis only) | 3,575 |
| Exported executable labels | 16,423 |
| `libkernl` functions recognised by the extension | 42 named syscall stubs (`CreateThread`, `WaitSema`, `AddDmacHandler`, `FlushCache`, `sceSifSetDma`...), at `0x001C49E0`-`0x001C5210` |

The 42 stubs go into PS2Recomp's TOML as `stubs`: the runtime has a handler
for each one.

## 4. The script API table

- At `0x002225B0`, in `.data`, there are **1,586 32-bit pairs** `{pointer to
  the name, pointer to the function}`. 1,585 names start with `ass_`; 1 with
  `ass` and no underscore.
- Found by searching the loaded segment for words that point to the `ass_*`
  strings ([09](09-executable.md) s.7): 1,585 pointers, all with a stride of
  2 words, and the next word always inside `.text`.
- Every target is unique (1,586 targets for 1,586 names).
- Before the script, only 741 targets were functions in Ghidra; 629 were
  just labels and 236 not even that. These are functions called **only
  through pointers** (by the engine's script interpreter), which
  auto-analysis does not see.
- `ApplyScriptApiNames.java`: **845 functions created, 1,586 named, 0
  failures.**

## 5. Final inventory

| Measure | Value |
|---|---|
| Functions | **4,420** (2,783 `FUN_*`, 1,586 from the script API, 51 with library names) |
| Median size | 84 bytes |
| Largest function | 13,676 bytes (`FUN_00110850`; two others above 11 KB) |
| `.text` coverage | 95.5% of the bytes are inside some function |
| Gaps | 2,168, totalling 50 KB; the largest is 20 KB (s.6) |

The full CSV stays out of git (`generated/ghidra/`): it is derived from the
game binary.

## 6. The data region at the start of `.text`

- `0x001002E0`-`0x00105200` (20,256 bytes) belongs to no function.
- **All 585 words that do not decode are in it**, and none falls inside a
  function.
- Content: 49% zero words, 58% values below 0x10000, ~600 values that look
  like 32-bit `float`s, no `JAL` pointing into it.
- Conclusion: it is a **data table stored in `.text`** (contents: to be
  determined). PS2Recomp only translates what is in the function CSV, so it
  is already left out. Risk R7 in [KNOWN_ISSUES](../KNOWN_ISSUES.md) is
  mitigated.

## 7. Open

- What the 20 KB table is (who reads those addresses).
- Check the three functions above 11 KB (they may be several functions glued
  together).
- The other, smaller gaps (2,167): alignment, or functions not discovered
  yet.
- Test `tools/ghidra_analyze.ps1` on Windows.
