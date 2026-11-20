# 14: the repository, the full build and the first per-instruction tests

## 1. The question

Phase 2 of the master prompt: does the generated C++ compile and link in
full with the runtime? Do the translated instructions do what the PS2 does?

## 2. The repository

- Layout copied from Mind over Recomp (MoR): `ben10_poe.toml` in place of
  `crash_mom_manifest.toml`, `patches/ps2recomp/` in place of
  `patches/rexglue-sdk/`, a local `generated/`, `docs/findings/`,
  `tools/native_progress.py` for the README board. Decision D9.
- PS2Recomp as a submodule in `thirdparty/PS2Recomp`, pinned to commit
  `e563698` (D8).
- Wrong turn: the first configure failed with "include could not find
  requested file: <project>/ps2xRuntime/cmake/ReleaseMode.cmake". Four of
  PS2Recomp's `CMakeLists.txt` use `${CMAKE_SOURCE_DIR}`, which only points
  to its root when it is the top-level project. Fixed in patch `0001`
  (`${CMAKE_CURRENT_LIST_DIR}/..`).
- The patches are applied by our own `CMakeLists.txt` at configure time, in
  order, only if not applied yet (checked with `git apply --reverse
  --check`). Tested: from a clean submodule at the pinned commit, the first
  configure applies all 5; the second applies nothing.

## 3. The full build

- Target `ben10_codegen`: runs `ps2_recomp` with `ben10_poe.toml`. Same
  result as [13](13-first-recompilation.md): 0 unsupported instructions, 0
  errors.
- Target `ben10_poe`: the ~20 thousand generated `.cpp` + the `main` of
  PS2Recomp's runner + `ps2_runtime`. Unity build (32 files per unit) and
  precompiled headers, like PS2Recomp's `ps2EntryRunner`.
- Wrong turn: the first build stopped with "inlining failed in call to
  always_inline '_mm_blendv_ps': target specific option mismatch". The
  generated code uses SSE4.1; PS2Recomp only sets CPU flags in Release mode,
  and MSVC does not need them. Fix: `-march=x86-64-v2` for GCC/Clang in our
  `CMakeLists.txt` (the same choice as Gitaroo Man Native).
- Result (Linux, GCC 13, `-O0`, 2 cores): **737 units in 4 min 34 s,
  executable linked** (116 MB with debug info). After patches 0002-0005, the
  game was regenerated and built again (s.6).
- **Not tested**: the Windows build with MSVC; an optimised build. Running
  it: [15](15-first-boot.md).

## 4. The per-instruction tests

- `tests/tools/make_test_elf.py` builds a PS2 ELF with one small function
  per case (no game data), plus the CSV and the TOML. The real `ps2_recomp`
  translates it; GoogleTest (`tests/instructions/`) sets up the registers,
  calls the generated function and checks the result.
- This tests the **C++ text the translator writes** for each instruction,
  not just the runtime macros.
- The encoding of each instruction was checked against the disassembly
  `ps2_recomp` itself writes into the generated code.
- Expected values: PS2 behaviour from the R5900 manual and PCSX2's
  interpreter (commit `15afef6`), cited case by case.
- 34 cases: 64-bit integers (ADDU, DADDU, DSRA32), LQ/SQ, delay slots (BEQ,
  BEQL, BNEL), FPU (CVT.W.S, ADD/SUB/MUL/DIV.S, SQRT.S), MMI (PCPYLD,
  PCPYUD, PEXTLW, PADDSH, PADDUH, PSUBUH, PPACH, PNOR, PLZCW), VU0 macro
  (VADD with a destination mask). One disabled on purpose (s.7).

## 5. What the tests found

First run, before any fix: **24 passed, 8 failed.** Each failure was checked
in the generated code before blaming the translator:

| Failure | Cause | Affects Ben 10? |
|---|---|---|
| `LqIgnoresLowFourBits`, `SqIgnoresLowFourBits` | the translator passes `rs + imm` straight to `READ128`/`WRITE128`, which do not align; on the PS2, LQ/SQ ignore the low 4 bits (PCSX2 `LQ`/`SQ`: `addr & ~0xf`) | 6,705 LQ/SQ; only matters when the address is unaligned |
| `CvtWsTruncatesTowardZero`, `...Negative...`, `CvtWsSaturatesPositive` | `FPU_CVT_W_S` was `(int32_t)nearbyintf(x)`: rounds with the current mode (nearest), and 3e9 gives `0x80000000` on x86. The PS2 truncates and saturates (PCSX2 `CVT_W`). Only VU code changes the rounding mode | **566 CVT.W.S** |
| `SqrtsReadsFt`, `SqrtsOfNegativeUsesAbsoluteValue` | the translator reads the operand from `fs`; on the R5900 SQRT.S reads `ft` (PCSX2 `SQRT_S` uses `_FtValUl_`). The game's 85 SQRT.S have `fs = 0`, so **all** of them read `$f0` | **31 of the 85 SQRT.S** use another register and gave wrong results |
| `PsubuhClampsAtZero` | PSUBUH became `_mm_sub_epi16` (wraps around); on the PS2 it saturates at 0 (PCSX2 `_PSUBUH`). PADDUH had the same bug (`_mm_add_epi16`) | 27 PSUBUH |

Found by reading the neighbouring lines: PADDUB/PSUBUB already used the
saturating SSE instructions; only the 16-bit ones were wrong.

Fixes, each in a patch with a header (what broke, why, the fix, how it was
found):

| Patch | What |
|---|---|
| `0002-lq-sq-align-address.patch` | `(rs + imm) & ~0xF` in LQ/SQ |
| `0003-sqrt-s-reads-ft.patch` | `FPU_SQRT_S(ctx->f[ft])` |
| `0004-cvt-w-s-truncate-and-saturate.patch` | `ps2_cvt_w_s()`, with PCSX2's rule |
| `0005-padduh-psubuh-saturate.patch` | `_mm_adds_epu16` / `_mm_subs_epu16` |

After: **33 passed, 0 failed, 1 disabled.** The new case `PadduhSaturates`
was written together with patch 0005 and passed the first time.

## 6. In the real game

After the patches, the game code was regenerated: the SQRT.S now read
`f[12]`, `f[2]`, `f[1]`... instead of always `f[0]`, and the game built
again.

These bugs **would not make the game crash**: they would give wrong numbers
(square roots, integer conversions, 16-bit masks) that would show up as odd
physics, camera or animation, hard to track down by playing the game. That
is why the master prompt asks for per-instruction tests before the first
run.

## 7. What was left out (open)

- **Denormal operand** (exponent 0, non-zero mantissa) in ADD/SUB/MUL.S: the
  PS2 treats it as zero (PCSX2 `fpuDouble`); PS2Recomp's runtime does not
  yet, and says so in a comment. The test
  `DISABLED_MulsDenormalOperandIsZero` exists and is disabled on purpose: it
  is a known difference (KNOWN_ISSUES R10), not a weakened test.
- **LQC2/SQC2** (790 and 445 in the game): they do not align the address
  either, but PCSX2 does not align those two. Without evidence, we leave
  them alone (KNOWN_ISSUES R11).
- **SQRT.S of -0**: PCSX2 returns -0; the runtime returns +0 and says it was
  checked on hardware (ps2autotests). No test until there is an arbiter
  (KNOWN_ISSUES R12).
- The same 4 bugs probably exist in upstream PS2Recomp: worth sending the
  patches to the project (ran-j/PS2Recomp).
- Coverage: 22 of the ~60 R5900-specific instructions the game uses have a
  test. Missing, among others: QFSRV, PMFHL, PEXT5/PPAC5, MADD.S/MSUB.S,
  RSQRT.S, LDL/LDR/SDL/SDR, MULT1/DIV1, the C.*.S comparisons, and the rest
  of the VU0 macro operations.
