# patches/ps2recomp/

Our fixes to [PS2Recomp](https://github.com/ran-j/PS2Recomp). The
`thirdparty/PS2Recomp` submodule stays at the pinned commit (`e563698`); the
`CMakeLists.txt` applies these patches in order at configure time, only the
ones not applied yet. Each file starts with a header: what broke, why, the
fix and how it was found.

| Patch | What | Found in |
|---|---|---|
| `0001-cmake-paths-as-submodule.patch` | CMake paths relative to the file itself, so it works as a submodule | findings/14 s.2 |
| `0002-lq-sq-align-address.patch` | LQ/SQ align the address to 16 bytes | findings/14 s.5 |
| `0003-sqrt-s-reads-ft.patch` | SQRT.S reads its operand from `ft` (R5900), not `fs` | findings/14 s.5 |
| `0004-cvt-w-s-truncate-and-saturate.patch` | CVT.W.S truncates toward zero and saturates | findings/14 s.5 |
| `0005-padduh-psubuh-saturate.patch` | PADDUH/PSUBUH saturate | findings/14 s.5 |
| `0006-kernel-data-in-kernel-ram.patch` | The runtime's own guest data (RPC/TLS pools, SIF command packets, callback stacks) moves to EE kernel RAM; the game's heap may grow up to the main stack | findings/15 s.15 |
| `0007-vu-max-mini-raw-bits.patch` | VU1 MAX/MINI compare raw bits and do not flush denormals (games use MAX as a move of integer data) | findings/18 s.9 |
| `0008-vu0-discard-vf0-writes.patch` | VU0 macro mode: writes to VF0 (QMTC2, LQC2, macro ops) and VI0 are discarded, as on hardware | findings/18 s.11 |
| `0009-mmi-word-halfword-permutes.patch` | MMI PROT3W, PEXEW, PEXCW, PEXEH, PEXCH move the right elements | findings/18 s.12 |
| `0010-vu0-macro-memory-ops.patch` | VU0 macro VLQI/VSQI/VLQD/VSQD/VILWR/VISWR use VU0 data memory; VSQI/VSQD operand fields | findings/18 s.12 |
| `0011-pad-keyboard-wasd-left-stick.patch` | Pad backend: W/A/S/D drive the left analog stick (arrows stay the D-pad) | findings/18 s.13 |
| `0012-cd-shared-sector-numbers.patch` | CD: sceCdSearchFile on the EE uses the IOP's virtual-disc LSNs, so LSNs the game passes to IOP modules match; faster `ownsRamRange` | findings/19 s.3 |
| `0013-vu-interpreter-hot-path.patch` | VU interpreter: cheaper hot path (pipeline commit only when due and only valid slots, fast XGKICK drain, decoded pairs by reference and decoded lazily after a code upload, bit scans for VI/lanes, inline operand normaliser); same results and cycle counts | findings/19 s.9, findings/20 |
| `0014-gs-cpu-row-bands.patch` | GS CPU backend: `SetRowBand` draws only some rows, so several backends can draw one picture on several threads with the same bytes | findings/20 s.7 |
| `0015-pad-only-port-0.patch` | Pad: only port 0, slot 0 has a controller; other ports report DISCONNECTED and read nothing (they mirrored player 1 before) | findings/20 s.9 |
| `0016-gs-cpu-palette-cache.patch` | GS CPU backend: the final colour of each CLUT index (after TEXA) is kept per draw state and rebuilt after a CLUT load; same results | findings/20 s.11 |
| `0017-pad-keyboard-with-gamepad.patch` | Pad backend: the keyboard is read even when a host gamepad is connected (it was switched off entirely); logs which gamepad was found | findings/20 s.16 |
| `0018-vu1-shared-pair-step.patch` | VU1 interpreter: the per-pair step is a header function shared with translated code; a resolver can hand the interpreter one function per pair (VU1 only); decoded-pair types public | findings/21 |
| `0019-vu1-inline-upper-and-hazards.patch` | VU1 interpreter: `execUpper`, `calculatePairReadyCycle` and `markPairWrites` inline in headers, so translated pairs fold through them; same results | findings/21 s.3 |
| `0020-vu1-inline-fmac-shared-upper.patch` | VU1 interpreter: FMAC result/flag helpers inline; `execPairWith` takes the upper executor (translated code shares one per upper word); the commit pass clears only the valid flag; same results | findings/21 s.3 |
| `0021-vu1-inline-lower-executor.patch` | VU1 interpreter: lower executor in an inline header (`execLowerInline`, not included by `ps2_vu1.h`); `execPairWith` takes the lower executor too; same results | findings/21 s.3 |
| `0022-vu1-immediate-vf-acc-writes.patch` | VU1 interpreter: translated pairs write VF/ACC results at issue (hazard tracking and the end-of-program flush unchanged); the interpreter keeps its queues; same results and cycle counts | findings/21 s.3 |
| `0023-vu1-fmac-inline-msvc.patch` | VU1 interpreter: the FMAC result/flag model is forced inline on GCC/Clang only; on MSVC plain `inline` (forcing it into every opcode case made one translated file need over 30 GB to compile); same results | findings/21 s.6 |
| `0024-vu1-xgkick-reset-without-buffer.patch` | VU1 interpreter: resetting the XGKICK state at each program start and each XGKICK no longer zeroes its 64 KB packet buffer (only read below what was copied in); same results | findings/21 s.8 |
| `0025-vu1-executors-per-word-templates.patch` | VU1 interpreter: upper and lower executors as templates on the instruction word (`execUpperT`/`execLowerT`, every opcode case under `if constexpr`), so translated code compiles only its word's case on any compiler; FMAC model forced inline again; same results | findings/21 s.9 |
| `0026-vu1-fmac-fast-path.patch` | VU1 interpreter: exact fast path for the FMAC flag model (no extended-precision recomputation when the float result is a comfortably normal number); same results | findings/21 s.10 |
| `0027-vu1-translated-writes-at-issue-flags-on-demand.patch` | VU1 interpreter: translated code writes stores and latency-1 VI writes at once; the flag pipeline is an issue-order ring applied when flags are read; same results and cycle counts | findings/21 s.14 |
| `0028-vu1-inline-cycle-checks-const-hazards.patch` | VU1 interpreter: per-cycle commit/XGKICK checks inline; translated pairs get the hazard check and write marking unrolled at compile time (pair as a template argument); same results | findings/21 s.15 |
| `0029-vu1-unroll-lanes-loophead-check.patch` | VU1 interpreter: 4-lane loops of the executors and FMAC model unrolled on GCC/Clang; loopHead checks the commit's due cycle inline; same results | findings/21 s.16 |
| `0030-vu1-fmac-operands-once-normalize-fast.patch` | VU1 interpreter: product operands read once per FMAC instruction; normalizeOperand returns at once for exponents 1..254; same results | findings/21 s.17 |
| `0031-vu1-immediate-write-sequence-only-when-queued.patch` | VU1 interpreter: immediate VF/ACC/VI writes take a write sequence only when an older write is queued; same results | findings/21 s.18 |
| `0032-vu0-native-resolver.patch` | VU interpreter: a second resolver hook for the VU0 instance (setNativeResolverVu0), so VU0 microprograms can run translated pairs too; none by default | findings/21 s.19 |
| `0033-vu1-fmac-flags-four-lanes.patch` | VU interpreter: FMAC flags and operand normalisation four lanes at a time with SSE2 when every written lane is in the plain range; same results | findings/21 s.20 |
| `0034-async-vif1-worker.patch` | Optional VIF1/VU1 worker thread (`PS2Memory::setAsyncVif1`, off by default): a VIF1 transfer runs beside the EE and completes for it at fixed points of the EE's execution (register accesses, idle, the next VBlank) | findings/21 s.22 |
| `0035-vu1-fmac-operands-zero-results-in-place-writes.patch` | VU interpreter: FMAC product operands four lanes at a time, exact model out of line, zero results with a zero operand on the fast path, no save/restore around in-place immediate writes; same results | findings/21 s.23 |
| `0036-gs-bilinear-inline-rounding.patch` | GS CPU backend: bilinear filter rounds channels inline instead of calling `lround`; same results | findings/20 s.20 |
| `0037-vu1-translated-blocks.patch` | VU interpreter: the per-pair step for the body of a translated block (no PC/branch/end tail, only the checks that can matter, hazard check on the lanes not settled at build time); for translated code only | findings/21 s.24 |

Patches 0002-0010, 0012, 0015 and 0017 fix PS2 behaviour (0011 is a keyboard-mapping fix of the runtime, 0013, 0014, 0016 and 0019-0022 and 0024-0031, 0033, 0035-0037 speed-ups, 0034 an optional speed-up that moves the end of VIF1 transfers later in EE time, 0023 an MSVC build fix with the same results, 0018 and 0032 hooks for translated VU code) and should also go to upstream
PS2Recomp.

CMake keeps a stamp (`thirdparty/PS2Recomp/.ben10_patches_applied`, the
SHA-256 of the patch set). When the set changes (a patch added, edited,
merged or renamed), configure resets the submodule's files to the pinned
commit and applies every patch again, so work kept in the submodule itself
is lost: make it a patch.

To undo them all: `git -C thirdparty/PS2Recomp checkout .`, then
`git -C thirdparty/PS2Recomp clean -fd` (removes the new files, such as
`ps2xRuntime/include/ps2_kernel_ram.h` from 0006, and the stamp).
