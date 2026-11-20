#!/usr/bin/env python3
"""make_test_elf.py: monta um ELF minúsculo de PS2 (EE) para testar o recompilador.

PARA QUE SERVE
    Os testes por instrução (tests/instructions/) precisam passar pelo
    tradutor DE VERDADE do PS2Recomp, não só pelas macros do runtime: é o
    texto C++ que o ps2_recomp escreve para cada instrução que roda no jogo.
    Este script escreve, sem nenhum dado do jogo:

      <saída>/test_program.elf   ELF32 little-endian, MIPS R5900, um só
                                 segmento em 0x00100000 com as funções de teste
      <saída>/functions.csv      a lista de funções, no formato que o
                                 ExportPS2Functions.java do Ghidra produz
      <saída>/test_program.toml  a config do ps2_recomp para este ELF
      <saída>/test_cases.h       um #define por caso: CASE_<nome> -> o nome da
                                 função C++ gerada, e o endereço dela

    O CMake de tests/ roda este script, depois o ps2_recomp, depois compila o
    C++ gerado junto com os testes GoogleTest.

CODIFICAÇÃO
    Os formatos das instruções seguem o manual do R5900 ("EE Core
    Instruction Set Manual") e as tabelas de decodificação do PCSX2
    (pcsx2/R5900OpcodeTables.cpp). Os testes conferem, além do resultado, o
    comentário de disassembly que o ps2_recomp escreve: se a codificação
    aqui estiver errada, o mnemônico gerado não bate e o erro aparece.

USO
    python3 -I make_test_elf.py <pasta de saída>
"""
import struct
import sys
from pathlib import Path

BASE = 0x00100000  # mesmo endereço de carga do jogo (findings/09 s.4)

# ---------------------------------------------------------------------------
# Codificador
# ---------------------------------------------------------------------------
# Registradores pelo número; nomes só para legibilidade dos casos.
ZERO, AT, V0, V1, A0, A1, A2, A3 = range(8)
T0, T1, T2, T3 = 8, 9, 10, 11
RA = 31


def r_type(op, rs, rt, rd, sa, fn):
    return (op << 26) | (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | fn


def i_type(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


# Inteiros
def nop():                 return 0
def addiu(rt, rs, imm):    return i_type(0x09, rs, rt, imm)
def ori(rt, rs, imm):      return i_type(0x0D, rs, rt, imm)
def lui(rt, imm):          return i_type(0x0F, 0, rt, imm)
def addu(rd, rs, rt):      return r_type(0, rs, rt, rd, 0, 0x21)
def daddu(rd, rs, rt):     return r_type(0, rs, rt, rd, 0, 0x2D)
def dsra32(rd, rt, sa):    return r_type(0, 0, rt, rd, sa, 0x3F)
def jr(rs):                return r_type(0, rs, 0, 0, 0, 0x08)

# Memória
def lq(rt, off, base):     return i_type(0x1E, base, rt, off)
def sq(rt, off, base):     return i_type(0x1F, base, rt, off)
def lw(rt, off, base):     return i_type(0x23, base, rt, off)


# Desvios: o deslocamento é relativo à instrução SEGUINTE (pc + 4), em palavras.
def _branch(op, rs, rt, pc, target):
    return i_type(op, rs, rt, (target - (pc + 4)) >> 2)


def beq(rs, rt, pc, target):   return _branch(0x04, rs, rt, pc, target)
def beql(rs, rt, pc, target):  return _branch(0x14, rs, rt, pc, target)
def bnel(rs, rt, pc, target):  return _branch(0x15, rs, rt, pc, target)

# FPU (COP1). Formato S: fd = fs op ft.
COP1 = 0x11
def mtc1(rt, fs):          return r_type(COP1, 0x04, rt, fs, 0, 0)
def mfc1(rt, fs):          return r_type(COP1, 0x00, rt, fs, 0, 0)
def _fpu_s(fn, fd, fs, ft): return r_type(COP1, 0x10, ft, fs, fd, fn)
def add_s(fd, fs, ft):     return _fpu_s(0x00, fd, fs, ft)
def sub_s(fd, fs, ft):     return _fpu_s(0x01, fd, fs, ft)
def mul_s(fd, fs, ft):     return _fpu_s(0x02, fd, fs, ft)
def div_s(fd, fs, ft):     return _fpu_s(0x03, fd, fs, ft)
def sqrt_s(fd, ft):        return _fpu_s(0x04, fd, 0, ft)   # no R5900, SQRT.S lê ft
def cvt_w_s(fd, fs):       return _fpu_s(0x24, fd, fs, 0)

# MMI (opcode 0x1C). MMI0..MMI3 escolhem a operação pelo campo sa.
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

# VU0 em modo macro (COP2)
COP2 = 0x12
def qmtc2(rt, vf):         return r_type(COP2, 0x05, rt, vf, 0, 0)
def qmfc2(rt, vf):         return r_type(COP2, 0x01, rt, vf, 0, 0)


def vadd(dest, fd, fs, ft):
    """VADD.dest fd, fs, ft. dest: string com x/y/z/w (bit 24 = x ... bit 21 = w)."""
    mask = sum(bit for c, bit in (("x", 8), ("y", 4), ("z", 2), ("w", 1)) if c in dest)
    return (COP2 << 26) | (1 << 25) | (mask << 21) | (ft << 16) | (fs << 11) | (fd << 6) | 0x28


# ---------------------------------------------------------------------------
# Os casos
# ---------------------------------------------------------------------------
# Cada caso é uma função: recebe "pc" (o endereço de cada instrução, para os
# desvios) e devolve a lista de palavras. Toda função termina em "jr ra; nop".
# O teste C++ prepara os registradores, chama a função gerada e confere.

def ret():
    return [jr(RA), nop()]


CASES = {}


def case(fn):
    CASES[fn.__name__] = fn
    return fn


# --- Inteiros de 32/64 bits ---------------------------------------------------
@case
def t_addu_sign_extends(pc):
    # v0 = a0 + a1 (32 bits) com o resultado estendido em sinal para 64 bits.
    return [addu(V0, A0, A1)] + ret()


@case
def t_daddu_64bit(pc):
    return [daddu(V0, A0, A1)] + ret()


@case
def t_dsra32(pc):
    return [dsra32(V0, A0, 4)] + ret()   # deslocamento de 4 + 32 = 36


# --- LQ / SQ alinham o endereço em 16 bytes ----------------------------------
@case
def t_lq_ignores_low_bits(pc):
    return [lq(V0, 0x7, A0)] + ret()     # a0 + 7: o LQ usa (a0 + 7) & ~0xF


@case
def t_sq_ignores_low_bits(pc):
    return [sq(A1, 0x9, A0)] + ret()


# --- Desvios e delay slots ----------------------------------------------------
# Layout comum: v0 = 0; desvio; delay slot "v0 += 1"; (caminho não tomado)
# "v0 += 0x10"; alvo: jr ra; nop.
def _branch_case(kind):
    def build(pc):
        words = []
        words.append(addiu(V0, ZERO, 0))                 # pc+0
        target = pc + 4 * 5                               # aponta para o jr ra
        words.append(kind(A0, A1, pc + 4, target))        # pc+4
        words.append(addiu(V0, V0, 1))                    # pc+8  delay slot
        words.append(addiu(V0, V0, 0x10))                 # pc+12 só se não desviar
        words.append(nop())                               # pc+16
        words += ret()                                    # pc+20
        return words
    return build


CASES["t_beq_delay_slot"] = _branch_case(beq)
CASES["t_beql_delay_slot"] = _branch_case(beql)
CASES["t_bnel_delay_slot"] = _branch_case(bnel)


# --- FPU ----------------------------------------------------------------------
# Entradas por $f12/$f13 (como bits, via mtc1), saída lida de volta em v0.
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

# --- VU0 em modo macro --------------------------------------------------------
# vf1 = a0, vf2 = a1 (128 bits); vf3 recebe uma base conhecida (a2);
# VADD com máscara de destino; v0 = vf3.
CASES["t_vadd_xyzw"] = lambda pc: [qmtc2(A0, 1), qmtc2(A1, 2), qmtc2(A2, 3),
                                   vadd("xyzw", 3, 1, 2), qmfc2(V0, 3)] + ret()
CASES["t_vadd_x_only"] = lambda pc: [qmtc2(A0, 1), qmtc2(A1, 2), qmtc2(A2, 3),
                                     vadd("x", 3, 1, 2), qmfc2(V0, 3)] + ret()


# ---------------------------------------------------------------------------
# ELF
# ---------------------------------------------------------------------------
def build_program():
    """Coloca os casos um depois do outro, alinhados em 16 bytes."""
    layout = []   # (nome, início, fim)
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

    # e_flags 0x20924001: os mesmos bits do ELF do jogo (noreorder, 5900,
    # eabi64, mips3), para o ps2_recomp tratar igual (findings/09 s.4).
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
        f.write("// Gerado por tests/tools/make_test_elf.py: não editar.\n#pragma once\n")
        for name, start, end in layout:
            f.write(f"#define CASE_{name} {name}_0x{start:x}\n")
            f.write(f"#define ADDR_{name} 0x{start:08X}u\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
