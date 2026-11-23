# gs_replay

Offline replay of a recorded GS command stream (findings/20 s.21).

| File | What it does |
|---|---|
| `gs_synth.cpp` | the `ben10_gs_synth` target: writes a recording with random GS memory and random draws, CLUT loads, uploads and TEXFLUSH over every state the backends handle (`--seed N`, `--records N`; `--any-texture 1` also lets draws sample their own buffers, for GSCpuBackend-only comparisons); no game data (findings/20 s.24) |
| `gs_replay.cpp` | the `ben10_gs_replay` target: loads a recording, makes every recorded backend call again on PS2Recomp's `GSCpuBackend` (`--sync`) or the port's async backend with N row bands (`--bands N`), and prints a hash of GS memory every N records (`--every N`, default 20000) and at the end; the last line is the time it took |

The recording comes from the game: `src/gs/gs_record.h` describes the
format and the variables (`BEN10_GS_RECORD=<file>`,
`BEN10_GS_RECORD_AFTER=S`, `BEN10_GS_RECORD_SECONDS=S`). It holds the
game's textures and frames, so it lives in `user/` and is never versioned
(AGENTS.md s.2). A file is read only by a build of the same compiler and
target as the one that wrote it (the structs are written as raw bytes).

Typical use, to check that a change to the GS backends draws the same
thing:

```bash
cmake --build --preset linux-debug --target ben10_poe ben10_gs_replay
mkdir -p user/gs_rec
# first level reached after about 8 minutes with tools that drive the menus;
# record 15 seconds from then
BEN10_GS_THREADS=6 BEN10_GS_RECORD=$PWD/user/gs_rec/level.gsr \
  BEN10_GS_RECORD_AFTER=480 BEN10_GS_RECORD_SECONDS=15 out/build/linux-debug/ben10_poe game/SLUS_216.61
out/build/linux-debug/ben10_gs_replay user/gs_rec/level.gsr --sync > sync.txt
out/build/linux-debug/ben10_gs_replay user/gs_rec/level.gsr --bands 6 > bands.txt
diff sync.txt bands.txt   # only the last line (timing) may differ
```

Random recordings check states the game's recording does not reach:

```bash
cmake --build --preset linux-debug --target ben10_gs_synth
out/build/linux-debug/ben10_gs_synth /tmp/r1.gsr --seed 1 --records 30000
out/build/linux-debug/ben10_gs_replay /tmp/r1.gsr --sync --every 0     # reference
out/build/linux-debug/ben10_gs_replay /tmp/r1.gsr --bands 6 --every 0  # same "end" line
```

Other options:

- `--every 0`: only the final hash. The hash points wait for the queue,
  so they also end segments; use `--every 0` to time a backend or to
  check it without those extra barriers.
- `--reps N`: replay N times from the same start, for timing.
- `--limit N`: stop after record N, e.g. to count instructions with
  `valgrind --tool=callgrind` on a part of a recording (the machine's
  wall times vary by about 10%).
- `--row-band K N`: `GSCpuBackend` drawing only row band K of N on the
  replay's thread, to time one band's share of the work (its hashes
  differ by design).
- `--dump N FILE`: GS memory after record N, raw, to compare two backends
  byte by byte (a page is 8 KB).
- `--list A B`: records A to B on stderr (draws with frame, Z, texture,
  scissor and vertex range; transfers; CLUT loads; TEXFLUSH).

A replay starts without the CLUT buffer and texture page caches the
game's backend had when the recording started, so its first frames may
differ from the game's; every backend starts the same way, so they can be
compared with each other.
