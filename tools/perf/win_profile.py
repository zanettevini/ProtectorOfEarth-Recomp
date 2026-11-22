#!/usr/bin/env python3
"""win_profile.py: reads a profile written by the sampling profiler of the
Windows build (BEN10_PROFILE, src/diagnostics/sample_profiler.cpp) and the
linker map of the same build (ben10_poe.map, written next to ben10_poe.exe),
and prints where the sampled thread spent its time.

Usage:
  python3 tools/perf/win_profile.py profile.txt ben10_poe.map [--windows A-B] [--top N]

  --windows A-B  only the 10-second windows A to B (the list of windows with
                 their sample counts and top function is printed first, to
                 pick e.g. the part spent in a level); default: all.
  --top N        rows per table (default 40).

Tables:
  - self: samples whose innermost frame is in the function (time spent in
    the function's own code, including what the compiler inlined into it);
  - inclusive: samples with the function anywhere on the stack (counted once
    per sample);
  - groups: self time summed by area (recompiled game code, VU1 translated
    code, VU1 interpreter, GS, IOP, C runtime and system DLLs...).

Function names come from the map's public and static symbols; MSVC names are
demangled with llvm-undname when it is installed. Addresses outside
ben10_poe.exe are shown by DLL name. A sample is roughly one millisecond.
"""

import argparse
import bisect
import collections
import os
import re
import shutil
import subprocess
import sys


def parse_map(path):
    """Returns (sorted rvas, names) of the code symbols in an MSVC linker map."""
    base = None
    entries = []
    line_re = re.compile(r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s+(f?)\s*")
    with open(path, "r", errors="replace") as f:
        for line in f:
            if base is None:
                m = re.search(r"Preferred load address is ([0-9a-fA-F]+)", line)
                if m:
                    base = int(m.group(1), 16)
                continue
            m = line_re.match(line)
            if m:
                va = int(m.group(2), 16)
                if va >= base:
                    entries.append((va - base, m.group(1)))
    entries.sort()
    return [e[0] for e in entries], [e[1] for e in entries]


def demangle(names):
    """MSVC name -> readable name (parameters dropped) via llvm-undname."""
    tool = shutil.which("llvm-undname")
    result = {n: n for n in names}
    mangled = [n for n in names if n.startswith("?")]
    if not tool or not mangled:
        return result
    out = subprocess.run([tool], input="\n".join(mangled) + "\n", capture_output=True, text=True).stdout
    blocks = out.split("\n\n")
    for block in blocks:
        lines = block.strip("\n").split("\n")
        if len(lines) >= 2 and lines[0] in result and not lines[1].startswith("error"):
            text = lines[1]
            text = re.sub(r"^(public|private|protected): ", "", text)
            text = re.sub(r"\b(static|virtual) ", "", text)
            text = text.replace("__cdecl ", "")
            # Drop the return type (text before the qualified name) and the
            # parameter list, keeping template arguments readable.
            depth = 0
            cut = len(text)
            for i, ch in enumerate(text):
                if ch == "<":
                    depth += 1
                elif ch == ">":
                    depth -= 1
                elif ch == "(" and depth == 0:
                    cut = i
                    break
            head = text[:cut]
            depth = 0
            start = 0
            for i, ch in enumerate(head):
                if ch == "<":
                    depth += 1
                elif ch == ">":
                    depth -= 1
                elif ch == " " and depth == 0:
                    start = i + 1
            result[lines[0]] = head[start:] or text
    return result


GROUPS = [
    ("VU1 translated code", re.compile(r"vu1_native|anonymous namespace'::p[0-9A-F]+_\d+|VU1NativeAccess|VU1NativeLowerAccess")),
    ("VU1 interpreter (step, queues, FMAC)", re.compile(r"VU1Interpreter|ps2_vu1")),
    ("VU0 / VIF", re.compile(r"VU0|Vif|VIF")),
    ("GS (front end, queue, GIF)", re.compile(r"\bGS::|gs_async|GifArbiter|GSMem|gs_")),
    ("IOP", re.compile(r"iop|Iop|IOP|ps2x::")),
    ("recompiled game code", re.compile(r"_0x[0-9a-fA-F]+$")),
    ("EE runtime (memory, DMA, syscalls, scheduler)", re.compile(r"PS2Memory|Ps2|PS2|EeScheduler|ps2_syscalls|ps2_stubs|R5900")),
]


def group_of(name):
    if name.startswith("[") and name.endswith("]"):
        return "system / C runtime DLLs"
    for label, pattern in GROUPS:
        if pattern.search(name):
            return label
    return "other"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("profile")
    ap.add_argument("mapfile")
    ap.add_argument("--windows", default=None)
    ap.add_argument("--top", type=int, default=40)
    args = ap.parse_args()

    rvas, raw_names = parse_map(args.mapfile)
    names = demangle(sorted(set(raw_names)))

    modules = {}
    windows = []  # (index, start_ms, end_ms, [(count, [frames])])
    with open(args.profile, "r", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("module "):
                _, idx, path = line.split(" ", 2)
                modules[idx] = path
            elif line.startswith("window "):
                p = line.split()
                windows.append((int(p[1]), int(p[3]), int(p[5]), []))
            elif line and line[0].isdigit() and windows:
                p = line.split()
                windows[-1][3].append((int(p[0]), p[1:]))

    exe_index = None
    for idx, path in modules.items():
        if os.path.basename(path.replace("\\", "/")).lower() == "ben10_poe.exe":
            exe_index = idx

    cache = {}

    def name_of(frame):
        if frame in cache:
            return cache[frame]
        mod, _, off = frame.partition(":")
        if mod == exe_index:
            rva = int(off, 16)
            i = bisect.bisect_right(rvas, rva) - 1
            n = names.get(raw_names[i], raw_names[i]) if i >= 0 else "?"
        elif mod in modules:
            n = "[" + os.path.basename(modules[mod].replace("\\", "/")) + "]"
        else:
            n = "[unknown]"
        cache[frame] = n
        return n

    print("windows (10 s each): index, time, samples, top function by self time")
    for idx, start, end, stacks in windows:
        total = sum(c for c, _ in stacks)
        top = collections.Counter()
        for c, frames in stacks:
            top[name_of(frames[0])] += c
        first = top.most_common(1)
        print(f"  {idx:3d}  {start / 1000:6.0f}-{end / 1000:4.0f} s  {total:6d}  {first[0][0][:70] + ' ' + str(round(100 * first[0][1] / total)) + '%' if first else ''}")

    chosen = windows
    if args.windows:
        a, _, b = args.windows.partition("-")
        lo, hi = int(a), int(b or a)
        chosen = [w for w in windows if lo <= w[0] <= hi]
    self_t = collections.Counter()
    incl_t = collections.Counter()
    groups = collections.Counter()
    total = 0
    for _, _, _, stacks in chosen:
        for c, frames in stacks:
            total += c
            leaf = name_of(frames[0])
            self_t[leaf] += c
            groups[group_of(leaf)] += c
            for n in set(name_of(fr) for fr in frames):
                incl_t[n] += c
    if total == 0:
        print("no samples in the chosen windows")
        return
    print(f"\nsamples: {total} in {len(chosen)} window(s)")
    print("\ngroups (self time):")
    for g, c in groups.most_common():
        print(f"  {100 * c / total:5.1f}%  {g}")
    print(f"\nself time, top {args.top}:")
    for n, c in self_t.most_common(args.top):
        print(f"  {100 * c / total:5.1f}%  {n[:150]}")
    print(f"\ninclusive time, top {args.top}:")
    for n, c in incl_t.most_common(args.top):
        print(f"  {100 * c / total:5.1f}%  {n[:150]}")


if __name__ == "__main__":
    main()
