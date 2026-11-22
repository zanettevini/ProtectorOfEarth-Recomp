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
- VU0 programs (VCALLMS): translated since s.19 (patch 0032).

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

## 10. An exact fast path for the FMAC flags (patch 0026)

- What the FMAC model does per lane: the float result is computed as the
  interpreter always did, then the same operation is recomputed exactly
  (`long double`: 80-bit x87 with GCC on x86-64, the same as `double` with
  MSVC) only to classify it; the float result is replaced only when the
  exact value is zero, above `FLT_MAX` or below `FLT_MIN`. Product-sum
  instructions also classify the exact product for the sticky flags.
- Patch 0026: when the float result's exponent is 3..252 the lane's flags
  are its sign and the value stays, without the exact computation. Why
  that is exact: for one operation the exact value is within one ulp of
  the float result; for ACC +/- product the rounded product is also off by
  at most one ulp of itself, and requiring it not to have overflowed and to
  be at most 2^20 times the result keeps the exact value within a quarter
  of the result, so nonzero, of the same sign and in range. The argument
  holds for any rounding mode and with fused multiply-adds. Products for
  the sticky flags: a zero operand gives an exact signed zero; otherwise
  exponent 2..253 gives the sign. Everything else goes through the exact
  model as before.
- Checked: same checksums and cycle count on the 40 workloads with and
  without `--native`; ps2xTest 454/454; and a throwaway randomised test
  (1.6 million lanes over every FMAC opcode, operands drawn near zero,
  near the range limits and in near-cancellation of ACC and the product,
  1.1 million of them on the fast path) found no difference against the
  exact model, compiled plainly and with `-march=x86-64-v3
  -ffp-contract=fast` (fused multiply-adds, as `/fp:fast /arch:AVX2` may
  produce on Windows).
- Gain: translated VU code 336 M -> 321 M instructions on the replay
  (-4.5%). The x87 work was not the bulk of the model's cost: the lane
  loops, the flag packing and the flag pipeline entry per instruction are.
  Building the translated library with `-O3` instead of `-O2` (loops
  unrolled) gave about 8% more on the replay for a 40% longer build: not
  kept.
- Found on the way: on MSVC Release, PS2Recomp's runtime (the VU
  interpreter with it) is compiled with `/fp:fast /arch:AVX2`, the
  translated code with the default `/fp:precise` (KNOWN_ISSUES).
- Where this leaves the plan: about 550 instructions per translated pair
  remain (step bookkeeping, flag pipeline, per-cycle commit). Small
  changes inside this model give a few percent each; the next gains need a
  different execution model for translated code (s.11).

## 11. Plan: from about 550 to about 150 instructions per pair

Where a translated pair's cost goes (replay, GCC, after patch 0026; per VU
cycle): the pair step itself about 215 instructions (hazard check, write
bookkeeping, VI queue, branch and end bits, budget), the FMAC model and
flag packing about 160, the per-cycle commit and PATH1 progress about 120.
The same model, implemented for translated code without its general-purpose
machinery, with every step checked by the replay checksums (registers,
flags, VU memory, GS output and cycle count identical):

1. Flags as an ordered history instead of a slot pipeline committed every
   cycle: each FMAC instruction appends {ready cycle, MAC, status, sticky};
   MAC/status/clip are materialised only when read (FMAND, FMEQ, FMOR,
   FSAND..., FCAND..., the end of the program, a resume) or when the
   history is full. In group F flags are read at 24 places (findings/20
   s.14).
2. No per-cycle commit when nothing is due: the next due cycle of the
   remaining queues (Q, P, stores, VI) is kept, and a pair advances the
   cycle by one with a single compare when nothing is due.
3. PATH1 (XGKICK) progressed in batches: up to the next store commit or
   the end of the transfer, instead of 16 bytes every other cycle, since
   only a store into the packet's range can change what it copies.
4. The pair's register reads, writes, latencies and branch/end bits
   emitted by the translator as straight-line code (no walk over the
   decoded usage), and no save/restore copies around the immediate VF/ACC
   writes.
5. Then the VU0 microprograms through the same translator (a quarter of
   the game thread on Windows, s.8; they re-enter at the same few
   addresses, findings/20 s.12).

Outside the VU code, for the target of 25-30 frames per second in the
first level on Windows: the 6 GS bands spend 33-53 ms per game frame each
there (s.7), which alone would cap the level near 20 per second; the band
count (6 at most by default) and the rasteriser need work too. Running
VIF1 and VU1 on their own thread (as on the console) is a larger change of
the timing model and is left for a decision of its own.

## 12. Step 1 of s.11 tried: flags applied on demand (not kept)

- Every flag entry (FMAC MAC/status, CLIP, FSSET, FCSET) is due exactly
  `kFmacLatency` cycles after issue, and within a cycle the upper entry
  takes a lower slot than the lower one, so the eight-slot flag pipeline is
  in effect a queue in issue order. Tried: a 16-entry ring in issue order,
  applied only when something reads the flags (the ten flag-reading lower
  instructions, the FDIV status update, the end of `run`, a full ring),
  with the pending checks counting only entries not yet due, so the
  end-of-program cycle count stays the same.
- Result: same checksums and cycle count, but the replay cost went from
  321 M to 324 M instructions. The per-cycle commit still ran on 541,785
  of 554,310 cycles (74 instructions per call on average): VI writes and
  stores are due on almost every cycle, so removing the flags from it saved
  only about 15 M, which the ring bookkeeping gave back. Reverted.
- So the commit is not dominated by any one queue; together with the pair
  step and the FMAC model it is spread over many small costs. The
  remaining steps of s.11 have to replace the per-cycle model for
  translated code as a whole (a pipeline representation built for it,
  synchronised with the interpreter's state where control passes between
  them), not one queue at a time.

## 13. The other route: VIF1 and VU1 off the game thread

- How it works now: DMA transfers are instant. Writing a channel's CHCR
  runs `PS2Memory::processPendingTransfers`, which processes the whole
  VIF1 chain (copied at kick time into `chainData`), runs every VU1
  program it starts (`MSCAL`/`MSCNT` callbacks into `VU1Interpreter`, up to
  65,536 cycles each) and sends PATH1/PATH2 packets to the GIF, then sets
  D_STAT, clears CHCR STR and queues the DMAC interrupt cause, all before
  the EE's next instruction.
- On the console VU1 runs alongside the EE; PCSX2's MTVU runs it on its
  own thread. Here that would take: a worker owning VIF1 and VU1 (and
  their GIF output); the EE waiting for the worker whenever it touches
  VIF1/DMA/GIF/GS registers, VU1 memory, or sends GIF packets itself
  (PATH3 or GS registers), so that the GS sees the same order; and
  completion (D_STAT, CHCR, the interrupt) delivered after a fixed amount
  of EE time, waiting for the worker then if needed, so that the game
  stays deterministic instead of depending on which thread is faster.
- That changes when the game sees DMA completion (now: at once), which
  is a decision of its own. Before deciding: measure how many VIF1 and GIF
  transfers the first level starts per frame and how much EE work lies
  between a VIF1 kick and the next point where the EE would have to wait.
- Measured (a temporary counter in `processPendingTransfers` and in
  `readIORegister`, not committed; first level, Linux, about 4 game frames
  per second): every game frame has the same pattern, five VIF0
  transfers, then three reads of D1_CHCR (waiting for the previous VIF1
  transfer), one VIF1 transfer, three more reads, a second VIF1 transfer.
  No GIF (PATH3) transfers at all in the level. The game thread spent
  about 780 ms of every second inside `processPendingTransfers` (VIF1 and
  the VU1 programs it starts, VIF0, GIF output), so only about a fifth of
  its time is the EE's own work that a VU1 thread could overlap: at best
  about a quarter faster, not twice. Not pursued for now; the VU code
  itself has to get faster (s.11-12).

## 14. Step 1 done: stores and VI writes at issue, flags on demand (patch 0027)

- Why the commit pass still ran almost every cycle with flags removed
  (s.12): stores and VI writes. Every store is due one cycle after issue;
  most VI writes too (only ILW and ILWR write VI 4 cycles later). A result
  due one cycle after issue is committed by the `advanceOneCycle` at the
  end of the same pair's step, and nothing between the instruction and
  that commit reads it (PATH1 reads VU memory after the commit, in the
  same `advanceOneCycle`).
- Patch 0027, for translated code: stores are written at once
  (`storeOrQueue<true>`), latency-1 VI writes are written at once as the
  commit would (16-bit) and become the register's latest write (an older
  ILW still queued to it is dropped, as before); flags use the issue-order
  ring of s.12, applied when read. The interpreter keeps its queues for
  stores and VI.
- Same checksums and cycle count on the 40 workloads with and without
  `--native`; ps2xTest 454/454. Translated code 321 M -> 291 M
  instructions (-9.4%); `advanceOneCycle` with the commit 67 M -> 33 M.

## 15. Per-cycle checks inline, hazard check unrolled (patch 0028)

- After patch 0027 the commit pass rarely had work but was still called
  on every cycle: its prologue cost more than its "nothing due" check
  (13 M of the 32 M were on its first line). `advanceOneCycle` and
  `microAddressMask` were calls per pair too. Now inline, with the two
  "nothing to do" checks (commit not due, no XGKICK) in `advanceOneCycle`:
  291 M -> 270 M.
- The hazard check and the write marking walked the decoded usage lists
  of each pair at run time (two usages, up to two VF reads each, lane
  masks, VI masks, ACC lanes): about 40 M, although every translated pair's
  lists are constants. `execPairWith` now takes a timing policy; the
  translator passes each pair's `constexpr` description as a template
  argument (`ConstTiming<k_pXXXX>`), and `calculatePairReadyCycleConst` /
  `markPairWritesConst` unroll the same checks and stores with
  `if constexpr`. The interpreter keeps the run-time walk.
- Same checksums and cycle count on the 40 workloads with and without
  `--native`; ps2xTest 454/454. Translated code 270 M -> 214 M
  instructions (from 321 M before step 1: -33%). One generated file now
  compiles with GCC in 15 s and 502 MB (21 s and 529 MB before).

## 16. Lane loops unrolled, loop-head commit check (patch 0029)

- At `-O2` GCC keeps the 4-lane loops of the upper executor and the FMAC
  model as loops, so in a translated word (constant `dest`) every lane's
  dest test and all twelve operand normalisations still ran.
  `PS2_VU_UNROLL_LANES` (`#pragma GCC unroll 4`, `#pragma unroll 4` on
  Clang; MSVC unrolls such loops itself) before those loops: 214 M -> 199 M.
- `loopHead` called the commit pass before every pair; now with the same
  inline check as `advanceOneCycle`: 199 M -> 184 M.
- Same checksums and cycle count on the 40 workloads with and without
  `--native`; ps2xTest 454/454.
- Step 1 so far (patches 0027-0029): translated code 321 M -> 184 M
  instructions on the replay (-43%). First level, Linux: game thread
  119 -> 99 ms of CPU per game frame (126 ms before patch 0024); same
  picture, no `missing-target`. The frame rate there stays at the single
  GS band's limit (267 ms of CPU per game frame on that 2-core machine).

## 17. FMAC operands read once, quicker normalisation (patch 0030)

- A hot MADDbc upper function still cost about 600 instructions per call
  with every lane on the fast path (no exact computations ran in the
  replay at all). Most of it was operand handling: each normalisation
  about 10 instructions, and for product-sum instructions the fast-path
  check and the sticky pass each re-read and re-normalised both product
  operands per lane (the compiler cannot share them across the stores in
  between). `applyFmacDest` now reads them once and passes them on:
  184 M -> 178 M.
- `normalizeOperand` first tests `((bits >> 23) + 1) & 0xFE`, nonzero
  exactly for exponents 1..254 (checked for all 512 sign/exponent
  combinations), and returns the value unchanged then: 178 M -> 169 M.
- Same checksums and cycle count on the 40 workloads with and without
  `--native`; ps2xTest 454/454; the randomised FMAC comparison of s.10
  still finds no difference.

## 18. Write sequences only when needed (patch 0031)

- Immediate VF/ACC/VI writes still took a new write sequence and stored it
  per lane, which only matters if an older write to the same lane is still
  queued (possible only after an interpreted pair in the same run, or an
  ILW for VI). Now only when the matching queue is not empty; with it
  empty, any write queued later is newer and its own sequence is the
  latest anyway. 169 M -> 159 M.
- Same checksums and cycle count on the 40 workloads with and without
  `--native`; ps2xTest 454/454.
- Where a translated VU cycle's ~290 instructions go now (replay, GCC):
  upper function (FMAC arithmetic, operand normalisation, flag
  classification and packing, flag entry) about 85, pair step (hazard
  check, writes, branch and end bits, cycle advance) about 75, run loop
  (budget and stop checks, dispatch through the native table) about 27,
  lower function about 19, commit pass about 17.

## 19. The VU0 microprograms run translated too (patch 0032)

- What: the game's VU0 microprograms, started from EE code with VCALLMS
  (character skinning, findings/18), ran in the interpreter: the resolver
  hook of patch 0018 was VU1 only. The Windows profile (s.8) put them at a
  quarter of the game thread, and the maintainer saw the level slow down
  when enemies come on screen: more characters, more skinning calls.
- The programs are already in the translator's input: they are overlay
  group B of `.DVP.ovlytab` (findings/20 s.12), copied into VU0 micro
  memory. A translated pair is only right on VU0 if it decodes the same
  there: on VU0 the EFU instructions, XGKICK and lower opcode 0x64 are
  reserved. `tools/vu1_translate` now decodes every pair below 0x1000 (VU0
  micro memory is 4 KB) a second time as VU0 code and marks it VU0-valid
  only if the decoded usage and the I/E/M/D/T bits are the same: 2766 of
  the 6727 pairs. Everything else that differs between the units (micro
  address mask, data memory size, the VU0-only paths) is read at run time
  from the interpreter instance, so one function serves both.
- Patch 0032: `VU1Interpreter::setNativeResolverVu0`, a separate resolver
  for the VU0 instance (none by default); `resolveNativeCode` uses the
  resolver, code memory and upload generation of the instance's unit.
  Port side: `ben10::vu1_native::resolveVu0` hands out only VU0-valid
  pairs; `src/vu/vu1_native_install.cpp` installs it, and
  `BEN10_VU0_NATIVE=0` turns off only the VU0 part (`BEN10_VU1_NATIVE=0`
  turns off both).
- Check: `tools/vu1_replay/capture_vu0.gdb` saves VU0 calls from the
  running game (code, data, state, start PC and ITOP, one call in 23) and
  `ben10_vu1_replay --vu0` replays them the way
  `PS2Runtime::executeVU0Microprogram` runs them. 60 workloads from the
  first level: the same 60 checksums interpreted and with `--native`.
  The VU1 checksums (40 workloads, with and without `--native`) are
  unchanged; ps2xTest 454/454.
- What the captures show: the VU0 code memory was the same in all 60
  calls; the calls start at 0x0, 0x38 and 0x140, and every pair from
  those entry points to the end bit is translated. The programs are short:
  2040 VU cycles in 60 calls, about 34 per call. So the cost per call
  outside the instructions matters as much as the instructions: in the
  replay (callgrind) the reset, `resetScheduler` and the run setup are a
  few thousand instructions per call against about 10,000 for the
  program. (The replay re-resolves the code at every call because it runs
  without PS2Memory; in the game the resolution is kept until the code
  memory is written again.)
- Result in the game, first level, same spot, Linux, `cpu_per_frame.sh`
  90 s: game thread 86.1 and 87.4 ms of CPU per game frame with
  `BEN10_VU0_NATIVE=0`, 75.1, 76.5 and 74.9 ms with it on: about 13% less.
  The level renders the same (screenshot compared by eye). Untested on
  Windows.
- Open: the per-call overhead of a VU0 call (reset of the state and the
  scheduler, copies of the EE's VU0 registers in and out, the rounding
  mode switch) is now a visible share of each call.

## 20. FMAC flags four lanes at a time (patch 0033)

- Measured first: with `perf` now available on the Linux test machine
  (`linux-tools`), a 60 s profile of the game thread in the first level
  (patch 0032 build): translated VU code 48%, the VU interpreter's own
  functions (run loop, commit, PATH1, VI queue...) 9%, recompiled game
  code and its memory helpers about 15%, GS front end 6%, the IOP
  kernel's thread list walk (`IopKernel::beginNextReady` and
  `nextWakeCycle` iterating a `std::map` at every scheduler checkpoint)
  about 5%, the rest spread out.
- In the replay (callgrind, `--native`), one four-lane MADD upper
  function cost about 370 host instructions per call: the operands
  normalised lane by lane, then per lane the result classification, the
  product operands and product classification, the flag packing, and a
  flag entry.
- Patch 0033: `fmacFlagsFast` checks the per-lane fast paths of s.10 for
  all four lanes at once with SSE2 (exponent ranges by integer compares,
  zero operands and signs by `movemask`) and, when every written lane
  qualifies, appends the same flag entry directly; otherwise the per-lane
  code runs as before. `normalizeOperands4` normalises four operand lanes
  at once. Non-x86 builds keep the per-lane code.
- Checked: same checksums and cycle counts on the 40 VU1 and the 60 VU0
  workloads, interpreted and translated; ps2xTest 454/454; a throwaway
  randomised comparison of the new path against the per-lane model (2
  million FMAC operations over every opcode and dest mask, operands near
  zero, near the range limits and in near-cancellation; 1.04 million
  took the fast path; plus 8 million operand lanes) found no difference,
  plain and with `-march=x86-64-v3 -ffp-contract=fast`.
- Gain: replay, translated run 33.2 M -> 32.0 M instructions (-4%). The
  plain MADD/ADD forms got about a third cheaper; OPMSUB (cross product)
  got dearer, because its lanes often fail the product test (the result
  is much smaller than the products) and then pay for both checks. In the
  game the change is below the noise of the 2-core test machine (game
  thread 75-79 ms per frame either way).

## 21. VIF1 kicks and the EE work between them (measured)

- The question left open in s.13: how much EE work lies between a VIF1
  kick and the point where the EE next needs its result. Measured with a
  temporary probe (thread CPU time around each `processPendingTransfers`
  of a channel kick, and from the end of a VIF1 kick to the next read of
  D1_CHCR; not committed), first level, Linux, patches 0001-0033:
  every game frame has two VIF1 kicks of 23-26 ms of game-thread CPU
  each (VIF1 unpacking, the VU1 programs, PATH1 to the GS), and after
  each the EE runs 16-18 ms of its own before reading D1_CHCR again (the
  three reads before the next kick); VIF0 transfers cost 0.003 ms.
- So now that the VU code is faster, about two fifths of the game
  thread's frame (2 x 17 of 76 ms) is EE work that could run while VU1
  works, against a fifth in s.13. A VIF1/VU1 thread could bring the game
  thread's critical path from about 76 to about 45 ms per frame on this
  machine.
- But completion cannot stay invisible to the EE: the game registers an
  enabled DMAC handler for channel 1 (guest 0x12E670, read with gdb from
  `EeScheduler::m_dmacHandlers`; the same function
  `ben10::frame_rate_log::countedDmaEnd` wraps). The runtime dispatches
  it right after the CHCR store, since the transfer is already done, and
  the handler is what kicks the next VIF1 transfer. Running VU1 in
  parallel means delivering that interrupt (and D_STAT, CHCR STR) later
  in EE time, as the console does, at a deterministic point (the first
  EE access that could observe the transfer, or a fixed number of EE
  cycles after the kick, whichever comes first, the EE waiting for the
  worker there). That is a change of the timing model, left to a
  decision (NEXT_TASK).

## 22. VIF1 and VU1 on a worker thread, opt-in (patch 0034, `BEN10_ASYNC_VU1=1`)

- Decision (maintainer): implement the s.21 route as an option, off by
  default; whether it becomes the default is decided after testing.
- Where the kicks come from (gdb, stacks at the D1_CHCR reads and at the
  VIF1 kick): both VIF1 kicks of a frame are made by the game's VBlank
  handler FUN_0012E5F8 (via FUN_0014EB10 and FUN_001C0DD0, which first
  reads D1_CHCR to check that the previous transfer ended). Between a kick
  and that next read the EE runs the frame's game work. A temporary probe
  of every EE access outside RAM, scratchpad and BIOS in that interval
  (300 kicks; not committed) found only: EE timers, VIF0 kicks and its
  channel registers, scratchpad DMA kicks (toSPR) with their D_STAT
  acknowledgements and D_PCR, VU0 memory, and once per frame the GS
  display registers (PMODE, SMODE2, DISPFB/DISPLAY 1 and 2). None of
  these touch what a VIF1 transfer works on.
- Patch 0034 (runtime, off unless a port calls
  `PS2Memory::setAsyncVif1`): a VIF1 kick hands the transfer (chains are
  copied at kick time already; direct sources are copied then) to a
  worker thread, which runs VIF1, the VU1 programs and their PATH1/PATH2
  output to the GIF arbiter and the GS front end (which takes its own
  lock; the window thread reads it too). The transfer completes for the
  EE (D_STAT CIS1, CHCR STR, QWC, the DMAC cause; then the game's handler
  FUN_0012E670) in `syncVif1` on the EE thread, after waiting for the
  worker, at fixed points of the EE's execution: an access to DMA
  channels 1-2, GIF/VIF1 registers and FIFOs, a D_STAT read, D_CTRL,
  D_ENABLER/W, VU1 memory, GS CSR/IMR/SIGLBLID; a new VIF1 kick; and in
  the EE scheduler, before dispatching a VBlank start, when it has
  nothing to run, or after an optional number of EE cycles since the kick
  (`EeScheduler::pollAsyncVif1`, which also dispatches the DMAC
  handlers). Results depend only on the EE's own progress. The VU1 run
  callbacks take FBRST as it was at the kick and report the D/T stop to
  VPU_STAT at completion. The runtime stops the worker after joining the
  game thread and before destroying the VU1 instance and the GS.
  `src/vu/async_vu1_install.cpp` turns it on with `BEN10_ASYNC_VU1=1`;
  `BEN10_ASYNC_VU1_CYCLES=N` adds a cycle limit.
- Choosing the completion point. First version: a limit of 1,000,000 EE
  cycles (about 3.4 ms of console time). Every transfer completed at the
  limit and the EE then waited for the worker on most of them: a
  temporary counter (not committed) showed the EE runs 44-49 million
  cycles (average 46.7 million; its idle thread included) between a kick
  and its next D1_CHCR read. Without a limit every transfer completed at
  that read, which is inside the VBlank handler that then kicks the next
  transfer: the DMA end handler of the old transfer would then run after
  the new kick, an order neither the console nor the instant model gives.
  So the default point is the next VBlank start: the DMA end handler is
  queued ahead of the VBlank handlers, as with the instant model, and the
  EE overlaps up to one VBlank of its own work with the worker.
- First level, Linux, option on (default point): the level renders as
  without it (compared by eye); 6,599 of 6,600 transfers completed at the
  VBlank, one at a register access. CPU per game frame
  (`cpu_per_frame.sh`, 90 s): game thread 31.1 ms (75-79 ms with the
  option off), worker 40.0 ms. The frame rate on this 2-core machine does
  not change (3.75 game frames per second): the GS thread uses about 260
  ms of CPU per game frame here, so the worker has no free core, and the
  EE waited for it at about three quarters of the VBlanks (about 150 ms
  each). On a machine with free cores the game thread's 31 ms and the
  worker's 40 ms run side by side, and the worker and the GS bands set the
  frame rate.
- What changes for the game: its VIF1 DMA end handler runs at the next
  VBlank instead of at once. The handler only clears the "frame kicked"
  and "waiting for the DMA" flags (state+0x08, findings/15 s.12), which
  the VBlank handler checks before the next kick.
- Checked: ps2xTest 454/454 and the replay checksums (VU1 and VU0) are
  unchanged (they do not use the option); MinGW compiles the changed
  files.
- Windows (maintainer's desktop, MSVC Release, 6 GS bands, first level,
  about 2 minutes each, `cpu-monitor.ps1` every 10 s; the two runs are
  not the same path through the level, so compare ranges):
  - option off (commit with patches 0001-0033): 9-18 game frames per
    second in the level; the game thread at 100% of a core, 50-126 ms of
    CPU per game frame; the busiest GS band 39-68 ms.
  - option on: 12.7-23.9 game frames per second; game thread 14-32 ms;
    the worker 23-74 ms and near 100% of its core (up to 978 ms per
    second) when enemies are on screen; the busiest GS band 32-54 ms.
    Every transfer completed at the VBlank (one at a register); the EE
    waited for the worker at about half of the VBlanks, 30-39 ms on
    average. The game played normally and closed cleanly (exit code 0).
  - So with the option on the worker (VU1 code) is the limit when
    enemies are on screen, then the busiest GS band; the game thread is
    not any more.

## 23. FMAC common path and pair step trimmed further (patch 0035)

- After s.22 the worker thread runs the VU1 code, and on Windows it is
  the limit when enemies are on screen, so the cost per VU cycle matters
  directly. Measured per instruction with `callgrind --dump-instr=yes`
  on the replay (a small script maps the per-address costs to `objdump`):
  - a four-lane MADD upper word still cost about 280 host instructions:
    about 90 of them re-read the product operands lane by lane through a
    branchy normalisation, although the executor had normalised them
    four lanes at a time already; the rare exact model, inlined into
    every translated upper word, made every such function save six
    registers;
  - the exact model was taken on every call by the cross-product pair
    OPMULA/OPMSUB (6,325 calls on the 40 workloads, 444 host
    instructions each): their lanes multiply zero components, so the
    results are exact signed zeros, outside the fast path's range;
  - the pair step saved and restored the VF/ACC register around every
    write, although with immediate writes the executors had already
    written exactly the dest lanes.
- Patch 0035: `fmacProductOperands4` (operands of the product-sum and
  plain-product words, four lanes at a time); `applyFmacSlow`, the exact
  path, out of line; `fmacFlagsFast` also takes a lane whose result is a
  signed zero when a product operand is zero (the exact product is then a
  signed zero, so the exact result is that same zero: Z plus its sign);
  in-place immediate writes keep only the write sequence and pending
  mark; `recordViWriteForBranch` inline.
- Checked: same checksums and cycle counts on the 40 VU1 and 60 VU0
  workloads, interpreted and translated; ps2xTest 454/454; the randomised
  comparison of s.20 extended to plain products, signed-zero operands
  and accumulators, the per-lane operand reader and `applyFmacDest`/
  `applyFmacDestAcc` end to end (2 million operations, 1.11 million on
  the fast path): no difference, plain and with FMA contraction.
- Gain (replay, translated VU work without the replay's code
  resolution): 26.4 M -> 24.4 M host instructions; with patch 0033,
  28.4 M -> 24.4 M (-14%), about 220 per VU cycle. Now per VU cycle: pair
  step 74, upper word 56, run loop 26, lower word 22, per-cycle commit 17,
  PATH1 11, exact model 4.
- What is left in the pair step (about 75-85 host instructions): the
  hazard check, the budget check, the calls to the upper and lower word
  functions, the VI write, the PC and branch-delay update, the halt
  checks and the cycle advance with its commit and PATH1 checks. These
  are the timing model itself; going much lower needs the block-level
  translation of s.11-12 (whole straight-line runs with the pipeline
  timing worked out at build time).

## 24. Translated blocks (patch 0037)

- Decision (maintainer): go on with block-level translation (s.23, last
  point). First step: blocks with the same timing model, so the results
  stay identical by construction; static scheduling only where it can be
  proved at build time.
- What a block is (`tools/vu1_translate`): a straight-line run of pairs of
  one overlay chunk, from a leader (the chunk's first pair, a static
  branch target of B/BAL/IBxx, the pair after a branch's or an E bit's
  delay slot, the pair after a D/T or reserved pair) up to a branch, an
  E/D/T pair, the next leader, or 64 pairs. 447 blocks cover 5,922 of the
  6,727 translated pairs (about 13 per block). They are generated into 32
  more files (`generated/vu1/vu1_native_blocks_N.cpp`).
- How it runs: the resolver puts a block's function at its first pair only
  if every pair of the block is in code memory as translated (the longest
  one when several match; VU0 gets only blocks whose pairs are all valid
  on VU0). The block function runs its body pairs one after another and
  its last pair through the ordinary per-pair step, which sets the PC and
  handles the branch or the end; with a branch, delay slot or end pending
  at entry it runs only its first pair, through the ordinary step.
- Patch 0037: `execPairWith(..., blockBody)` stops after the writes for a
  body pair (no PC, branch-delay or E/D/T tail); `blockPairUpper` /
  `blockPairUpperLower` run a body pair with only the checks that can
  still matter inside a block (the stop request; the step makes the
  budget check itself; the previous pair's cycle advance has already made
  every commit due by this cycle; the PC is stored only when the block
  leaves early, since only branches read it); `MaskedTiming` checks only
  the read lanes the translator could not settle at build time. A lane is
  settled when its latest write is by an earlier pair of the same block
  at least that write's latency in pairs back: pairs issue at least one
  cycle apart, so the write is due when the read issues and the check
  could not stall. 14,094 of the 22,319 read lanes of block bodies are
  settled; reads whose latest write is earlier in the block but closer,
  or comes from before the block, are still checked at run time.
- Checked: same checksums and cycle counts on the 40 VU1 and 60 VU0
  workloads interpreted, translated with blocks and translated without
  (`ben10_vu1_replay ... --native --no-blocks`); ps2xTest 454/454.
  `BEN10_VU1_BLOCKS=0` turns blocks off in the game.
- Gain (replay, translated VU work without the replay's code resolution):
  24.4 M -> 21.3 M host instructions with blocks, 20.6 M without the
  redundant checks, 20.0 M with the settled lanes left out: about 180 per
  VU cycle (256 at the start of s.20-24, -30%). Per VU cycle now: upper
  words 56, block bodies and last pairs about 58, lower words 22, the
  per-cycle commit 17, the run loop 7, PATH1 6. Compiling a block file
  takes less memory and time than a pair file (GCC: 494 MB and 10 s
  against 590 MB and 15 s).
- What is left in a body pair (about 50 host instructions, spread thin):
  the remaining hazard lanes, the write bookkeeping (write sequence and
  pending mark), the VF0/VI0 reset, the VI write, the ready marks, the
  calls to the upper and lower word functions and the cycle advance with
  its commit and PATH1 checks.
