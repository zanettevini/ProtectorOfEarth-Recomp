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

## 8. The next 45 cases (R13, 2026-11-26)

New cases for R5900-specific instructions the game uses (counts from the
disassembly comments in `generated/default`), plus the rest of the MMI
instructions that read or write HI/LO as 128 bits:

| Group | Cases | In the game |
|---|---|---|
| LDL/LDR/SDL/SDR (alone and as a pair) | 6 | 1,506 LDL, 1,506 LDR, 1,366 SDL, 1,384 SDR |
| QFSRV after MTSAB | 1 | 8 |
| RSQRT.S | 3 | 60 |
| MADD.S/MSUB.S (ACC from ADDA.S) | 3 | 0 (1 ADDA.S) |
| C.F/C.EQ/C.LT/C.LE | 8 | 1,205 C.LT.S, 851 C.EQ.S, 456 C.LE.S |
| MAX.S/MIN.S | 2 | 24, 14 |
| PMFHL (LW, UW, SLW, LH, SH) | 6 | 36 PMFHL.UW |
| PMULTH, PMADDH, PMSUBH, PHMADH, PHMSBH | 5 | 36 PMULTH |
| PMULTW, PMULTUW, PMADDW, PMSUBW, PMADDUW | 5 | 0 |
| PDIVW, PDIVUW, PDIVBW | 3 | 0 |
| PMTHI/PMFHI, PMTLO/PMFLO, PMTHL.LW | 3 | 0 |

Expected values follow PCSX2's interpreter (`pcsx2/MMI.cpp`, `FPU.cpp`,
`R5900OpcodeImpl.cpp`, commit `15afef6`), cited in each case. As in s.4,
each encoding was checked against the disassembly `ps2_recomp` writes.

First run: LDL/LDR/SDL/SDR, QFSRV, plain MADD.S/MSUB.S, the plain compares
and positive RSQRT.S passed. The rest failed, for three reasons:

1. **HI/LO were 32-bit in the MMI translation** (a translator bug). PMTHI
   was `ctx->hi = GPR_U32(ctx, 5)`; PMFHL read only `hi`/`lo`, never
   `hi1`/`lo1`, and built its formats from the wrong lanes; PMULTH and the
   other halfword multiplies added all eight products into one 64-bit sum;
   the word multiplies summed both lanes; PDIVW/PDIVUW/PDIVBW divided one
   lane. **The game runs PMULTH then PMFHL.UW 36 times** (the fixed-point
   loops at `0x1b59d0`-`0x1b6400`), so those loops got wrong values. Fixed
   by patch 0054: runtime helpers that work on the four 32-bit words of HI
   and LO, word for word as PCSX2. All 22 HI/LO cases pass.
2. **Compares and MAX.S/MIN.S used host float order** (a runtime bug by the
   runtime's own float model). In that model exponent 255 is an ordinary
   exponent, so x/0 and overflows leave `0x7FFFFFFF`, which the host reads
   as NaN: `C.LT 1.0, 0x7FFFFFFF` cleared C and `MAX.S 1.0, 0x7FFFFFFF`
   gave 1.0. A denormal compared as a tiny number instead of zero, and
   `std::min(+0, -0)` gave +0. Fixed by patch 0055 (sign-magnitude order of
   the bits, exponent 0 as zero; PCSX2's `fp_max`/`fp_min`).
3. **Differences that stay, as disabled tests** (no expected value was
   changed):
   - The runtime does not keep FCR31's cause and sticky flags (O, U, I, D):
     `DISABLED_RsqrtsNegativeSetsInvalid` (KNOWN_ISSUES R26). The game has
     no CFC1, so it never reads them.
   - PCSX2 and the runtime disagree on exponent-255 values: PCSX2 clamps
     them to `0x7F7FFFFF` (`fpuDouble`, `checkOverflow`), the runtime treats
     them as numbers and saturates to `0x7FFFFFFF`, as it says it checked on
     hardware (ps2autotests). `DISABLED_RsqrtsByZeroGivesSignedFmax`
     (PCSX2 also takes the sign of ft, the runtime the sign of fs),
     `DISABLED_MaddsOverflowGivesFmax`, `DISABLED_ClesInfinityCountsAsFmax`
     (KNOWN_ISSUES R27). The new cases where both agree
     (`CltsFmaxPatternIsLargest`, `MaxsOrdersByBits`) are active.

After patches 0054 and 0055: **86 cases, 81 pass, 5 disabled** (the 4 above
and `DISABLED_MulsDenormalOperandIsZero` from s.7). PS2Recomp's own
`ps2x_tests`: 454/454.

In the game: `ben10_codegen` must run again after these patches, because
the generated code calls the new helpers (the old `PS2_PMFHL_*` macros are
gone; an old `generated/default` does not compile). Regenerating changed
exactly the 36 PMULTH, 36 PMFHL.UW, 24 MAX.S and 14 MIN.S lines; the
compares changed in the runtime macros. The game builds and reaches the
first level as before (screenshot checked). The three PMULTH functions are
only reached through pointers; a 60 s gdb watch in the first level did not
hit them, so where the game uses them is not known yet.

Then 12 more cases, all passing the first time (no bug found): MULT1,
MULTU1, DIV1, DIVU1, MADD1, MADDU1 and the HI1/LO1 moves (each also checks
that pipeline 0's HI/LO stay), MADD and MADDU, PEXT5, PPAC5, and MTSAH
followed by QFSRV. In the game: 13 MULT1, 1 MULTU1, 2 MTHI1/MTLO1, 1
MFHI1/MFLO1, 16 MADD, 16 PPAC5. Now **98 cases, 93 pass, 5 disabled**.

Then 11 VU0 macro cases, from the operations the game uses most (counts in
`generated/default`: 810 VMULx, 576 VADD, 470 VMULAx, 468 each of VMADDw,
VMADDAz and VMADDAy, 397 VSUB, 238 VMUL, ~230 each VADDw/y/z, 205 VWAITQ,
180 VMULq, 124 VRSQRT, 73 VOPMULA/VOPMSUB, 56 VDIV, 25 VSQRT): VMULx, the
VADDbc forms, VSUB, VMUL, VMAX, VMINIx, the ACC matrix-times-vector chain
(VMULAx, VMADDAy, VMADDAz, VMADDw), VOPMULA + VOPMSUB (cross product),
VMR32, VFTOI0/VITOF12, and VDIV/VSQRT/VRSQRT read back through VMULq. 8 pass
as they are. 3 failed, a third translator bug: **VRSQRT computed
1 / sqrt(ft) and ignored fs** (RSQRT Q, fs, ft is fs / sqrt(ft): 6 / sqrt(9)
gave 0.333), VDIV by zero and VSQRT/VRSQRT of a negative value gave Q = 0.
Fixed by patch 0056, which follows PCSX2's VU interpreter (`VUops.cpp`
`_vuDIV`, `_vuSQRT`, `_vuRSQRT`) and this runtime's own VU interpreter:
+-0x7F7FFFFF for a zero divisor, |ft| for the root. The game's 124 VRSQRT
and 56 VDIV all divide vf0.w (1.0), so only their zero and negative cases
change; its 25 VSQRT (which the disassembler prints as `.word`) took 0 for
a negative operand. Now **109 cases, 104 pass, 5 disabled**. After
regenerating, the game builds and reaches the first level as before
(screenshot checked).

CFC2/CTC2 of VI1-VI15 have no case: PCSX2's interpreter keeps 32 bits
there, the runtime 16 (the VI registers' width), so the two references
disagree for values above 0xFFFF.

Still without a case: the rarer VU0 macro operations (VLQI/VSQI, CFC2/CTC2,
VCALLMS, which need VU0 memory or the runtime), and MFSA/MTSA (the runtime
keeps SA in bits, PCSX2 in bytes, so MFSA would differ; the game has
neither).
