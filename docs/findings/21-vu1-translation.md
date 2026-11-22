# 21: Translating the VU microprograms at build time (D14, phase 1)

Continues [20](20-vu1-census-and-gs-thread.md) s.14: in the first level the
game thread's largest cost is PS2Recomp's VU1 interpreter (KNOWN_ISSUES T2),
and on the maintainer's Windows desktop the game thread is what limits the
first level (s.18 there). Decision D14 plans a build-time translation, the
interpreter staying as the reference.

## 1. What phase 1 is

A translation that gives the interpreter's results **by construction**, as
the base for the faster phases:

- **Input.** The game's ELF: `.DVP.ovlytab` lists 32 overlay entries (name
  offset, LMA in `.vutext`, VMA in VU micro memory, findings/11). Each
  entry's chunk runs from its LMA to the next entry's LMA, at most 0x800
  bytes. Checked against the VU1 code memory captured in the level
  (findings/20 s.2): entries #13-#19 match their VMA range pair for pair
  (256 of 256); the 8 bytes between two chunks of a group are the next
  chunk's VIF tag; a group's last chunk ends with a few bytes that are not
  code (entry #20: 135 of its 143 pairs matched the capture).
- **Translator** (`tools/vu1_translate/vu1_translate.cpp`, run by CMake):
  decodes every distinct (address, 64-bit pair) of every chunk with the
  interpreter's own decoder, 6,727 pairs, and writes for each one the
  decoded pair as a `constexpr` object and a function that runs
  `VU1Interpreter::execPair` (the interpreter's per-pair step, patch 0018,
  forced inline) with it. The compiler drops every branch the step takes
  on the decoded fields. Output: 32 files plus an index in `generated/vu1/`
  (the game's code: never versioned), built into the `ben10_vu1_native`
  library at `-O2`.
- **Run time.** Patch 0018 lets the game give the VU1 interpreter a
  resolver. When the code memory may have changed (a new upload
  generation), the resolver (`src/vu/vu1_native_resolver.cpp`) fills one
  entry per instruction pair: the translated function whose pair has the
  same address and the same 64 bits, or nothing. The interpreter calls the
  function where there is one and decodes and steps the pair itself
  otherwise. Pairs are matched one by one because the overlay groups
  overlap in VU memory and the game uploads code several times a frame.
  `src/vu/vu1_native_install.cpp` installs the resolver;
  `BEN10_VU1_NATIVE=0` turns it off.

Why this is exact: a pair's decoding depends only on its 64 bits (and the
unit, VU1), and the translated function runs the same step code with that
decoding.

## 2. Results

- **Same results.** `ben10_vu1_replay --sum --native` prints the same
  checksums as the interpreter for all 40 captured workloads (registers,
  flags, cycle counts, VU data memory, GS memory). ps2xTest 454/454.
- **Coverage.** In the replay every pair ran translated (the interpreter's
  decoder was never called).
- **Speed (small, as expected for phase 1).** `callgrind`, 40 workloads x
  5: VU code 794 M instructions with the interpreter, 677 M translated
  (-15%). The replay passes no `PS2Memory`, so its interpreter decodes
  every pair at every step (findings/20 s.14); against an interpreter that
  keeps its decoded pairs, as in the game, the saving is about 7%. In the
  first level (Linux, 90 s): game thread 193 ms of CPU per game frame,
  inside the spread of earlier runs (197-221 ms). The picture is the same;
  no `missing-target`.
- Build cost: translating takes under a second; compiling the 6,727 pair
  functions about 1.5 minutes on the 2-core Linux machine.

## 3. Phase 2a: the step's helpers inline (patch 0019)

`callgrind` of the translated replay showed the rest of the per-pair cost
in functions the step called out of line, so the constant pair could not
be folded through them: `execUpper` (a switch on the opcode, the dest mask
in loops), `calculatePairReadyCycle` and `markPairWrites` (loops over the
decoded register sets). Patch 0019 moves them unchanged into headers,
forced inline (the four field extractions of `execUpper` are written out,
because the short helper names clash with ps2xRecomp macros).

| VU code, 40 workloads x 5 (`callgrind`) | Instructions |
|---|---|
| interpreter (replay: decodes at every step) | 793 M |
| translated, phase 1 | 677 M |
| + `execUpper` inline | 610 M |
| + hazard check and write bookkeeping inline | 540 M |
| + FMAC helpers inline, shared upper functions, lighter commit (patch 0020) | 446 M |
| + lower executor per distinct lower word (patch 0021) | 418 M |
| + VF/ACC results written at issue (patch 0022) | 349 M |

So -56% against the replay's interpreter, about -51% against an
interpreter that keeps its decoded pairs (as in the game). Same checksums
with and without `--native`; ps2xTest 454/454. Building `ben10_vu1_native`
now takes about 3.5 minutes on the 2-core Linux machine. On Windows (MSVC
Release) the whole build went from 8 to 24 minutes, the translated files
compiling slowly; the translator now writes 32 smaller files instead of 8
so they compile on all cores at once.

In the first level (Linux, 90 s, `tools/perf/cpu_per_frame.sh`): game
thread 169 ms of CPU per game frame, against 193 ms with phase 1 and
197-221 ms before translation; same picture, no `missing-target`.

Patch 0020 (phase 2b):

- The FMAC result and flag helpers (`normalizeFmacResult`,
  `calculateFmacExactResult`, which decoded the upper word again for each
  lane, `updateFmacFlags`, ...) move unchanged to an inline header.
- Inlining the whole upper executor and FMAC model into each of the 6,727
  pairs gave 440 M but made the Linux build about 4 times longer (23
  minutes on 2 cores). The translator now writes one function per
  distinct upper word (1,582) with the upper executor inlined for that
  word, and each pair calls its word's function
  (`VU1NativeAccess::execPairUpper`): 446 M, and a build about as long as
  before.
- `commitReadyPipelines` clears only the valid flag of a committed entry
  (every insert resets the entry or sets every field that is read); the
  interpreter itself drops from 793 M to 748 M.

First level, Linux: game thread 158 ms of CPU per game frame (169 ms
after patch 0019); same picture, no `missing-target`.

Windows, after patch 0019 (one short session of the maintainer, 6 GS
bands): 7.4-9.0 game frames per second in the first level, against
5.4-7.2 per second in one-minute averages before translation (findings/20
s.18); not a difference the maintainer noticed while playing.

Patch 0021 (phase 2c) does the same for the lower half: the lower
executor moves to an inline header (`execLowerInline`) and the translator
writes one function per distinct lower word (1,371), called by the pairs
through `VU1NativeAccess::execPairUpperLower`. Pairs with the I bit hold an
immediate in the lower word and keep no lower function.
First level, Linux: game thread 145 ms of CPU per game frame (158 ms
after patch 0020, 197-221 ms before translation); same picture, no
`missing-target`.

Patch 0022 (phase 2d): translated pairs write their VF and ACC results at
issue instead of queueing them. This is exact because the hazard check
still makes every read of a VF or ACC lane wait until that lane's latest
write is due, so no instruction can see when the value landed. The write
takes a new write sequence (an older queued write to the same lane is
dropped at commit, as for a newer queued write), and the runtime keeps
"pipelines pending" true until the write would have committed, so the
end-of-program flush, and with it the cycle count, are unchanged. VI, Q,
P, flag and store results keep their queues: their delays are visible to
the program (a VI read in the next pair sees the old value; Q and P are
read without a stall). Same checksums and cycle count (110,862) as the
interpreter on the 40 captured workloads; ps2xTest 454/454. The
interpreter path is unchanged (717 M after this patch, 748 M before).
First level, Linux: game thread 126 ms of CPU per game frame (145 ms
after patch 0021, 197-221 ms before translation); same picture, no
`missing-target`.

What is left per pair: the remaining queues (VI, flags, Q/P, stores) and
their commit (`commitReadyPipelines` is still about 15% of the translated
VU time), the hazard check and the FMAC flag model. The next steps below
target them.

## 4. Where the time still goes, and phase 2

The translated pair still does what the interpreter does per pair:
`execUpper`/`execLower` (a call each, with the instruction as an argument,
so their big switches are not folded), the hazard and stall check
(`calculatePairReadyCycle`), the delayed writes through the pipeline
queues (`queueVfWrite`, `commitReadyPipelines`), `markPairWrites`, and the
FMAC flag model. Per findings/20 s.14 these are about 40% (pipeline
bookkeeping) and 25% (flags) of the VU time. Next, in order:

1. (Partly done in s.3 for the upper half.) The lower half inline or per
   operation.
2. Static timing per block: the pipelines are empty at every resume
   (findings/20 s.2); from a block's entry the stalls and the moments the
   results land are known at build time, so the queues can be replaced by
   writes at the right pair where no early read observes them.
3. Flags only where read (MAC flags reach 24 reads in group F; status
   never read there, findings/20 s.14).

Each step must keep `--sum` identical to the interpreter.

## 5. Open

- The board in the README still lists the VU programs as interpreted:
  phase 1 runs the interpreter's own step code; they count as native once
  phases 1-2 above replace it.
- VU0 programs (VCALLMS) are not translated yet (the resolver is VU1
  only).

## 6. Windows build: MSVC ran out of memory (patch 0023)

- Symptom: the first Windows build after patches 0020-0022 (commit
  `273f3c3`, MSVC 14.44, Release) failed on the translated files with
  `C1002: compiler is out of heap space in pass 2`, and the machine froze
  while it ran. The last Windows build that worked was at patch 0019.
- Measured: one translated file (`vu1_native_10.cpp`: 210 pairs, 50 upper
  and 43 lower functions) compiled alone with the build's own command:
  `cl.exe` passed 4.2 GB of committed memory in the first 80 s and 31 GB
  at 145 s, when it was stopped. GCC 13 compiles the same file with
  0.7 GB at most (24 s). Ninja starts one compile per hardware thread.
- Cause: patch 0020 forces the FMAC result and flag model
  (`normalizeFmacResult`, `calculateFmacExactResult`, `updateFmacFlags`,
  `applyFmacDest`, ...) inline. Each generated upper function is
  `execUpper` with a constant word; GCC drops the opcode cases the word
  cannot reach before inlining, MSVC inlines first, so each of the ~70
  cases got the whole model, in every function, before being dropped.
- Fix (patch 0023): `PS2_VU_FMAC_INLINE` is the forced inline on GCC and
  Clang (no change there) and plain `inline` on MSVC, which then applies
  its own limits. The model is still folded on Linux; on Windows it is
  inlined where MSVC chooses, so the Windows gain of phase 2b can be
  smaller than on Linux. Measured with the fix, same file and command:
  0.26 GB at most, 9 s. The whole Windows build (commit `8a53de7`, at
  most 5 translated files at once) took 11.2 minutes instead of 24, and
  the game ran with the translated code on (6,727 pairs) to the first
  level and exited normally, with no `missing-target`.
- Windows speed in that session (6 GS bands, one-second samples): about
  48 game frames per second in the menus, 5-10 in the first level (mostly
  7-9), against 7.4-9 with patch 0019. So on Windows the first level did
  not get faster, although the game thread's CPU per frame fell by a
  quarter on Linux since then: the game thread is probably no longer what
  limits the level there. Next: per-thread CPU on Windows (s.7).
- Guard in `CMakeLists.txt`: the translated files compile in a Ninja job
  pool sized by the machine's memory (half the RAM divided by 3 GB per
  file with MSVC, 1 GB with GCC/Clang; `BEN10_VU1_COMPILE_JOBS`
  overrides), so a mistake of this kind cannot start one heavy compile per
  hardware thread.
- The proper fix for every compiler is to pick the opcode case at
  translation time (one function per operation), which phase 2 of D14
  already plans.

## 7. Windows: what limits the first level

- Tool: `tools/perf/cpu_per_frame_windows.ps1`, run beside the game, logs
  every 10 s the CPU time of each busy thread per second and per game
  frame (the Windows counterpart of `tools/perf/cpu_per_frame.sh`).
- Measured on the maintainer's Windows desktop (MSVC Release, commit
  `8a53de7`, 6 GS bands), first level, 10-second samples:

| | Game thread | Each GS band | Window thread | Game frames/s |
|---|---|---|---|---|
| translated VU1 code | 1000 ms/s (one core), 100-111 ms per frame | 33-53 ms per frame (35-45% of a core) | 11-15 ms per frame | 9-10 |
| `BEN10_VU1_NATIVE=0` (interpreter) | 1000 ms/s, 98-129 ms per frame | 32-53 ms per frame | 12-15 ms per frame | 7.7-10 |

  The menus are the other way round: GS bands near a full core, game
  thread 6-24 ms per frame, 38-46 frames per second.
- So the game thread limits the first level on Windows, and the translated
  code saves only about a tenth of its time there (about 105 against 116
  ms per frame on average), against a quarter or more on Linux with GCC
  (s.3). Either MSVC makes much less of the translated code (patch 0023
  takes part of the inlining away from it), or on Windows most of the game
  thread's time is outside the VU1 code. Frame rates come in steps (9 or
  10 per second): a game frame takes a whole number of vblanks.
- To tell which: `src/diagnostics/sample_profiler.cpp`, a sampling
  profiler for the Windows build (`BEN10_PROFILE`): it samples the game
  thread's call stack about every millisecond into a text file, and the
  build writes a linker map (`ben10_poe.map`, `/MAP`) that
  `tools/perf/win_profile.py` uses to name the functions. Compiled for
  Windows with MinGW here; first run on Windows in s.8.

## 8. Windows profile of the game thread; the XGKICK buffer reset (patch 0024)

- Profile: `BEN10_PROFILE` on the maintainer's Windows desktop (commit
  `e82338d`, MSVC Release, 6 GS bands), first level, 90 s, about 50,000
  stack samples (one every 1.8 ms; the game ran normally with the
  profiler on). `tools/perf/win_profile.py`, windows 2-10:

| Inclusive | Function |
|---|---|
| 78.5% | `VU1Interpreter::run` (VU1 programs and VU0 microprograms) |
| 67.0% | `PS2Memory::processVIF1Data` (VU1 programs started by VIF1) |
| 25.2% | `PS2Runtime::executeVU0Microprogram` (VU0 microprograms: not translated) |
| 11.6% | `VU1Interpreter::resetScheduler` |

| Self | Function |
|---|---|
| 12.6% | `VCRUNTIME140.dll` (`memset`/`memcpy`: 10.8% called by `resetScheduler`) |
| 8.0% | `VU1Interpreter::updateFmacFlags` |
| 7.1% | `VU1Interpreter::calculateFmacExactResult` |
| 4.6% | `VU1Interpreter::commitReadyPipelines` |
| 4.4% | `VU1Interpreter::queueViWrite` |
| 4.3% | `VU1Interpreter::normalizeFmacResult` |
| 3.5% | `VU1Interpreter::calculateFmacProductSticky` |
| 9.4% | all translated pair, upper and lower functions together |

  Recompiled game code is 1.5% and the IOP 2.8%: on Windows the game
  thread is VU time.
- Three causes, by size:
  1. The FMAC model runs out of line under MSVC (about 23% self): patch
     0023 lets MSVC decide, and it does not inline it.
  2. VU0 microprograms (a quarter of the thread) still run in the
     interpreter: the translation covers VU1 only.
  3. `resetScheduler`, run at every VU program start (VU1 and every VU0
     microprogram call), assigned `m_xgkick = {}`, which zeroes the
     XGKICK state's 64 KB packet buffer; `startXgkick` did the same at
     every XGKICK.
- Fix for 3 (patch 0024): `XgkickPipeline::resetControl()` resets every
  field but the buffer. The buffer is only read below `copiedBytes`, and
  those bytes are always copied in from VU memory first, so the results
  are the same: same checksums and cycle count on the 40 workloads with
  and without `--native`, ps2xTest 454/454. Replay (`callgrind`, the
  replay does not go through `execute`): translated run loop 349 M -> 336
  M instructions. First level, Linux: game thread 126 -> 119 ms of CPU per
  game frame (glibc's `memset` is faster than MSVC's, so the Windows gain
  should be larger); same picture, no `missing-target`.
- Next, from this profile: the FMAC model inline on MSVC without the
  memory blow-up of s.6 (pick the opcode case at translation time), and
  the VU0 microprograms translated like VU1's.

## 9. Executors as templates on the instruction word (patch 0025)

- Each translated upper or lower function inlined the whole executor with
  a constant word and left it to the compiler to drop the other opcode
  cases. GCC does that before inlining; MSVC does not (s.6), so on Windows
  the FMAC model had to stay out of line (s.8).
- Patch 0025: `execUpperT<kConst, kInstr>` and `execLowerT<kConst, kInstr>`
  hold the executors' bodies with every opcode case group under
  `if constexpr`. The interpreter instantiates `<false, 0>` (every case);
  translated code calls `VU1NativeAccess::execUpperWord<W>` and
  `VU1NativeLowerAccess::execLowerWord<W>`, which instantiate only W's case,
  with constant fields and dest mask, on any compiler. The FMAC model is
  forced inline again everywhere. The case groups were wrapped by a script
  that also checked that every non-empty group ends in `return` or `break`
  (empty groups keep falling through: the reserved upper ops 0x30-0x33
  still reach `default`).
- Same checksums and cycle count on the 40 workloads with and without
  `--native`; ps2xTest 454/454. GCC: same instruction count (it already
  pruned), one generated file compiles with 529 MB instead of 711 MB.
  MSVC: not built yet. The Windows build script now stops any compiler
  process above 6 GB, so a repeat of s.6 fails the build instead of
  freezing the machine.
