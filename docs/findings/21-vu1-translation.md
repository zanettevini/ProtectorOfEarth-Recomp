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

So -32% against the replay's interpreter, about -25% against an
interpreter that keeps its decoded pairs (as in the game). Same checksums
with and without `--native`; ps2xTest 454/454. Building `ben10_vu1_native`
now takes about 3.5 minutes on the 2-core Linux machine. On Windows (MSVC
Release) the whole build went from 8 to 24 minutes, the translated files
compiling slowly; the translator now writes 32 smaller files instead of 8
so they compile on all cores at once.

In the first level (Linux, 90 s, `tools/perf/cpu_per_frame.sh`): game
thread 169 ms of CPU per game frame, against 193 ms with phase 1 and
197-221 ms before translation; same picture, no `missing-target`.

What is left per pair: `commitReadyPipelines` and the queues (`queueVfWrite`
and friends), the FMAC result and flag model (`calculateFmacExactResult`,
`updateFmacFlags`...), `execLower` (out of line). The next steps below
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
- Not tested on Windows yet (MSVC compile time and size of the 8 generated
  files).
