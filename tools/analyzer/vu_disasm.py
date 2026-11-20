#!/usr/bin/env python3
"""vu_disasm.py: a disassembler for PS2 VU microcode (VU0/VU1 micro mode).

WHAT IT IS FOR
    The game's VU1 programs (findings/11) still run in the runtime's VU
    interpreter (KNOWN_ISSUES T2). To check what a program does, or to find
    which instruction the interpreter gets wrong, we need to read it. This
    prints a 64-bit VU instruction pair per line: the upper (FMAC) half and
    the lower half, with the upper flag bits (I, E, M, D, T).

INPUT
    A raw dump of VU micro memory (16 KB for VU1), for example written by a
    gdb script from the runtime's VU1Interpreter (findings/18 s.9). Nothing
    from the game is stored in this tool.

ENCODING (EE/VU manuals; the same tables PCSX2's VU disassembler uses)
    Each instruction is 8 bytes: low word = lower instruction, high word =
    upper instruction. Upper bits 31..27 = I, E, M, D, T. With I set, the
    lower word is a 32-bit float loaded into the I register.
    Fields: dest = bits 24..21 (x = bit 24), ft = 20..16, fs = 15..11,
    fd = 10..6. Upper special ops (bits 5..0 = 0x3C..0x3F) and lower
    special ops (lower opcode 0x40, bits 5..0 = 0x3C..0x3F) are indexed by
    (bits 10..6) * 4 + bits 1..0.

USAGE
    python3 -I tools/analyzer/vu_disasm.py vu1code.bin [--start 0x0] [--count 64]
    (addresses in bytes; one instruction pair = 8 bytes)
"""
import argparse
import struct

BC = "xyzw"


def dest(w):
    d = (w >> 21) & 15
    return "".join(c for i, c in enumerate("xyzw") if d & (8 >> i))


def ft(w): return (w >> 16) & 31
def fs(w): return (w >> 11) & 31
def fd(w): return (w >> 6) & 31


UPPER = {}
for i, name in enumerate(["ADD", "SUB", "MADD", "MSUB", "MAX", "MINI", "MUL"]):
    for b in range(4):
        UPPER[i * 4 + b] = (name + BC[b], "bc")
UPPER.update({0x1C: ("MULq", "q"), 0x1D: ("MAXi", "i"), 0x1E: ("MULi", "i"), 0x1F: ("MINIi", "i"),
              0x20: ("ADDq", "q"), 0x21: ("MADDq", "q"), 0x22: ("ADDi", "i"), 0x23: ("MADDi", "i"),
              0x24: ("SUBq", "q"), 0x25: ("MSUBq", "q"), 0x26: ("SUBi", "i"), 0x27: ("MSUBi", "i"),
              0x28: ("ADD", "3"), 0x29: ("MADD", "3"), 0x2A: ("MUL", "3"), 0x2B: ("MAX", "3"),
              0x2C: ("SUB", "3"), 0x2D: ("MSUB", "3"), 0x2E: ("OPMSUB", "3"), 0x2F: ("MINI", "3")})

USPECIAL = {}
for i, name in enumerate(["ADDA", "SUBA", "MADDA", "MSUBA"]):
    for b in range(4):
        USPECIAL[i * 4 + b] = (name + BC[b], "abc")
for b, n in enumerate(["ITOF0", "ITOF4", "ITOF12", "ITOF15"]):
    USPECIAL[0x10 + b] = (n, "conv")
for b, n in enumerate(["FTOI0", "FTOI4", "FTOI12", "FTOI15"]):
    USPECIAL[0x14 + b] = (n, "conv")
for b in range(4):
    USPECIAL[0x18 + b] = ("MULA" + BC[b], "abc")
USPECIAL.update({0x1C: ("MULAq", "aq"), 0x1D: ("ABS", "conv"), 0x1E: ("MULAi", "aq"), 0x1F: ("CLIP", "clip"),
                 0x20: ("ADDAq", "aq"), 0x21: ("MADDAq", "aq"), 0x22: ("ADDAi", "aq"), 0x23: ("MADDAi", "aq"),
                 0x24: ("SUBAq", "aq"), 0x25: ("MSUBAq", "aq"), 0x26: ("SUBAi", "aq"), 0x27: ("MSUBAi", "aq"),
                 0x28: ("ADDA", "a2"), 0x29: ("MADDA", "a2"), 0x2A: ("MULA", "a2"),
                 0x2C: ("SUBA", "a2"), 0x2D: ("MSUBA", "a2"), 0x2E: ("OPMULA", "a2"), 0x2F: ("NOP", "none")})


def upper(w):
    op = w & 0x3F
    d = dest(w)
    if op >= 0x3C:
        idx = (((w >> 6) & 0x1F) << 2) | (w & 3)
        name, kind = USPECIAL.get(idx, ("?upper-special-%02X" % idx, "none"))
        if kind == "none":
            return name
        if kind == "abc":
            return "%s.%s ACC, vf%02d, vf%02d%s" % (name, d, fs(w), ft(w), BC[w & 3])
        if kind == "conv":
            return "%s.%s vf%02d, vf%02d" % (name, d, ft(w), fs(w))
        if kind == "aq":
            return "%s.%s ACC, vf%02d, %s" % (name, d, fs(w), "Q" if "q" in name else "I")
        if kind == "a2":
            return "%s.%s ACC, vf%02d, vf%02d" % (name, d, fs(w), ft(w))
        if kind == "clip":
            return "CLIPw.xyz vf%02d, vf%02dw" % (fs(w), ft(w))
    name, kind = UPPER.get(op, ("?upper-%02X" % op, "none"))
    if kind == "bc":
        return "%s.%s vf%02d, vf%02d, vf%02d%s" % (name, d, fd(w), fs(w), ft(w), BC[op & 3])
    if kind in ("q", "i"):
        return "%s.%s vf%02d, vf%02d, %s" % (name, d, fd(w), fs(w), kind.upper())
    if kind == "3":
        return "%s.%s vf%02d, vf%02d, vf%02d" % (name, d, fd(w), fs(w), ft(w))
    return name


def s11(w):
    v = w & 0x7FF
    return v - 0x800 if v & 0x400 else v


LSPECIAL = {0x30: "MOVE", 0x31: "MR32", 0x34: "LQI", 0x35: "SQI", 0x36: "LQD", 0x37: "SQD",
            0x38: "DIV", 0x39: "SQRT", 0x3A: "RSQRT", 0x3B: "WAITQ", 0x3C: "MTIR", 0x3D: "MFIR",
            0x3E: "ILWR", 0x3F: "ISWR", 0x40: "RNEXT", 0x41: "RGET", 0x42: "RINIT", 0x43: "RXOR",
            0x64: "MFP", 0x68: "XTOP", 0x69: "XITOP", 0x6C: "XGKICK", 0x70: "ESADD", 0x71: "ERSADD",
            0x72: "ELENG", 0x73: "ERLENG", 0x74: "EATANxy", 0x75: "EATANxz", 0x76: "ESUM",
            0x78: "ESQRT", 0x79: "ERSQRT", 0x7A: "ERCPR", 0x7B: "WAITP", 0x7C: "ESIN", 0x7D: "EATAN",
            0x7E: "EEXP"}
LOWER = {0x00: "LQ", 0x01: "SQ", 0x04: "ILW", 0x05: "ISW", 0x08: "IADDIU", 0x09: "ISUBIU",
         0x10: "FCEQ", 0x11: "FCSET", 0x12: "FCAND", 0x13: "FCOR", 0x14: "FSEQ", 0x15: "FSSET",
         0x16: "FSAND", 0x17: "FSOR", 0x18: "FMEQ", 0x1A: "FMAND", 0x1B: "FMOR", 0x1C: "FCGET",
         0x20: "B", 0x21: "BAL", 0x24: "JR", 0x25: "JALR", 0x28: "IBEQ", 0x29: "IBNE",
         0x2C: "IBLTZ", 0x2D: "IBGTZ", 0x2E: "IBLEZ", 0x2F: "IBGEZ"}


def lower(w, addr):
    op = w >> 25
    d = dest(w)
    t, s, dd = ft(w), fs(w), fd(w)
    if op == 0x40:
        f = w & 0x3F
        if f >= 0x3C:
            idx = (((w >> 6) & 0x1F) << 2) | (w & 3)
            n = LSPECIAL.get(idx, "?lower-special-%02X" % idx)
            fsf, ftf = "xyzw"[(w >> 21) & 3], "xyzw"[(w >> 23) & 3]
            if n in ("MOVE", "MR32"):
                return "%s.%s vf%02d, vf%02d" % (n, d, t, s)
            if n in ("LQI", "LQD"):
                return "%s.%s vf%02d, (vi%02d%s)" % (n, d, t, s, "++" if n == "LQI" else "--")
            if n in ("SQI", "SQD"):
                return "%s.%s vf%02d, (vi%02d%s)" % (n, d, s, t, "++" if n == "SQI" else "--")
            if n == "DIV":
                return "DIV Q, vf%02d%s, vf%02d%s" % (s, fsf, t, ftf)
            if n == "SQRT":
                return "SQRT Q, vf%02d%s" % (t, ftf)
            if n == "RSQRT":
                return "RSQRT Q, vf%02d%s, vf%02d%s" % (s, fsf, t, ftf)
            if n == "MTIR":
                return "MTIR vi%02d, vf%02d%s" % (t, s, fsf)
            if n == "MFIR":
                return "MFIR.%s vf%02d, vi%02d" % (d, t, s)
            if n in ("ILWR", "ISWR"):
                return "%s.%s vi%02d, (vi%02d)" % (n, d, t, s)
            if n in ("XTOP", "XITOP"):
                return "%s vi%02d" % (n, t)
            if n == "XGKICK":
                return "XGKICK vi%02d" % s
            if n == "MFP":
                return "MFP.%s vf%02d, P" % (d, t)
            if n.startswith("E"):
                return "%s P, vf%02d%s" % (n, s, "" if n in ("ESADD", "ERSADD", "ELENG", "ERLENG", "ESUM") else fsf)
            return n
        if f == 0x30:
            return "IADD vi%02d, vi%02d, vi%02d" % (dd, s, t)
        if f == 0x31:
            return "ISUB vi%02d, vi%02d, vi%02d" % (dd, s, t)
        if f == 0x32:
            imm = (w >> 6) & 31
            imm = imm - 32 if imm & 16 else imm
            return "IADDI vi%02d, vi%02d, %d" % (t, s, imm)
        if f == 0x34:
            return "IAND vi%02d, vi%02d, vi%02d" % (dd, s, t)
        if f == 0x35:
            return "IOR vi%02d, vi%02d, vi%02d" % (dd, s, t)
        return "?lower40-%02X" % f
    n = LOWER.get(op, "?lower-%02X" % op)
    if n in ("LQ",):
        return "LQ.%s vf%02d, %d(vi%02d)" % (d, t, s11(w), s)
    if n in ("SQ",):
        return "SQ.%s vf%02d, %d(vi%02d)" % (d, s, s11(w), t)
    if n in ("ILW", "ISW"):
        return "%s.%s vi%02d, %d(vi%02d)" % (n, d, t, s11(w), s)
    if n in ("IADDIU", "ISUBIU"):
        imm = ((w >> 10) & 0x7800) | (w & 0x7FF)
        return "%s vi%02d, vi%02d, %d" % (n, t, s, imm)
    if n.startswith("IB") or n in ("B", "BAL"):
        target = addr + 8 + s11(w) * 8
        if n in ("B",):
            return "B 0x%04X" % target
        if n == "BAL":
            return "BAL vi%02d, 0x%04X" % (t, target)
        if n in ("IBEQ", "IBNE"):
            return "%s vi%02d, vi%02d, 0x%04X" % (n, t, s, target)
        return "%s vi%02d, 0x%04X" % (n, s, target)
    if n in ("JR", "JALR"):
        return "%s vi%02d" % (n, s) if n == "JR" else "JALR vi%02d, vi%02d" % (t, s)
    if n.startswith("F"):
        return "%s vi01, 0x%06X" % (n, w & 0xFFFFFF) if n not in ("FMEQ", "FMAND", "FMOR", "FCGET") \
            else "%s vi%02d, vi%02d" % (n, t, s)
    if w == 0x8000033C:
        return "NOP"
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("code")
    ap.add_argument("--start", type=lambda x: int(x, 0), default=0)
    ap.add_argument("--count", type=lambda x: int(x, 0), default=2048)
    a = ap.parse_args()
    data = open(a.code, "rb").read()
    for n in range(a.count):
        addr = a.start + 8 * n
        if addr + 8 > len(data):
            break
        lo, hi = struct.unpack_from("<II", data, addr)
        flags = "".join(f for f, b in zip("IEMDT", (31, 30, 29, 28, 27)) if hi & (1 << b))
        u = upper(hi & 0x07FFFFFF)
        if hi & (1 << 31):
            l = "LOI %g" % struct.unpack("<f", struct.pack("<I", lo))[0]
        else:
            l = "NOP" if lo == 0x8000033C else lower(lo, addr)
        print("%04X: %-5s %-44s %s" % (addr, flags, u, l))


if __name__ == "__main__":
    main()
