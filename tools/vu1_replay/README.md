# vu1_replay

Offline replay of the game's VU1 microprogram runs (findings/20).

| File | What it does |
|---|---|
| `capture_vu1.gdb` | gdb script: while the game runs, saves 40 VU1 workloads (code, data, registers) at the moment VIF1 resumes a program (MSCNT) into `user/vu1_captures/` |
| `vu1_replay.cpp` | the `ben10_vu1_replay` target: runs the saved workloads through PS2Recomp's VU1 interpreter; `--sum` prints a checksum per workload of everything the run produced, `--reps N` repeats them for timing |

The captures are the game's code and data: they live in `user/` and are
never versioned (AGENTS.md s.2).

Typical use, to check that a change to the VU code does not change any
result:

```bash
mkdir -p user/vu1_captures
gdb -p "$(pgrep -x ben10_poe)" -batch -x tools/vu1_replay/capture_vu1.gdb
cmake --build --preset linux-debug --target ben10_vu1_replay
out/build/linux-debug/ben10_vu1_replay user/vu1_captures --sum > before.txt
# ... change, rebuild ...
out/build/linux-debug/ben10_vu1_replay user/vu1_captures --sum > after.txt
diff before.txt after.txt   # only the last line (timing) may differ
```

For a profile: `valgrind --tool=callgrind out/build/linux-debug/ben10_vu1_replay user/vu1_captures`
(instruction counts do not depend on the machine's load, unlike the
wall time).
