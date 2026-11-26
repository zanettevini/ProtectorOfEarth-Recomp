#!/usr/bin/env python3
"""make_test_elf.py: builds a tiny PS2 (EE) ELF to test the recompiler.

WHAT IT IS FOR
    The per-instruction tests (tests/instructions/) must go through
    PS2Recomp's REAL translator, not just the runtime macros: what runs in
    the game is the C++ text ps2_recomp writes for each instruction.
    This script writes, with no game data at all:

      <out>/test_program.elf   ELF32 little-endian, MIPS R5900, a single
                               segment at 0x00100000 holding the test functions
      <out>/functions.csv      the function list, in the format Ghidra's
                               ExportPS2Functions.java produces
      <out>/test_program.toml  the ps2_recomp config for this ELF
      <out>/test_cases.h       one #define per case: CASE_<name> -> the name
                               of the generated C++ function, and its address

    The tests/ CMake runs this script, then ps2_recomp, then compiles the
    generated C++ together with the GoogleTest tests.

ENCODING
    Instruction formats follow the R5900 manual ("EE Core Instruction Set
    Manual") and PCSX2's decode tables (pcsx2/R5900OpcodeTables.cpp). Besides
    the result, the disassembly comment ps2_recomp writes was checked: if an
    encoding here were wrong, the generated mnemonic would not match and the
    mistake would show.

USAGE
    python3 -I make_test_elf.py <output folder>
"""
import struct
import sys
from pathlib import Path

BASE = 0x00100000  # same load address as the game (findings/09 s.4)

# ---------------------------------------------------------------------------
# Encoder
# ---------------------------------------------------------------------------
# Registers by number; names only make the cases readable.
ZERO, AT, V0, V1, A0, A1, A2, A3 = range(8)
T0, T1, T2, T3 = 8, 9, 10, 11
RA = 31


def r_type(op, rs, rt, rd, sa, fn):
    return (op << 26) | (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | fn


def i_type(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


# Integers
def nop():                 return 0
def addiu(rt, rs, imm):    return i_type(0x09, rs, rt, imm)
def ori(rt, rs, imm):      return i_type(0x0D, rs, rt, imm)
def lui(rt, imm):          return i_type(0x0F, 0, rt, imm)
def addu(rd, rs, rt):      return r_type(0, rs, rt, rd, 0, 0x21)
def daddu(rd, rs, rt):     return r_type(0, rs, rt, rd, 0, 0x2D)
def dsra32(rd, rt, sa):    return r_type(0, 0, rt, rd, sa, 0x3F)
def jr(rs):                return r_type(0, rs, 0, 0, 0, 0x08)

# Memory
def lq(rt, off, base):     return i_type(0x1E, base, rt, off)
def sq(rt, off, base):     return i_type(0x1F, base, rt, off)
def lw(rt, off, base):     return i_type(0x23, base, rt, off)
# Unaligned 64-bit loads and stores (PCSX2 R5900OpcodeTables.cpp: LDL 0x1A,
# LDR 0x1B, SDL 0x2C, SDR 0x2D)
def ldl(rt, off, base):    return i_type(0x1A, base, rt, off)
def ldr(rt, off, base):    return i_type(0x1B, base, rt, off)
def sdl(rt, off, base):    return i_type(0x2C, base, rt, off)
def sdr(rt, off, base):    return i_type(0x2D, base, rt, off)
# REGIMM (opcode 1) MTSAB: rt field 0x18 (PCSX2 tbl_REGIMM)
def mtsab(rs, imm):        return i_type(0x01, rs, 0x18, imm)
def mtsah(rs, imm):        return i_type(0x01, rs, 0x19, imm)
# MMI pipeline-1 and multiply-add (opcode 0x1C, picked by the function field).
def _mmi_fn(fn):           return lambda rd, rs, rt: r_type(0x1C, rs, rt, rd, 0, fn)
madd, maddu, mult1, multu1 = _mmi_fn(0x00), _mmi_fn(0x01), _mmi_fn(0x18), _mmi_fn(0x19)
madd1, maddu1 = _mmi_fn(0x20), _mmi_fn(0x21)
def div1(rs, rt):          return r_type(0x1C, rs, rt, 0, 0, 0x1A)
def divu1(rs, rt):         return r_type(0x1C, rs, rt, 0, 0, 0x1B)
def mfhi1(rd):             return r_type(0x1C, 0, 0, rd, 0, 0x10)
def mthi1(rs):             return r_type(0x1C, rs, 0, 0, 0, 0x11)
def mflo1(rd):             return r_type(0x1C, 0, 0, rd, 0, 0x12)
def mtlo1(rs):             return r_type(0x1C, rs, 0, 0, 0, 0x13)
def pext5(rd, rt):         return r_type(0x1C, 0, rt, rd, 0x1E, 0x08)  # MMI0
def ppac5(rd, rt):         return r_type(0x1C, 0, rt, rd, 0x1F, 0x08)  # MMI0


# Branches: the offset is relative to the NEXT instruction (pc + 4), in words.
def _branch(op, rs, rt, pc, target):
    return i_type(op, rs, rt, (target - (pc + 4)) >> 2)


def beq(rs, rt, pc, target):   return _branch(0x04, rs, rt, pc, target)
def beql(rs, rt, pc, target):  return _branch(0x14, rs, rt, pc, target)
def bnel(rs, rt, pc, target):  return _branch(0x15, rs, rt, pc, target)

# FPU (COP1). S format: fd = fs op ft.
COP1 = 0x11
def mtc1(rt, fs):          return r_type(COP1, 0x04, rt, fs, 0, 0)
def mfc1(rt, fs):          return r_type(COP1, 0x00, rt, fs, 0, 0)
def _fpu_s(fn, fd, fs, ft): return r_type(COP1, 0x10, ft, fs, fd, fn)
def add_s(fd, fs, ft):     return _fpu_s(0x00, fd, fs, ft)
def sub_s(fd, fs, ft):     return _fpu_s(0x01, fd, fs, ft)
def mul_s(fd, fs, ft):     return _fpu_s(0x02, fd, fs, ft)
def div_s(fd, fs, ft):     return _fpu_s(0x03, fd, fs, ft)
def sqrt_s(fd, ft):        return _fpu_s(0x04, fd, 0, ft)   # on the R5900, SQRT.S reads ft
def cvt_w_s(fd, fs):       return _fpu_s(0x24, fd, fs, 0)
def cfc1(rt, fs):          return r_type(COP1, 0x02, rt, fs, 0, 0)
# PCSX2 R5900OpcodeTables.cpp tbl_COP1_S: RSQRT_S 0x16, ADDA_S 0x18,
# MADD_S 0x1C, MSUB_S 0x1D, C_F 0x30, C_EQ 0x32, C_LT 0x34, C_LE 0x36.
def rsqrt_s(fd, fs, ft):   return _fpu_s(0x16, fd, fs, ft)
def adda_s(fs, ft):        return _fpu_s(0x18, 0, fs, ft)
def madd_s(fd, fs, ft):    return _fpu_s(0x1C, fd, fs, ft)
def msub_s(fd, fs, ft):    return _fpu_s(0x1D, fd, fs, ft)
def c_f_s(fs, ft):         return _fpu_s(0x30, 0, fs, ft)
def c_eq_s(fs, ft):        return _fpu_s(0x32, 0, fs, ft)
def c_lt_s(fs, ft):        return _fpu_s(0x34, 0, fs, ft)
def c_le_s(fs, ft):        return _fpu_s(0x36, 0, fs, ft)
def max_s(fd, fs, ft):     return _fpu_s(0x28, fd, fs, ft)
def min_s(fd, fs, ft):     return _fpu_s(0x29, fd, fs, ft)

# MMI (opcode 0x1C). MMI0..MMI3 pick the operation by the sa field.
MMI = 0x1C
def plzcw(rd, rs):         return r_type(MMI, rs, 0, rd, 0, 0x04)
def paddsh(rd, rs, rt):    return r_type(MMI, rs, rt, rd, 0x14, 0x08)  # MMI0
def ppach(rd, rs, rt):     return r_type(MMI, rs, rt, rd, 0x17, 0x08)  # MMI0
def pextlw(rd, rs, rt):    return r_type(MMI, rs, rt, rd, 0x12, 0x08)  # MMI0
def psubuh(rd, rs, rt):    return r_type(MMI, rs, rt, rd, 0x15, 0x28)  # MMI1
def padduh(rd, rs, rt):    return r_type(MMI, rs, rt, rd, 0x14, 0x28)  # MMI1
def pcpyld(rd, rs, rt):    return r_type(MMI, rs, rt, rd, 0x0E, 0x09)  # MMI2
def pcpyud(rd, rs, rt):    return r_type(MMI, rs, rt, rd, 0x0E, 0x29)  # MMI3
def pnor(rd, rs, rt):      return r_type(MMI, rs, rt, rd, 0x13, 0x29)  # MMI3
def pexeh(rd, rt):         return r_type(MMI, 0, rt, rd, 0x1A, 0x09)   # MMI2
def pexew(rd, rt):         return r_type(MMI, 0, rt, rd, 0x1E, 0x09)   # MMI2
def prot3w(rd, rt):        return r_type(MMI, 0, rt, rd, 0x1F, 0x09)   # MMI2
def pexch(rd, rt):         return r_type(MMI, 0, rt, rd, 0x1A, 0x29)   # MMI3
def pexcw(rd, rt):         return r_type(MMI, 0, rt, rd, 0x1E, 0x29)   # MMI3
def qfsrv(rd, rs, rt):     return r_type(MMI, rs, rt, rd, 0x1B, 0x28)  # MMI1
def pmthi(rs):             return r_type(MMI, rs, 0, 0, 0x08, 0x29)    # MMI3
def pmtlo(rs):             return r_type(MMI, rs, 0, 0, 0x09, 0x29)    # MMI3
def pmfhl(rd, fmt):        return r_type(MMI, 0, 0, rd, fmt, 0x30)     # sa = format
def pmthl_lw(rs):          return r_type(MMI, rs, 0, 0, 0x00, 0x31)
def _mmi2(sa):             return lambda rd, rs, rt: r_type(MMI, rs, rt, rd, sa, 0x09)
def _mmi3(sa):             return lambda rd, rs, rt: r_type(MMI, rs, rt, rd, sa, 0x29)
pmaddw, pmsubw, pmultw, pdivw = _mmi2(0x00), _mmi2(0x04), _mmi2(0x0C), _mmi2(0x0D)
pmaddh, phmadh, pmsubh, phmsbh = _mmi2(0x10), _mmi2(0x11), _mmi2(0x14), _mmi2(0x15)
pmulth, pdivbw = _mmi2(0x1C), _mmi2(0x1D)
pmadduw, pmultuw, pdivuw = _mmi3(0x00), _mmi3(0x0C), _mmi3(0x0D)
def pmfhi(rd):             return r_type(MMI, 0, 0, rd, 0x08, 0x09)    # MMI2
def pmflo(rd):             return r_type(MMI, 0, 0, rd, 0x09, 0x09)    # MMI2

# VU0 macro mode (COP2)
COP2 = 0x12
def qmtc2(rt, vf):         return r_type(COP2, 0x05, rt, vf, 0, 0)
def qmfc2(rt, vf):         return r_type(COP2, 0x01, rt, vf, 0, 0)


def vadd(dest, fd, fs, ft):
    """VADD.dest fd, fs, ft. dest: a string of x/y/z/w (bit 24 = x ... bit 21 = w)."""
    mask = sum(bit for c, bit in (("x", 8), ("y", 4), ("z", 2), ("w", 1)) if c in dest)
    return (COP2 << 26) | (1 << 25) | (mask << 21) | (ft << 16) | (fs << 11) | (fd << 6) | 0x28


# ---------------------------------------------------------------------------
# The cases
# ---------------------------------------------------------------------------
# Each case is a function: it takes "pc" (the address of each instruction, for
# branches) and returns the list of words. Every function ends in "jr ra; nop".
# The C++ test sets up the registers, calls the generated function and checks.

def ret():
    return [jr(RA), nop()]


CASES = {}


def case(fn):
    CASES[fn.__name__] = fn
    return fn


# --- 32/64-bit integers -------------------------------------------------------
@case
def t_addu_sign_extends(pc):
    # v0 = a0 + a1 (32 bits) with the result sign-extended to 64 bits.
    return [addu(V0, A0, A1)] + ret()


@case
def t_daddu_64bit(pc):
    return [daddu(V0, A0, A1)] + ret()


@case
def t_dsra32(pc):
    return [dsra32(V0, A0, 4)] + ret()   # shift by 4 + 32 = 36


# --- LQ / SQ align the address to 16 bytes ----------------------------------
@case
def t_lq_ignores_low_bits(pc):
    return [lq(V0, 0x7, A0)] + ret()     # a0 + 7: LQ uses (a0 + 7) & ~0xF


@case
def t_sq_ignores_low_bits(pc):
    return [sq(A1, 0x9, A0)] + ret()


# --- Branches and delay slots -------------------------------------------------
# Common layout: v0 = 0; branch; delay slot "v0 += 1"; (not-taken path)
# "v0 += 0x10"; target: jr ra; nop.
def _branch_case(kind):
    def build(pc):
        words = []
        words.append(addiu(V0, ZERO, 0))                 # pc+0
        target = pc + 4 * 5                               # points at the jr ra
        words.append(kind(A0, A1, pc + 4, target))        # pc+4
        words.append(addiu(V0, V0, 1))                    # pc+8  delay slot
        words.append(addiu(V0, V0, 0x10))                 # pc+12 only if not taken
        words.append(nop())                               # pc+16
        words += ret()                                    # pc+20
        return words
    return build


CASES["t_beq_delay_slot"] = _branch_case(beq)
CASES["t_beql_delay_slot"] = _branch_case(beql)
CASES["t_bnel_delay_slot"] = _branch_case(bnel)


# --- FPU ----------------------------------------------------------------------
# Inputs through $f12/$f13 (as bits, via mtc1), output read back into v0.
def _fpu_case(op):
    def build(pc):
        return [mtc1(A0, 12), mtc1(A1, 13), op, mfc1(V0, 0)] + ret()
    return build


CASES["t_cvt_w_s"] = _fpu_case(cvt_w_s(0, 12))
CASES["t_add_s"] = _fpu_case(add_s(0, 12, 13))
CASES["t_sub_s"] = _fpu_case(sub_s(0, 12, 13))
CASES["t_mul_s"] = _fpu_case(mul_s(0, 12, 13))
CASES["t_div_s"] = _fpu_case(div_s(0, 12, 13))
CASES["t_sqrt_s"] = _fpu_case(sqrt_s(0, 12))

# --- MMI (128 bits) -----------------------------------------------------------
CASES["t_pcpyld"] = lambda pc: [pcpyld(V0, A0, A1)] + ret()
CASES["t_pcpyud"] = lambda pc: [pcpyud(V0, A0, A1)] + ret()
CASES["t_pextlw"] = lambda pc: [pextlw(V0, A0, A1)] + ret()
CASES["t_paddsh"] = lambda pc: [paddsh(V0, A0, A1)] + ret()
CASES["t_psubuh"] = lambda pc: [psubuh(V0, A0, A1)] + ret()
CASES["t_padduh"] = lambda pc: [padduh(V0, A0, A1)] + ret()
CASES["t_ppach"] = lambda pc: [ppach(V0, A0, A1)] + ret()
CASES["t_pnor"] = lambda pc: [pnor(V0, A0, A1)] + ret()
CASES["t_plzcw"] = lambda pc: [plzcw(V0, A0)] + ret()
# Word/halfword permutes: one source (rt = a1), rs is 0 in the encoding.
CASES["t_pexeh"] = lambda pc: [pexeh(V0, A1)] + ret()
CASES["t_pexew"] = lambda pc: [pexew(V0, A1)] + ret()
CASES["t_prot3w"] = lambda pc: [prot3w(V0, A1)] + ret()
CASES["t_pexch"] = lambda pc: [pexch(V0, A1)] + ret()
CASES["t_pexcw"] = lambda pc: [pexcw(V0, A1)] + ret()

# FPU, R13 (findings/14 s.7): the FPU control register (FCR31) is read
# back into v1 after the operation.
CASES["t_rsqrt_s"] = lambda pc: [mtc1(A0, 12), mtc1(A1, 13), rsqrt_s(0, 12, 13),
                                 mfc1(V0, 0), cfc1(V1, 31)] + ret()
# ACC = a2 + 0 (ADDA.S with $f15 = 0), then MADD.S/MSUB.S $f0 = ACC +- a0 * a1.
def _acc_case(op):
    def build(pc):
        return [mtc1(A0, 12), mtc1(A1, 13), mtc1(A2, 14), mtc1(ZERO, 15), adda_s(14, 15),
                op, mfc1(V0, 0), cfc1(V1, 31)] + ret()
    return build


CASES["t_madd_s"] = _acc_case(madd_s(0, 12, 13))
CASES["t_msub_s"] = _acc_case(msub_s(0, 12, 13))
# C.cond.S $f12, $f13, then v0 = FCR31 (bit 23 = C).
def _cmp_case(op):
    def build(pc):
        return [mtc1(A0, 12), mtc1(A1, 13), op, cfc1(V0, 31)] + ret()
    return build


CASES["t_c_eq_s"] = _cmp_case(c_eq_s(12, 13))
CASES["t_c_lt_s"] = _cmp_case(c_lt_s(12, 13))
CASES["t_c_le_s"] = _cmp_case(c_le_s(12, 13))
CASES["t_max_s"] = lambda pc: [mtc1(A0, 12), mtc1(A1, 13), max_s(0, 12, 13), mfc1(V0, 0)] + ret()
CASES["t_min_s"] = lambda pc: [mtc1(A0, 12), mtc1(A1, 13), min_s(0, 12, 13), mfc1(V0, 0)] + ret()
# C.EQ sets C, then C.F clears it.
CASES["t_c_f_s"] = lambda pc: [mtc1(A0, 12), mtc1(A0, 13), c_eq_s(12, 13), c_f_s(12, 13),
                               cfc1(V0, 31)] + ret()

# --- Unaligned 64-bit loads and stores, R13 -------------------------------
# v0 starts as a2; LDL/LDR merge bytes from the doubleword at (a0 + off) & ~7.
CASES["t_ldl"] = lambda pc: [daddu(V0, A2, ZERO), ldl(V0, 3, A0)] + ret()
CASES["t_ldr"] = lambda pc: [daddu(V0, A2, ZERO), ldr(V0, 5, A0)] + ret()
# The usual unaligned load idiom: ldl v0, 7(a0); ldr v0, 0(a0).
CASES["t_ldl_ldr_pair"] = lambda pc: [ldl(V0, 7, A0), ldr(V0, 0, A0)] + ret()
CASES["t_sdl"] = lambda pc: [sdl(A1, 2, A0)] + ret()
CASES["t_sdr"] = lambda pc: [sdr(A1, 6, A0)] + ret()
# The usual unaligned store idiom: sdl a1, 7(a0); sdr a1, 0(a0).
CASES["t_sdl_sdr_pair"] = lambda pc: [sdl(A1, 7, A0), sdr(A1, 0, A0)] + ret()

# --- MMI, R13 ------------------------------------------------------------------
# SA = a2 & 0xF (MTSAB a2, 0), then QFSRV v0, a0, a1 (a0 = rs, a1 = rt).
CASES["t_qfsrv"] = lambda pc: [mtsab(A2, 0), qfsrv(V0, A0, A1)] + ret()
CASES["t_qfsrv_h"] = lambda pc: [mtsah(A2, 0), qfsrv(V0, A0, A1)] + ret()
# Pipeline 1 (HI/LO bits 64-127) and the multiply-adds; the test sets HI/LO in the context.
for _name, _op in [("madd", madd), ("maddu", maddu), ("mult1", mult1), ("multu1", multu1),
                   ("madd1", madd1), ("maddu1", maddu1)]:
    CASES[f"t_{_name}"] = (lambda o: (lambda pc: [o(V0, A0, A1)] + ret()))(_op)
CASES["t_div1"] = lambda pc: [div1(A0, A1)] + ret()
CASES["t_divu1"] = lambda pc: [divu1(A0, A1)] + ret()
CASES["t_hilo1_moves"] = lambda pc: [mthi1(A0), mtlo1(A1), mfhi1(V0), mflo1(V1)] + ret()
CASES["t_pext5"] = lambda pc: [pext5(V0, A0)] + ret()
CASES["t_ppac5"] = lambda pc: [ppac5(V0, A0)] + ret()
# HI = a1, LO = a0 (128 bits), then PMFHL.fmt v0.
for _fmt, _name in ((0, "lw"), (1, "uw"), (2, "slw"), (3, "lh"), (4, "sh")):
    CASES[f"t_pmfhl_{_name}"] = (lambda f: (lambda pc: [pmthi(A1), pmtlo(A0), pmfhl(V0, f)] + ret()))(_fmt)

# 128-bit HI/LO: HI <- a2 and LO <- a3 first, then the operation on a0/a1
# into v0. The test reads HI/LO straight from the context.
for _name, _op in [("pmulth", pmulth), ("pmaddh", pmaddh), ("pmsubh", pmsubh),
                   ("phmadh", phmadh), ("phmsbh", phmsbh), ("pmultw", pmultw),
                   ("pmultuw", pmultuw), ("pmaddw", pmaddw), ("pmsubw", pmsubw),
                   ("pmadduw", pmadduw), ("pdivw", pdivw), ("pdivuw", pdivuw),
                   ("pdivbw", pdivbw)]:
    CASES[f"t_{_name}"] = (lambda o: (lambda pc: [pmthi(A2), pmtlo(A3), o(V0, A0, A1)] + ret()))(_op)
CASES["t_pmthi_pmfhi"] = lambda pc: [pmthi(A0), pmfhi(V0)] + ret()
CASES["t_pmtlo_pmflo"] = lambda pc: [pmtlo(A0), pmflo(V0)] + ret()
CASES["t_pmthl_lw"] = lambda pc: [pmthi(A2), pmtlo(A3), pmthl_lw(A0)] + ret()

# --- VU0 macro mode -----------------------------------------------------------
# vf1 = a0, vf2 = a1 (128 bits); vf3 gets a known base (a2);
# VADD with a destination mask; v0 = vf3.
CASES["t_vadd_xyzw"] = lambda pc: [qmtc2(A0, 1), qmtc2(A1, 2), qmtc2(A2, 3),
                                   vadd("xyzw", 3, 1, 2), qmfc2(V0, 3)] + ret()
CASES["t_vadd_x_only"] = lambda pc: [qmtc2(A0, 1), qmtc2(A1, 2), qmtc2(A2, 3),
                                     vadd("x", 3, 1, 2), qmfc2(V0, 3)] + ret()


# ---------------------------------------------------------------------------
# ELF
# ---------------------------------------------------------------------------
def build_program():
    """Lays the cases out one after another, aligned to 16 bytes."""
    layout = []   # (name, start, end)
    code = bytearray()
    for name, fn in CASES.items():
        while len(code) % 16:
            code += struct.pack("<I", nop())
        start = BASE + len(code)
        for w in fn(start):
            code += struct.pack("<I", w)
        layout.append((name, start, BASE + len(code)))
    return bytes(code), layout


def write_elf(path, text):
    shstr = b"\0.text\0.shstrtab\0"
    ehsize, phsize, shsize = 52, 32, 40
    text_off = 0x1000
    shstr_off = text_off + len(text)
    shoff = (shstr_off + len(shstr) + 3) & ~3

    # e_flags 0x20924001: the same bits as the game's ELF (noreorder, 5900,
    # eabi64, mips3), so ps2_recomp treats it the same way (findings/09 s.4).
    hdr = b"\x7fELF" + bytes([1, 1, 1, 0]) + bytes(8)
    hdr += struct.pack("<HHIIIIIHHHHHH", 2, 8, 1, BASE, ehsize, shoff, 0x20924001,
                       ehsize, phsize, 1, shsize, 3, 2)
    ph = struct.pack("<IIIIIIII", 1, text_off, BASE, BASE, len(text), len(text), 7, 0x1000)

    sh_null = bytes(shsize)
    sh_text = struct.pack("<IIIIIIIIII", 1, 1, 0x6, BASE, text_off, len(text), 0, 0, 16, 0)
    sh_str = struct.pack("<IIIIIIIIII", 7, 3, 0, 0, shstr_off, len(shstr), 0, 0, 1, 0)

    out = bytearray(hdr + ph)
    out += bytes(text_off - len(out))
    out += text + shstr
    out += bytes(shoff - len(out))
    out += sh_null + sh_text + sh_str
    Path(path).write_bytes(out)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    outdir = Path(sys.argv[1])
    outdir.mkdir(parents=True, exist_ok=True)

    text, layout = build_program()
    write_elf(outdir / "test_program.elf", text)

    with open(outdir / "functions.csv", "w", encoding="utf-8") as f:
        f.write("Name,Start,End,Size\n")
        for name, start, end in layout:
            f.write(f"{name},0x{start:08X},0x{end:08X},{end - start}\n")

    gen = (outdir / "generated").as_posix()
    (outdir / "test_program.toml").write_text(
        "[general]\n"
        f'input = "{(outdir / "test_program.elf").as_posix()}"\n'
        f'ghidra_output = "{(outdir / "functions.csv").as_posix()}"\n'
        f'output = "{gen}"\n'
        "single_file_output = true\n"
        "patch_syscalls = false\n"
        "patch_cop0 = true\n"
        "patch_cache = true\n"
        "stubs = []\n"
        "skip = []\n", encoding="utf-8")

    with open(outdir / "test_cases.h", "w", encoding="utf-8") as f:
        f.write("// Generated by tests/tools/make_test_elf.py: do not edit.\n#pragma once\n")
        for name, start, end in layout:
            f.write(f"#define CASE_{name} {name}_0x{start:x}\n")
            f.write(f"#define ADDR_{name} 0x{start:08X}u\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
