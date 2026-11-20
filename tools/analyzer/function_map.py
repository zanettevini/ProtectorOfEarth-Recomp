#!/usr/bin/env python3
"""function_map.py: for each function in the ELF, what it calls and which strings it uses.

WHAT IT IS FOR
    The game executable has no symbols. To bind Sony library functions
    (libsif, libcdvd, libpad...) to PS2Recomp's runtime handlers, they have
    to be recognised with evidence. This script gathers two clues per
    function:

      * direct calls (JAL) to other functions, with the name the Ghidra CSV
        gives (FUN_..., ass_..., or the named syscall stubs);
      * strings referenced through LUI + ADDIU/ORI pairs (the way the
        compiler builds a 32-bit address), for example the error messages
        Sony's libraries carry.

    The output is text (to read) or JSON. It describes the executable:
    addresses, names and short pieces of strings; none of it goes into
    versioned files beyond what is needed to justify a name (AGENTS.md s.2).

USAGE
    python3 -I tools/analyzer/function_map.py game/SLUS_216.61 generated/ghidra/functions.csv \
        [--range 0x1C5000-0x1CB000] [--callers-of 0x1C79F0] [--string sceSif] [--json out.json]
"""
import argparse
import bisect
import csv
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ps2_elf_audit as A  # noqa: E402  (same directory)


def load(elf_path, csv_path):
    data = Path(elf_path).read_bytes()
    _, phdrs, sections = A.parse_elf(data)
    seg = phdrs[0]
    text = A.section(sections, ".text")

    funcs = []
    for r in csv.DictReader(open(csv_path, encoding="utf-8")):
        name = r["Name"]
        # The ExportPS2Functions.java CSV mixes functions and internal labels.
        if name.startswith(("entry_", "caseD", "default", "switchD")):
            continue
        funcs.append((int(r["Start"], 16), int(r["End"], 16), name))
    funcs.sort()
    return data, seg, text, funcs


def read_cstring(data, seg, va, limit=120):
    off = va - seg["vaddr"] + seg["offset"]
    if not (seg["offset"] <= off < seg["offset"] + seg["filesz"]):
        return None
    end = data.find(b"\0", off, off + limit)
    if end <= off:
        return None
    raw = data[off:end]
    if len(raw) < 4 or not all(32 <= b < 127 or b in (9, 10) for b in raw):
        return None
    return raw.decode("latin-1")


def analyze(data, seg, text, funcs):
    starts = [f[0] for f in funcs]
    names = {f[0]: f[2] for f in funcs}

    def word(va):
        return struct.unpack_from("<I", data, va - text["addr"] + text["offset"])[0]

    out = {}
    for start, end, name in funcs:
        if not (text["addr"] <= start < text["addr"] + text["size"]):
            continue
        calls, strings, consts = [], [], set()
        lui = {}  # register -> high value loaded by LUI
        for pc in range(start, end, 4):
            w = word(pc)
            op = w >> 26
            rs, rt = (w >> 21) & 31, (w >> 16) & 31
            imm = w & 0xFFFF
            if op == 0x03:  # JAL
                tgt = (pc & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
                calls.append(tgt)
            elif op == 0x0F:  # LUI
                lui[rt] = imm << 16
            elif op in (0x09, 0x0D) and rs in lui:  # ADDIU / ORI on top of the LUI
                lo = A.sext16(imm) if op == 0x09 else imm
                va = (lui[rs] + lo) & 0xFFFFFFFF
                s = read_cstring(data, seg, va)
                if s:
                    strings.append((va, s))
                if op == 0x0D:
                    consts.add(va)
                lui.pop(rs, None)
                if rt != rs:
                    lui.pop(rt, None)
        out[start] = {
            "name": name, "start": start, "end": end,
            "calls": sorted(set(calls)),
            "strings": strings,
            "or_consts": sorted(consts),
        }
    return out, names


def fmt_call(addr, names):
    return f"{names.get(addr, 'no_function')}@0x{addr:08X}"


def main():
    ap = argparse.ArgumentParser(description="call and string map per function")
    ap.add_argument("elf")
    ap.add_argument("functions_csv")
    ap.add_argument("--range", help="only functions in this range, e.g. 0x1C5000-0x1CB000")
    ap.add_argument("--callers-of", help="list who calls this address")
    ap.add_argument("--string", help="only functions that use a string containing this text")
    ap.add_argument("--json")
    a = ap.parse_args()

    data, seg, text, funcs = load(a.elf, a.functions_csv)
    info, names = analyze(data, seg, text, funcs)

    sel = list(info.values())
    if a.range:
        lo, hi = (int(x, 16) for x in a.range.split("-"))
        sel = [f for f in sel if lo <= f["start"] < hi]
    if a.callers_of:
        t = int(a.callers_of, 16)
        sel = [f for f in sel if t in f["calls"]]
    if a.string:
        sel = [f for f in sel if any(a.string in s for _, s in f["strings"])]

    if a.json:
        Path(a.json).write_text(json.dumps(sel, indent=1), encoding="utf-8")
    for f in sel:
        print(f"{f['name']} 0x{f['start']:08X}-0x{f['end']:08X} ({f['end'] - f['start']} bytes)")
        if f["calls"]:
            print("   calls: " + ", ".join(fmt_call(c, names) for c in f["calls"]))
        for va, s in f["strings"][:6]:
            print(f"   string 0x{va:08X}: {s[:70]!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
