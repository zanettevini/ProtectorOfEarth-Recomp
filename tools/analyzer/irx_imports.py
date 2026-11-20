#!/usr/bin/env python3
"""irx_imports.py: lists an IOP module's (IRX) imports and exports, and can
annotate a disassembly of it with the imported function names.

WHAT IT IS FOR
    The game ships its own IOP modules (CDARB.IRX, ALASD.IRX,
    findings/10). To replace one with native code, its RPC protocol has to
    be read from the IRX itself. An IRX calls the IOP kernel through import
    stubs: small "jr $ra; addiu $0, $0, N" pairs that follow an import table
    header naming the library ("sifcmd", "cdvdman"...). Knowing which stub
    is which turns "jal 0x1F40" into "jal sifcmd:17 (sceSifRegisterRpc)".

HOW
    * The IRX is a little-endian ELF32 (MIPS R3000). Sections are parsed
      by name; .text starts at address 0 of the module.
    * Import table: word 0x41E00000, word 0, word version, 8-byte library
      name, then 8-byte stubs until a zero pair.
    * Export table: word 0x41C00000, word 0, word version, 8-byte library
      name, then one function address per index until a zero word
      (indices 0-3 are the standard module entry points).
    * Function names come from the PS2SDK IOP headers, if a PS2SDK
      checkout is given: "#define I_name DECLARE_IMPORT(N, name)" inside
      the header that declares "<library>_IMPORTS_start".

    Nothing from the module is written to tracked files: the output is for
    reading (AGENTS.md s.2).

USAGE
    python3 -I tools/analyzer/irx_imports.py game/MODULES/CDARB.IRX [--ps2sdk PATH]
        [--annotate disassembly.txt]

    For --annotate, make the listing with
        llvm-objdump -d --no-show-raw-insn --triple=mipsel MODULE.IRX
"""
import argparse
import re
import struct
import sys
from pathlib import Path

IMPORT_MAGIC = 0x41E00000
EXPORT_MAGIC = 0x41C00000
JR_RA = 0x03E00008


def sections(data):
    """Returns {name: (file offset, size, address)} for an ELF32 LE file."""
    shoff = struct.unpack_from("<I", data, 0x20)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    heads = [struct.unpack_from("<10I", data, shoff + i * shentsize) for i in range(shnum)]
    stroff = heads[shstrndx][4]
    out = {}
    for h in heads:
        name = data[stroff + h[0]:].split(b"\0", 1)[0].decode("latin-1")
        out[name] = (h[4], h[5], h[3])
    return out


def sdk_names(sdk_root):
    """Maps library name -> {index: function name} from the PS2SDK headers."""
    names = {}
    if not sdk_root:
        return names
    for header in Path(sdk_root).rglob("*.h"):
        try:
            text = header.read_text(encoding="latin-1")
        except OSError:
            continue
        lib = re.search(r"#define\s+(\w+)_IMPORTS_start", text)
        if not lib:
            continue
        table = names.setdefault(lib.group(1), {})
        for m in re.finditer(r"DECLARE_IMPORT\((\d+),\s*(\w+)\)", text):
            table.setdefault(int(m.group(1)), m.group(2))
    return names


def scan(data, sdk):
    sec = sections(data)
    off, size, _ = sec[".text"]
    text = data[off:off + size]
    imports = {}   # stub address -> "lib:index (name)"
    exports = []   # (library, index, address)
    for a in range(0, len(text) - 20, 4):
        magic = struct.unpack_from("<I", text, a)[0]
        if magic == IMPORT_MAGIC and struct.unpack_from("<I", text, a + 4)[0] == 0:
            lib = text[a + 12:a + 20].split(b"\0", 1)[0].decode("latin-1")
            p = a + 20
            while p + 8 <= len(text):
                w0, w1 = struct.unpack_from("<II", text, p)
                if w0 != JR_RA or (w1 >> 16) != 0x2400:
                    break
                idx = w1 & 0xFFFF
                fn = sdk.get(lib, {}).get(idx, "?")
                imports[p] = f"{lib}:{idx} ({fn})"
                p += 8
        elif magic == EXPORT_MAGIC and struct.unpack_from("<I", text, a + 4)[0] == 0:
            lib = text[a + 12:a + 20].split(b"\0", 1)[0].decode("latin-1")
            p, idx = a + 20, 0
            while p + 4 <= len(text):
                fa = struct.unpack_from("<I", text, p)[0]
                if fa == 0:
                    break
                exports.append((lib, idx, fa))
                p += 4
                idx += 1
    return imports, exports


def main():
    ap = argparse.ArgumentParser(description="IRX imports/exports")
    ap.add_argument("irx")
    ap.add_argument("--ps2sdk", help="PS2SDK checkout, for function names")
    ap.add_argument("--annotate", help="llvm-objdump listing to annotate")
    a = ap.parse_args()

    data = Path(a.irx).read_bytes()
    imports, exports = scan(data, sdk_names(a.ps2sdk))
    if a.annotate:
        for line in Path(a.annotate).read_text().splitlines():
            m = re.search(r"\bjal\s+0x([0-9a-f]+)", line)
            if m and int(m.group(1), 16) in imports:
                line += f"    <{imports[int(m.group(1), 16)]}>"
            print(line)
        return 0
    print("== imports (stub address: library:index (name))")
    for addr in sorted(imports):
        print(f"  0x{addr:05X}: {imports[addr]}")
    print("== exports (library, index, address)")
    for lib, idx, fa in exports:
        print(f"  {lib}:{idx} -> 0x{fa:05X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
