#!/usr/bin/env python3
"""
ps2_elf_audit.py: static audit of a PS2 (Emotion Engine) ELF.

What it is for
--------------
Phase 1 of the master prompt: measure the executable BEFORE recompiling.
The script modifies nothing and runs none of the game: it only reads the
file and counts.

It reports:
  * the ELF header, segments (program headers) and sections;
  * the VU microprogram overlay table (.DVP.ovlytab), when present;
  * the distribution of R5900 instructions in .text: plain MIPS
    instructions, MMI (128-bit), COP0, FPU (COP1), VU0 macro mode (COP2),
    VCALLMS (VU0 microprogram calls), "likely" branches, SYSCALL and the
    syscall number loaded into $v1, indirect jumps (JR/JALR);
  * words that do not decode as a valid R5900 instruction;
  * unique JAL targets (a lower bound on the number of functions);
  * referenced hardware register addresses (lui + offset), grouped by
    block (DMAC, VIF, GIF, IPU, timers, INTC, GS...).

Usage
-----
  python -I tools/analyzer/ps2_elf_audit.py PATH/SLUS_216.61 [--json out.json]

Sources for the opcode tables and the hardware map
--------------------------------------------------
Public R5900 documentation and PCSX2's tables
(pcsx2/R5900OpcodeTables.cpp, pcsx2/Hw.h). Check there before treating this
report as the truth; see docs/findings/09.

Legal rule (AGENTS.md s.2): the output describes the executable (counts,
addresses, sizes). Do not copy code bytes into versioned files.
"""

import argparse
import collections
import json
import struct
import sys

# ---------------------------------------------------------------------------
# ELF reading (32-bit, little-endian, like every EE executable)
# ---------------------------------------------------------------------------

SHT_NOBITS = 8
SHF_EXECINSTR = 0x4


def parse_elf(data):
    """Returns (header, list of program headers, list of sections)."""
    if data[:4] != b"\x7fELF":
        raise ValueError("not an ELF file")
    if data[4] != 1 or data[5] != 1:
        raise ValueError("esperado ELF32 little-endian (EE do PS2)")
    (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags,
     e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum,
     e_shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    hdr = dict(type=e_type, machine=e_machine, entry=e_entry, flags=e_flags,
               phnum=e_phnum, shnum=e_shnum)

    phdrs = []
    for i in range(e_phnum):
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
            struct.unpack_from("<IIIIIIII", data, e_phoff + i * e_phentsize)
        phdrs.append(dict(type=p_type, offset=p_offset, vaddr=p_vaddr,
                          filesz=p_filesz, memsz=p_memsz, flags=p_flags))

    raw = []
    for i in range(e_shnum):
        raw.append(struct.unpack_from("<IIIIIIIIII", data, e_shoff + i * e_shentsize))
    strtab_off = raw[e_shstrndx][4]

    def cstr(off):
        end = data.index(b"\0", off)
        return data[off:end].decode("latin-1")

    sections = []
    for (name, s_type, s_flags, addr, off, size, link, info, align, entsize) in raw:
        sections.append(dict(name=cstr(strtab_off + name), type=s_type, flags=s_flags,
                             addr=addr, offset=off, size=size, link=link))
    return hdr, phdrs, sections


def section(sections, name):
    for s in sections:
        if s["name"] == name:
            return s
    return None


# ---------------------------------------------------------------------------
# VU overlay table (.DVP.ovlytab)
# ---------------------------------------------------------------------------
# Layout seen in this executable (findings/11 s.4): 12-byte entries
#   +0x0 offset of the name in .DVP.ovlystrtab
#   +0x4 LMA: EE address (inside .vutext) where the code is stored
#   +0x8 VMA: destination address in VU micro-memory
# It is the format the toolchain's VU assembler (dvp-as) produces; confirm
# with more than one game before treating it as general.

def parse_dvp_overlays(data, sections):
    tab = section(sections, ".DVP.ovlytab")
    strs = section(sections, ".DVP.ovlystrtab")
    if not tab:
        return []
    out = []
    for i in range(tab["size"] // 12):
        name_off, lma, vma = struct.unpack_from("<III", data, tab["offset"] + i * 12)
        name = ""
        if strs and name_off < strs["size"]:
            start = strs["offset"] + name_off
            name = data[start:data.index(b"\0", start)].decode("latin-1")
        out.append(dict(index=i, name=name, lma=lma, vma=vma))
    return out


# ---------------------------------------------------------------------------
# R5900 decoder/classifier
# ---------------------------------------------------------------------------

PRIMARY = {
    0x02: "J", 0x03: "JAL", 0x04: "BEQ", 0x05: "BNE", 0x06: "BLEZ", 0x07: "BGTZ",
    0x08: "ADDI", 0x09: "ADDIU", 0x0A: "SLTI", 0x0B: "SLTIU", 0x0C: "ANDI",
    0x0D: "ORI", 0x0E: "XORI", 0x0F: "LUI",
    0x14: "BEQL", 0x15: "BNEL", 0x16: "BLEZL", 0x17: "BGTZL",
    0x18: "DADDI", 0x19: "DADDIU", 0x1A: "LDL", 0x1B: "LDR",
    0x1E: "LQ", 0x1F: "SQ",
    0x20: "LB", 0x21: "LH", 0x22: "LWL", 0x23: "LW", 0x24: "LBU", 0x25: "LHU",
    0x26: "LWR", 0x27: "LWU", 0x28: "SB", 0x29: "SH", 0x2A: "SWL", 0x2B: "SW",
    0x2C: "SDL", 0x2D: "SDR", 0x2E: "SWR", 0x2F: "CACHE",
    0x31: "LWC1", 0x33: "PREF", 0x36: "LQC2", 0x37: "LD",
    0x39: "SWC1", 0x3E: "SQC2", 0x3F: "SD",
}

SPECIAL = {
    0x00: "SLL", 0x02: "SRL", 0x03: "SRA", 0x04: "SLLV", 0x06: "SRLV", 0x07: "SRAV",
    0x08: "JR", 0x09: "JALR", 0x0A: "MOVZ", 0x0B: "MOVN", 0x0C: "SYSCALL",
    0x0D: "BREAK", 0x0F: "SYNC", 0x10: "MFHI", 0x11: "MTHI", 0x12: "MFLO",
    0x13: "MTLO", 0x14: "DSLLV", 0x16: "DSRLV", 0x17: "DSRAV", 0x18: "MULT",
    0x19: "MULTU", 0x1A: "DIV", 0x1B: "DIVU", 0x20: "ADD", 0x21: "ADDU",
    0x22: "SUB", 0x23: "SUBU", 0x24: "AND", 0x25: "OR", 0x26: "XOR", 0x27: "NOR",
    0x28: "MFSA", 0x29: "MTSA", 0x2A: "SLT", 0x2B: "SLTU", 0x2C: "DADD",
    0x2D: "DADDU", 0x2E: "DSUB", 0x2F: "DSUBU", 0x30: "TGE", 0x31: "TGEU",
    0x32: "TLT", 0x33: "TLTU", 0x34: "TEQ", 0x36: "TNE", 0x38: "DSLL",
    0x3A: "DSRL", 0x3B: "DSRA", 0x3C: "DSLL32", 0x3E: "DSRL32", 0x3F: "DSRA32",
}

REGIMM = {
    0x00: "BLTZ", 0x01: "BGEZ", 0x02: "BLTZL", 0x03: "BGEZL", 0x08: "TGEI",
    0x09: "TGEIU", 0x0A: "TLTI", 0x0B: "TLTIU", 0x0C: "TEQI", 0x0E: "TNEI",
    0x10: "BLTZAL", 0x11: "BGEZAL", 0x12: "BLTZALL", 0x13: "BGEZALL",
    0x18: "MTSAB", 0x19: "MTSAH",
}

MMI = {
    0x00: "MADD", 0x01: "MADDU", 0x04: "PLZCW", 0x10: "MFHI1", 0x11: "MTHI1",
    0x12: "MFLO1", 0x13: "MTLO1", 0x18: "MULT1", 0x19: "MULTU1", 0x1A: "DIV1",
    0x1B: "DIVU1", 0x20: "MADD1", 0x21: "MADDU1", 0x30: "PMFHL", 0x31: "PMTHL",
    0x34: "PSLLH", 0x36: "PSRLH", 0x37: "PSRAH", 0x3C: "PSLLW", 0x3E: "PSRLW",
    0x3F: "PSRAW",
}
MMI0 = {0x00: "PADDW", 0x01: "PSUBW", 0x02: "PCGTW", 0x03: "PMAXW", 0x04: "PADDH",
        0x05: "PSUBH", 0x06: "PCGTH", 0x07: "PMAXH", 0x08: "PADDB", 0x09: "PSUBB",
        0x0A: "PCGTB", 0x10: "PADDSW", 0x11: "PSUBSW", 0x12: "PEXTLW", 0x13: "PPACW",
        0x14: "PADDSH", 0x15: "PSUBSH", 0x16: "PEXTLH", 0x17: "PPACH", 0x18: "PADDSB",
        0x19: "PSUBSB", 0x1A: "PEXTLB", 0x1B: "PPACB", 0x1E: "PEXT5", 0x1F: "PPAC5"}
MMI1 = {0x01: "PABSW", 0x02: "PCEQW", 0x03: "PMINW", 0x04: "PADSBH", 0x05: "PABSH",
        0x06: "PCEQH", 0x07: "PMINH", 0x0A: "PCEQB", 0x10: "PADDUW", 0x11: "PSUBUW",
        0x12: "PEXTUW", 0x14: "PADDUH", 0x15: "PSUBUH", 0x16: "PEXTUH", 0x18: "PADDUB",
        0x19: "PSUBUB", 0x1A: "PEXTUB", 0x1B: "QFSRV"}
MMI2 = {0x00: "PMADDW", 0x02: "PSLLVW", 0x03: "PSRLVW", 0x04: "PMSUBW", 0x08: "PMFHI",
        0x09: "PMFLO", 0x0A: "PINTH", 0x0C: "PMULTW", 0x0D: "PDIVW", 0x0E: "PCPYLD",
        0x10: "PMADDH", 0x11: "PHMADH", 0x12: "PAND", 0x13: "PXOR", 0x14: "PMSUBH",
        0x15: "PHMSBH", 0x1A: "PEXEH", 0x1B: "PREVH", 0x1C: "PMULTH", 0x1D: "PDIVBW",
        0x1E: "PEXEW", 0x1F: "PROT3W"}
MMI3 = {0x00: "PMADDUW", 0x03: "PSRAVW", 0x08: "PMTHI", 0x09: "PMTLO", 0x0A: "PINTEH",
        0x0C: "PMULTUW", 0x0D: "PDIVUW", 0x0E: "PCPYUD", 0x12: "POR", 0x13: "PNOR",
        0x1A: "PEXCH", 0x1B: "PCPYH", 0x1E: "PEXCW"}
MMI_SUB = {0x08: MMI0, 0x28: MMI1, 0x09: MMI2, 0x29: MMI3}

COP0_RS = {0x00: "MFC0", 0x04: "MTC0", 0x08: "BC0x"}
COP0_C0 = {0x01: "TLBR", 0x02: "TLBWI", 0x06: "TLBWR", 0x08: "TLBP",
           0x18: "ERET", 0x38: "EI", 0x39: "DI"}

COP1_RS = {0x00: "MFC1", 0x02: "CFC1", 0x04: "MTC1", 0x06: "CTC1", 0x08: "BC1x"}
COP1_S = {0x00: "ADD.S", 0x01: "SUB.S", 0x02: "MUL.S", 0x03: "DIV.S", 0x04: "SQRT.S",
          0x05: "ABS.S", 0x06: "MOV.S", 0x07: "NEG.S", 0x16: "RSQRT.S", 0x18: "ADDA.S",
          0x19: "SUBA.S", 0x1A: "MULA.S", 0x1C: "MADD.S", 0x1D: "MSUB.S",
          0x1E: "MADDA.S", 0x1F: "MSUBA.S", 0x24: "CVT.W.S", 0x28: "MAX.S",
          0x29: "MIN.S", 0x30: "C.F.S", 0x32: "C.EQ.S", 0x34: "C.LT.S", 0x36: "C.LE.S"}

COP2_RS = {0x01: "QMFC2", 0x02: "CFC2", 0x05: "QMTC2", 0x06: "CTC2", 0x08: "BC2x"}


def classify(w):
    """Returns (category, mnemonic). mnemonic None = invalid word."""
    op = w >> 26
    rs = (w >> 21) & 31
    rt = (w >> 16) & 31
    sa = (w >> 6) & 31
    fn = w & 63
    if op == 0x00:
        return "special", SPECIAL.get(fn)
    if op == 0x01:
        return "regimm", REGIMM.get(rt)
    if op == 0x1C:
        if fn in MMI_SUB:
            return "mmi", MMI_SUB[fn].get(sa)
        return "mmi", MMI.get(fn)
    if op == 0x10:
        if rs == 0x10:
            return "cop0", COP0_C0.get(fn)
        return "cop0", COP0_RS.get(rs)
    if op == 0x11:
        if rs == 0x10:
            return "fpu", COP1_S.get(fn)
        if rs == 0x14:
            return "fpu", "CVT.S.W" if fn == 0x20 else None
        return "fpu", COP1_RS.get(rs)
    if op == 0x12:
        if rs >= 0x10:
            # VU0 macro mode. 0x38/0x39 = VCALLMS/VCALLMSR (starts a VU0 microprogram).
            if fn == 0x38:
                return "cop2", "VCALLMS"
            if fn == 0x39:
                return "cop2", "VCALLMSR"
            return "cop2", "VU0_MACRO"
        return "cop2", COP2_RS.get(rs)
    name = PRIMARY.get(op)
    if name in ("LWC1", "SWC1"):
        return "fpu", name
    if name in ("LQC2", "SQC2"):
        return "cop2", name
    if name in ("LQ", "SQ"):
        return "r5900_128", name
    return "primary", name


# ---------------------------------------------------------------------------
# EE hardware register map (physical address -> block)
# Source: public documentation / PCSX2 Hw.h. Check before relying on it.
# ---------------------------------------------------------------------------

HW_BLOCKS = [
    (0x10000000, 0x10002000, "TIMER"),
    (0x10002000, 0x10003000, "IPU"),
    (0x10003000, 0x10003800, "GIF"),
    (0x10003800, 0x10003C00, "VIF0"),
    (0x10003C00, 0x10004000, "VIF1"),
    (0x10004000, 0x10005000, "VIF0_FIFO"),
    (0x10005000, 0x10006000, "VIF1_FIFO"),
    (0x10006000, 0x10007000, "GIF_FIFO"),
    (0x10007000, 0x10008000, "IPU_FIFO"),
    (0x10008000, 0x10009000, "DMA_VIF0"),
    (0x10009000, 0x1000A000, "DMA_VIF1"),
    (0x1000A000, 0x1000B000, "DMA_GIF"),
    (0x1000B000, 0x1000B400, "DMA_fromIPU"),
    (0x1000B400, 0x1000C000, "DMA_toIPU"),
    (0x1000C000, 0x1000C400, "DMA_SIF0"),
    (0x1000C400, 0x1000C800, "DMA_SIF1"),
    (0x1000C800, 0x1000D000, "DMA_SIF2"),
    (0x1000D000, 0x1000D400, "DMA_fromSPR"),
    (0x1000D400, 0x1000E000, "DMA_toSPR"),
    (0x1000E000, 0x1000F000, "DMAC_CTRL"),
    (0x1000F000, 0x10010000, "INTC_SIF_misc"),
    (0x11000000, 0x11010000, "VU_MEM"),
    (0x12000000, 0x12002000, "GS_PRIV"),
    (0x70000000, 0x70004000, "SCRATCHPAD"),
]


def hw_block(addr):
    # KSEG0 (0x8/0x9...) and KSEG1 (0xA/0xB...) mirror physical memory:
    # strip the high bits. The scratchpad really lives at 0x70000000. Any
    # other segment (e.g. 0x5xxxxxxx) is NOT hardware: the first version of
    # this script masked everything and counted 0x5000000x constants as
    # TIMER.
    top = addr >> 28
    if top == 0x7:
        phys = addr
    elif top in (0x1, 0x8, 0x9, 0xA, 0xB):
        phys = addr & 0x1FFFFFFF
    else:
        return None
    for lo, hi, name in HW_BLOCKS:
        if lo <= phys < hi:
            return name
    return None


LOADSTORE_OR_ADDI = {0x09, 0x0D, 0x19, 0x1E, 0x1F, 0x20, 0x21, 0x23, 0x24, 0x25,
                     0x27, 0x28, 0x29, 0x2B, 0x31, 0x36, 0x37, 0x39, 0x3E, 0x3F}


def sext16(v):
    return v - 0x10000 if v & 0x8000 else v


# ---------------------------------------------------------------------------
# Audit
# ---------------------------------------------------------------------------

def audit(path):
    data = open(path, "rb").read()
    hdr, phdrs, sections = parse_elf(data)

    report = dict(file=path, bytes=len(data), header=hdr, segments=phdrs,
                  sections=[dict(name=s["name"], addr=s["addr"], size=s["size"],
                                 offset=s["offset"], flags=s["flags"])
                            for s in sections])
    report["dvp_overlays"] = parse_dvp_overlays(data, sections)

    text = section(sections, ".text")
    words = struct.unpack_from("<%dI" % (text["size"] // 4), data, text["offset"])
    base = text["addr"]

    by_cat = collections.Counter()
    by_mn = collections.Counter()
    invalid = []
    jal_targets = set()
    syscall_ids = collections.Counter()
    hw_hits = collections.Counter()
    hw_addrs = collections.defaultdict(set)

    for i, w in enumerate(words):
        cat, mn = classify(w)
        if mn is None:
            by_cat["INVALID"] += 1
            if len(invalid) < 50:
                invalid.append(base + i * 4)
            continue
        by_cat[cat] += 1
        by_mn[mn] += 1
        op = w >> 26

        if mn == "JAL":
            jal_targets.add(((base + i * 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2))

        if mn == "SYSCALL":
            # The kernel service number goes in $v1 (reg 3), usually through
            # an "addiu/ori v1, zero, N" right before. Looks up to 4 back.
            sid = None
            for back in range(1, 5):
                if i - back < 0:
                    break
                pw = words[i - back]
                pop = pw >> 26
                if (pop in (0x09, 0x0D)) and ((pw >> 16) & 31) == 3 and ((pw >> 21) & 31) == 0:
                    imm = pw & 0xFFFF
                    sid = sext16(imm) if pop == 0x09 else imm
                    break
            syscall_ids["?" if sid is None else str(sid)] += 1

        if op == 0x0F:  # LUI: look for use as a hardware address base
            hi = (w & 0xFFFF) << 16
            if hw_block(hi) or hw_block(hi + 0x8000):
                reg = (w >> 16) & 31
                for ahead in range(1, 9):
                    if i + ahead >= len(words):
                        break
                    nw = words[i + ahead]
                    nop = nw >> 26
                    if nop in LOADSTORE_OR_ADDI and ((nw >> 21) & 31) == reg:
                        addr = (hi + sext16(nw & 0xFFFF)) & 0xFFFFFFFF
                        blk = hw_block(addr)
                        if blk:
                            hw_hits[blk] += 1
                            hw_addrs[blk].add(addr)
                    # the register was overwritten: stop following
                    if nop in (0x0F, 0x09, 0x0D, 0x23, 0x37, 0x1E) and ((nw >> 16) & 31) == reg:
                        break

    report["text"] = dict(
        addr=base, words=len(words),
        by_category=dict(by_cat.most_common()),
        by_mnemonic=dict(by_mn.most_common()),
        invalid_first_addrs=[hex(a) for a in invalid],
        unique_jal_targets=len(jal_targets),
        syscall_v1_ids=dict(syscall_ids.most_common()),
        hw_blocks=dict(hw_hits.most_common()),
        hw_addrs={k: sorted(hex(a) for a in v) for k, v in hw_addrs.items()},
    )
    return report


def print_report(r):
    h = r["header"]
    print("file:", r["file"], "(%d bytes)" % r["bytes"])
    print("entry: 0x%08x  flags: 0x%08x" % (h["entry"], h["flags"]))
    for p in r["segments"]:
        print("segment: vaddr 0x%08x filesz 0x%x memsz 0x%x" % (p["vaddr"], p["filesz"], p["memsz"]))
    print("\nsections:")
    for s in r["sections"]:
        if s["size"]:
            print("  %-40s addr 0x%08x size 0x%06x" % (s["name"][:40], s["addr"], s["size"]))
    ov = r["dvp_overlays"]
    print("\nVU overlays (.DVP.ovlytab): %d" % len(ov))
    for o in ov:
        print("  #%02d lma 0x%08x -> vma 0x%04x  %s" % (o["index"], o["lma"], o["vma"], o["name"]))
    t = r["text"]
    print("\n.text: %d words" % t["words"])
    for k, v in t["by_category"].items():
        print("  %-10s %7d" % (k, v))
    print("unique JAL targets:", t["unique_jal_targets"])
    print("first invalid words:", t["invalid_first_addrs"][:10])
    print("\nR5900-specific mnemonics:")
    r5900 = {"MMI", "VU0_MACRO", "VCALLMS", "VCALLMSR", "LQ", "SQ", "LQC2", "SQC2",
             "QMFC2", "QMTC2", "CFC2", "CTC2", "MFSA", "MTSA", "MTSAB", "MTSAH",
             "MULT1", "MULTU1", "DIV1", "DIVU1", "MADD1", "MADDU1", "MFHI1",
             "MFLO1", "MTHI1", "MTLO1", "EI", "DI", "ERET", "SYNC", "CACHE", "PREF"}
    tables = [MMI, MMI0, MMI1, MMI2, MMI3]
    mmi_names = {n for tbl in tables for n in tbl.values()}
    for mn, c in t["by_mnemonic"].items():
        if mn in r5900 or mn in mmi_names:
            print("  %-10s %6d" % (mn, c))
    print("\nFPU:", {k: v for k, v in t["by_mnemonic"].items() if k.endswith(".S") or k in ("MFC1", "MTC1", "CFC1", "CTC1", "LWC1", "SWC1", "CVT.S.W", "BC1x")})
    print("\nsyscalls ($v1):", t["syscall_v1_ids"])
    print("\nreferenced hardware blocks:")
    for k, v in t["hw_blocks"].items():
        print("  %-14s %5d  %s" % (k, v, " ".join(t["hw_addrs"][k][:8])))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("elf")
    ap.add_argument("--json", help="write the full report as JSON")
    a = ap.parse_args()
    r = audit(a.elf)
    print_report(r)
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(r, f, indent=1)


if __name__ == "__main__":
    sys.exit(main())
