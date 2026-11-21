# 20: VU1 census, an offline VU1 replay, a faster VU interpreter, the GS on its own thread

Continues [19](19-windows-build-and-speed.md) s.8-10: in the first level the
VU1 interpreter was the largest cost, then the software GS renderer.

## 1. Which VU programs run in the first level

How: gdb breakpoints on `VU1Interpreter::execute` (VIF `MSCAL` and VU0
`VCALLMS` both end there) and `VU1Interpreter::resume` (VIF `MSCNT`),
printing the unit (`m_unit`), the start PC and the interpreter's cycle
counter, for about 20 seconds in the level. The cycles of a run are the
difference to the next start on the same unit.

- **VU0** (unit 0): about 17,000 calls, almost all at `0x38` and `0x140`,
  plus `0x0`: the skinning microprograms of [18](18-memory-card-and-menus.md)
  s.12, called by `VCALLMS`. The VU0 instance's cycle counter does not move
  between calls in this build, so their cost is not in the table below; the
  stack samples put about 7 of 60 in them.
- **VU1** (unit 1): the work is in **resumed** programs (`MSCNT`). The
  `MSCAL` starts (`0x100`, `0x190`, `0x150`...) run 13 to 45 cycles each:
  set-up code that stops with the E bit; `MSCNT` then continues the
  rendering loops.

| VU1 resume PC | Runs | Share of VU1 cycles | Cycles per run |
|---|---|---|---|
| `0x2DF8` | 432 | 28.8% | 2,815 |
| `0x3BD0` | 261 | 16.3% | 2,639 |
| `0x0BF8` | 285 | 15.2% | 2,258 |
| `0x1038` | 215 | 10.9% | 2,141 |
| `0x3520` | 288 | 7.7% | 1,129 |
| `0x37B0` | 245 | 7.2% | 1,248 |
| 6 others | 305 | 13.9% | 1,264-2,857 |

- **Which code:** a dump of VU1 micro memory in the level matches the
  `.vutext` overlay entries **#13 to #20** ([11](11-vu-microprograms.md),
  group F), each 0x800 bytes at VMA 0x0, 0x800 ... 0x3800 (256 of 257 pairs
  match per entry; the extra pair is the VIF code around the upload). So
  the level's 3D drawing is group F, and the hot entry points above are
  inside it.
- **Uploads:** the decoded-instruction cache was rebuilt 217 times for VU1
  and 63 times for VU0 in 15 seconds (`rebuildDecodedCodeCache`): the game
  uploads microcode (VIF `MPG`) several times per frame.

## 2. An offline replay of VU1 work

To work on VU1 without the game, `tools/vu1_replay/capture_vu1.gdb` saves,
at a VU1 `MSCNT`, the code memory, the data memory and the interpreter's
`VU1State`; `ben10_vu1_replay` (built from `tools/vu1_replay/`) runs them
again through PS2Recomp's interpreter. With `--sum` it prints a checksum per
workload of every result (registers, flags, cycle count, VU data memory, GS
memory). Captures stay in `user/` (they are the game's code and data).

At every one of the 40 resumes captured, the interpreter's pipelines were
empty (`pipelinesPending() == 0`), so code, data and `VU1State` are the
whole starting state.

## 3. A faster VU interpreter (patch 0013)

`callgrind` on the replay (instruction counts do not depend on the
machine's load) showed the time in the bookkeeping more than in the
arithmetic: the pipeline commit pass on every cycle, the hazard check, the
copy of the decoded pair, `markPairWrites`, an out-of-line operand
normaliser. Patch 0013 changes those without changing what is computed:

| Step | Instructions for the 40 workloads |
|---|---|
| before the patch | 296.5 M |
| skip the commit pass until something is due; fast XGKICK drain (findings/19 s.9) | about the same (the scan came back as a recomputation) |
| earliest-ready found in the commit pass itself; decoded pair by reference | 256.7 M |
| hazard check and `markPairWrites` visit only the set VI bits | 234.0 M |
| commit pass visits only valid slots (one bit mask per pipeline array) | 221.1 M |
| `normalizeOperand` inline | 200.1 M |
| `run()` and the hazard check use bit scans for VI and lanes | 185.1 M |

About 36 M of the remaining instructions are the replay's own start-up and
decoding (the replay has no `PS2Memory`, so it does not use the decoded
cache); the VU work itself went from about 260 M to about 150 M. Every
step was checked: the 40 checksums are identical to those of the
interpreter before the patch.

The patch also decodes lazily after a code upload: the cache forgets its
pairs and decodes each one on first use, instead of decoding all 2,048 at
every upload (s.1: several uploads per frame). A pair's decoding reads
only its own 8 bytes, so this cannot change a result.

All 451 ps2xTest cases pass.

In the game (Linux, first level, the same route, about 85 s measured):

| Build | Game frames per second | VBlanks per second |
|---|---|---|
| before patch 0013 | 1.42 | 2.85 |
| patch 0013, first part (findings/19 s.9) | 2.03 | 4.05 |
| patch 0013, complete | 2.58 | 5.17 |

## 4. The GS rasteriser on its own thread

PS2Recomp's GS frontend draws through a replaceable backend
(`GS::setRasterBackend`). `src/gs/gs_async_backend.cpp` installs one that
wraps the CPU rasteriser (`GSCpuBackend`) and runs it on a thread of its
own: primitives, CLUT loads, image uploads and TEXFLUSH are queued (with
their data copied) and run in order by one worker; every call that reads
GS memory for the game (local-to-host transfers, `ReadVram`, snapshots) or
returns a result waits for the queue first. The GS memory after the same
commands is the same bytes. Details in the file's header.

Two problems found on the way, both fixed:

- **Unbounded queue.** The first version let the game run ahead without a
  limit; whenever drawing was the slower side the queue grew (the
  process's memory grew by about 100 MB every 5 seconds). Now the game may
  run at most 8,192 commands ahead (`kMaxPending`) and waits otherwise.
- **Lost key presses.** The window thread called `Sync(Presentation)` and
  `Present`, which first waited for the queue; sampled with gdb, it spent
  nearly all its time there, its loop got slower than a key press, and
  presses were missed (the menus did not react to a 1.2 s press, and
  barely to a 2.5 s one). Presentation does not need to wait: the CPU
  rasteriser copies the frame under its own lock, between two commands,
  as it did when the game thread drew. The picture shown can be a little
  behind the game; the game's own view of GS memory is not.

`BEN10_GS_SYNC=1` keeps the original synchronous backend, for comparisons.

Measurements: s.5.

## 5. Measurements

Linux, game code at `-O0` (runtime and IOP at `-O2`), under Xvfb; the same
route each time (start, New Game, Normal, first slot, the loading tip, the
first level); `BEN10_FPS_LOG` averages. Boot = the warning, intro and
title screens, 20 to 150 s after start; level = about 80 s after arriving.

| Build | Boot, game frames/s | Level, game frames/s | Level, VBlanks/s |
|---|---|---|---|
| before patch 0013 (findings/19 s.9) | 6-7 | 1.42 | 2.85 |
| patch 0013, GS synchronous (`BEN10_GS_SYNC=1`) | 6.81 | 2.76 | 5.53 |
| patch 0013, GS on its own thread | 8.60 | 4.13 | 8.27 |

In the level the game draws one frame every two VBlanks (it runs at 30
frames per second on the console), so 4.13 frames per second is about 14%
of full speed. The picture in the level is unchanged (screenshot compared
by eye with findings/18's).

The scripted route needed longer key holds (2.5 s instead of 1.2 s) while
the window thread still waited for the queue (s.4); with that fixed, the
route works with both backends.

## 6. Where the time goes with the GS thread

40 gdb samples of all threads in the first level (GS thread on):

| Thread | Busy samples | In |
|---|---|---|
| game thread | 36 of 40 | VU1 interpreter 28 (VIF1 kicks 18), VU0 skinning 7, IOP 6, the game's own code 16; waiting for room in the GS queue 4 |
| GS thread | 39 of 40 | `GSCpuBackend` (drawing) |

Both threads are nearly full; the GS thread slightly more. On this
machine (2 cores) there is no third core to give drawing; on a desktop
with more cores there is.

## 7. Row bands: drawing one picture on several threads

- Patch 0014 lets a `GSCpuBackend` draw only some rows of the picture
  (`SetRowBand`: rows y with `((y >> 4) % N) == k`). Several backends on
  the same GS memory that see the same commands in the same order write
  the same bytes as one (each pixel is drawn by exactly one band). A
  ps2xTest case draws blended, Z-tested triangles and a textured sprite
  with two bands, one after the other (the worst order), and gets the
  same frame and Z buffer as one backend.
- `src/gs/gs_async_backend.cpp` uses it with `BEN10_GS_THREADS=N`: the GS
  thread becomes a dispatcher that hands runs of draws, CLUT loads and
  TEXFLUSHes ("segments") to N bands (band 0 on the dispatcher itself)
  and waits for all of them before a transfer. A band must never read GS
  memory another band is still writing: the dispatcher tracks the 8 KB
  pages written in the current segment (frame and Z rows inside the
  scissor, over-estimated) and ends the segment before a draw whose
  texture pages, or a CLUT load whose CLUT page, are among them.
- The one difference left with N > 1: each band has its own copy of the
  GS texture page buffer, so stale texture data seen without a TEXFLUSH
  can differ. Games must TEXFLUSH after rendering to a texture.
- Default N = 1 (one GS thread, as in s.4): this machine has 2 cores, so
  the bands could not be measured for speed. Checked here: with N = 2 the
  game runs the same route into the first level, the picture looks the
  same, and the level runs at the same rate as with one band (about 4
  frames per second: no spare core).

## 8. Open

- Measure `BEN10_GS_THREADS` = 2, 4, 6 on a desktop with more cores; make
  the best one the default.
- Native VU1 (decision D14): translate group F and the VU0 skinning
  programs; `ben10_vu1_replay` is the reference.
- The VU0 instance's cycle counter does not advance across `VCALLMS`
  calls (s.1): check why, and whether anything reads it.
