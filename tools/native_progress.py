#!/usr/bin/env python3
"""native_progress.py: the README's "Going native" board and milestones.

WHAT IT MEASURES
    The game's code (the EE, the main CPU) is translated to C++ before it
    runs: that part is native by construction. What still pretends to be a
    PS2 is the rest of the console around it: the IOP and its modules, the
    vector units (VU), the graphics chip (GS), sound, controllers, memory
    card. Going native = the port answering each piece with its own PC code,
    without emulating the hardware.

    Each PIECE has items. An item counts as DONE only when the port's own
    code answers it (status "native"). The other statuses:
        "emulated"      PS2Recomp imitates the hardware (generic, in C++)
        "interpreted"   runs on an interpreter (temporary debt: see
                        docs/KNOWN_ISSUES.md T1/T2)
        "stub"          only pretends to answer
        "to measure"    not known yet how the boot handles it

    NOT COUNTED: the EE kernel (42 syscalls such as CreateThread,
    WaitSema...). The runtime already translates them 1:1 to the PC
    (findings/12 s.3), like Mind over Recomp's "kernel basics".

    Change a status here ONLY with evidence (a finding, a test, a log), then
    run --readme to update the README.

USAGE
    python3 tools/native_progress.py            print the board
    python3 tools/native_progress.py --readme   rewrite the block between
        <!-- native-progress:begin --> and <!-- native-progress:end --> in README.md

Inspired by Mind over Recomp's tools/native_progress.py (GPL-3.0).
"""
import argparse
import sys
from pathlib import Path

# -----------------------------------------------------------------------------
# Milestones on the way to playing (in the style of BT2 Recomp)
# status: "done" | "in progress" | "pending"
# -----------------------------------------------------------------------------
MILESTONES = [
    ("Executable analysis", "done",
     "4,420 functions; the script API table (findings/09-12)"),
    ("Translation to C++", "done",
     "0 unsupported instructions (findings/13)"),
    ("Full build", "done",
     "the generated C++ + runtime compile and link on Linux (findings/14); Windows: untested"),
    ("Per-instruction tests", "in progress",
     "33 end-to-end tests; 4 translator bugs found and fixed (findings/14)"),
    ("Boot", "in progress",
     "loads the 8 IOP modules, sets the video mode; with native CDARB reads it runs ~2,900 frames, then exits via std::terminate in a data parser (findings/15)"),
    ("First frame", "pending", "the game's first picture on screen"),
    ("Main menu", "pending", ""),
    ("Playable level", "pending", ""),
]

# -----------------------------------------------------------------------------
# The PS2 pieces: (title, what it becomes, [(item, status)])
# -----------------------------------------------------------------------------
PIECES = [
    ("IOP modules",
     "port services answering each request (RPC) of the game, without running the IRX",
     [("SIO2MAN (controller and memory card bus)", "interpreted"),
      ("MTAPMAN (multitap)", "interpreted"),
      ("PADMAN (controllers)", "interpreted"),
      ("MCMAN (memory card)", "interpreted"),
      ("MCSERV (memory card, server)", "interpreted"),
      ("LIBSD (sound chip)", "interpreted"),
      ("CDARB: the EE's disc reads (src/iop/cdarb_native.cpp)", "native"),
      ("CDARB: ALASD's audio streams", "interpreted"),
      ("ALASD (audio driver, engine's own)", "interpreted")]),
    ("VU microprograms",
     "C++ translated at build time from the .DVP.ovlytab table (findings/11)",
     [("program A", "interpreted"), ("program B", "interpreted"),
      ("program C", "interpreted"), ("program D (VU1)", "interpreted"),
      ("program E", "interpreted"), ("program F (VU1)", "interpreted"),
      ("program G", "interpreted"), ("program H (VU1)", "interpreted"),
      ("program I (VU1)", "interpreted")]),
    ("Graphics (GS)",
     "the port's Vulkan renderer",
     [("video output (mode, framebuffer on screen)", "emulated"),
      ("GIF packets to drawing", "emulated"),
      ("textures and palettes", "emulated"),
      ("blending, depth, scissor", "emulated")]),
    ("Sound",
     "each audio frame straight to the PC's output",
     [("SPU2 sound RAM (src/audio/spu2_memory.cpp)", "native"),
      ("mixing and output (SPU2)", "emulated")]),
    ("Controllers",
     "SDL3: keyboard, mouse and any controller",
     [("controller reads", "emulated")]),
    ("Saves",
     "save files on the PC, no memory card",
     [("memory card", "emulated")]),
    ("Videos",
     "the PC's video decoder (if the game has MPEG videos)",
     [("IPU / MPEG", "to measure")]),
]

NOT_COUNTED = "42 EE kernel syscalls (threads, semaphores, interrupts), already translated 1:1 by the runtime"

BEGIN = "<!-- native-progress:begin -->"
END = "<!-- native-progress:end -->"
ICON = {"done": "✅", "in progress": "🔧", "pending": "⏳"}


def bar(done, total, width=10):
    filled = round(width * done / total) if total else 0
    return "▰" * filled + "▱" * (width - filled)


def render():
    lines = [BEGIN,
             "<!-- Written by tools/native_progress.py --readme: edit the tool, not this block. -->",
             "",
             "| Milestone | | Status |",
             "|---|---|---|"]
    for name, status, note in MILESTONES:
        lines.append(f"| {name} | {ICON[status]} | {note} |")

    total = sum(len(items) for _, _, items in PIECES)
    done = sum(1 for _, _, items in PIECES for _, st in items if st == "native")
    pct = round(100 * done / total) if total else 0
    lines += ["",
              f"**PS2 pieces replaced by the port's own code: {done} of {total} ({pct}%)** `{bar(done, total)}`",
              "",
              "| PS2 piece | Replaced | Becomes |",
              "|---|---|---|"]
    for title, becomes, items in PIECES:
        d = sum(1 for _, st in items if st == "native")
        lines.append(f"| {title} | `{bar(d, len(items))}` {d} / {len(items)} | {becomes} |")
    lines += ["",
              f"Not counted: {NOT_COUNTED}.",
              END]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description="README progress board")
    ap.add_argument("--readme", action="store_true", help="rewrite the block in README.md")
    a = ap.parse_args()
    block = render()
    if not a.readme:
        print(block)
        print()
        for title, _, items in PIECES:
            print(title)
            for name, st in items:
                print(f"    [{st}] {name}")
        return 0
    readme = Path(__file__).resolve().parent.parent / "README.md"
    text = readme.read_text(encoding="utf-8")
    if BEGIN not in text or END not in text:
        print("README.md has no native-progress markers", file=sys.stderr)
        return 1
    head, rest = text.split(BEGIN, 1)
    _, tail = rest.split(END, 1)
    readme.write_text(head + block + tail, encoding="utf-8")
    print("README.md updated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
