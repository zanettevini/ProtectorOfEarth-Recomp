# 13: first recompilation with PS2Recomp

## 1. The question

Can PS2Recomp translate the whole executable? Which instructions does it not
support? Does the generated C++ compile?

## 2. How it was done

- PS2Recomp cloned from `ran-j/PS2Recomp`, commit `e563698`.
- Only the recompiler built (`-DPS2X_BUILD_RECOMP=ON`, the rest `OFF`),
  CMake + Ninja + GCC 13, Linux. It built with no errors.
- Input: the TOML and the CSV exported from Ghidra ([12](12-ghidra-analysis.md)).
- Output: C++ in a local directory outside git (it is the game's code).

Wrong turn: configuring only the `ps2xRecomp` subdirectory fails
(`EnableFastReleaseMode` is defined in the root `CMakeLists.txt`).
Configuring the root with the `PS2X_BUILD_*` options works.

## 3. Recompiler result

| Measure | Value |
|---|---|
| Entries processed | 20,234 (the 4,420 functions + executable labels from the CSV) |
| Recompiled | 20,192; stubs: 42 (the syscalls) |
| Decode failures | **0** |
| **Unsupported instructions** | **0** |
| Errors | 0 |
| Warnings | 2,016, all of the "unresolved JR/JALR" kind |
| Resumable entry points | 86,057 (to continue in the middle of a function after an indirect jump) |
| Time | ~2 s |
| Output | 20,237 C++ files, 158 MB |

The 2,016 warnings are indirect jumps (table `JR` and `JALR`) that static
analysis did not resolve. The recompiler generates **by-address dispatch
entries** for them (a lookup in the table of already compiled functions),
without interpreting. It still has to be checked at run time that every real
target exists in that table.

## 4. Does the generated C++ compile?

Syntax test (`g++ -std=c++20 -msse4.1 -mavx -fsyntax-only`) against the
`ps2xRuntime` headers:

- 12 random files: **12/12 ok**.
- 21 files picked because they contain the most PS2-specific instructions
  (VCALLMS, VU0 macro, QMTC2, MMI such as PCPYLD/PEXTLB/MTSAB, LQ/SQ,
  MADD.S, RSQRT.S): **21/21 ok**.

Not tested here: compiling and linking all 20,237 files (that came in
[14](14-repository-build-and-tests.md)), or running the code.

## 5. How VU0 micro mode is handled

`VCALLMS` becomes `runtime->executeVU0Microprogram(...)`. In the current
runtime that function runs the VU0 microprogram through PS2Recomp's own VU
implementation (`ps2xRuntime/src/lib/vu/`, by interpretation). There is also
a VU1 implementation (`ps2_vu1_core.cpp` and friends). This **corrects**
[01](01-ps2recomp.md) s.4: the documentation read earlier said there was
only VU0 macro mode. It is the temporary debt T2.

## 6. The `gp` register

The ELF report shows `gp=0x0`, but `.reginfo` declares `0x0025D3F0`. The
generated entry code (crt0) writes `gp` itself, so this should not get in the
way. Confirm on the first run.

## 7. Open

- Compile and link everything with `ps2xRuntime` on Windows (MSVC).
- ~~First run: how far boot gets~~: [15](15-first-boot.md).
- Check the targets of the 2,016 indirect jumps at run time.
- ~~Per-instruction tests (master prompt, phase 2)~~: started in
  [14](14-repository-build-and-tests.md).
