#!/usr/bin/env python3
"""rpc_calls.py: lists every sceSifBindRpc / sceSifCallRpc call in the ELF.

WHAT IT IS FOR
    The Sony libraries that talk to the IOP (libcdvd, libpad, libmc, and
    the game's audio driver) do everything over RPC: first they "bind" a
    client struct to a server number (SID), then they call numbered
    functions of that server. Gathering, for each game function,
      (client struct -> SID) and (function number, send size, reply size)
    identifies the function reliably, by comparison with the (open) PS2SDK.
    See docs/findings/15.

HOW
    Inside each function, follows the constant values loaded into the
    argument registers (LUI/ADDIU/ORI/DADDU with $zero) up to each JAL to
    BindRpc or CallRpc; the delay-slot instruction counts (it runs before
    the call). EE EABI convention: arguments in a0-a3, t0-t3.
        sceSifBindRpc(cd=a0, sid=a1, mode=a2)
        sceSifCallRpc(cd=a0, fno=a1, mode=a2, send=a3, ssize=t0, recv=t1,
                      rsize=t2, endfunc=t3, endparam=stack)

USO
    python3 -I tools/analyzer/rpc_calls.py game/SLUS_216.61 generated/ghidra/functions.csv \
        --bind 0x1C82B0 --call 0x1C8490
"""
import argparse
import collections
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ps2_elf_audit as A  # noqa: E402
import function_map as M   # noqa: E402

A0, A1, A2, A3, T0, T1, T2, T3 = 4, 5, 6, 7, 8, 9, 10, 11


def track(word, regs):
    """Updates the reg -> constant map with one instruction (anything else clears the destination)."""
    op = word >> 26
    rs, rt, rd = (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
    imm = word & 0xFFFF
    fn = word & 63
    if op == 0x0F:                                   # LUI
        regs[rt] = imm << 16
    elif op == 0x09:                                 # ADDIU
        if rs == 0:
            regs[rt] = A.sext16(imm) & 0xFFFFFFFF
        elif rs in regs:
            regs[rt] = (regs[rs] + A.sext16(imm)) & 0xFFFFFFFF
        else:
            regs.pop(rt, None)
    elif op == 0x0D:                                 # ORI
        if rs == 0:
            regs[rt] = imm
        elif rs in regs:
            regs[rt] = regs[rs] | imm
        else:
            regs.pop(rt, None)
    elif op == 0 and fn in (0x2D, 0x21, 0x25) and rt == 0:   # DADDU/ADDU/OR rd, rs, $zero
        if rs == 0:
            regs[rd] = 0
        elif rs in regs:
            regs[rd] = regs[rs]
        else:
            regs.pop(rd, None)
    elif op == 0 and rd:                             # other ALU: destination unknown
        regs.pop(rd, None)
    elif op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x37, 0x1E, 0x08, 0x0A, 0x0B, 0x0C, 0x0E, 0x18, 0x19):
        regs.pop(rt, None)                           # loads and other I-types write rt


def scan(data, text, funcs, bind_addr, call_addr):
    def word(va):
        return int.from_bytes(data[va - text["addr"] + text["offset"]:][:4], "little")

    binds, calls = [], []
    for start, end, name in funcs:
        if not (text["addr"] <= start < text["addr"] + text["size"]):
            continue
        regs = {}
        pc = start
        while pc < end:
            w = word(pc)
            if (w >> 26) == 0x03:                    # JAL
                tgt = (pc & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
                track(word(pc + 4), regs)            # delay slot runs first
                if tgt == bind_addr:
                    binds.append((name, pc, regs.get(A0), regs.get(A1), regs.get(A2)))
                elif tgt == call_addr:
                    calls.append((name, pc, regs.get(A0), regs.get(A1), regs.get(A2),
                                  regs.get(T0), regs.get(T2)))
                regs = {}                            # the call clobbers the temporaries
                pc += 8
                continue
            track(w, regs)
            pc += 4
    return binds, calls


def h(v):
    return "?" if v is None else f"0x{v:X}"


def main():
    ap = argparse.ArgumentParser(description="BindRpc/CallRpc calls")
    ap.add_argument("elf")
    ap.add_argument("functions_csv")
    ap.add_argument("--bind", required=True, help="address of sceSifBindRpc")
    ap.add_argument("--call", required=True, help="address of sceSifCallRpc")
    a = ap.parse_args()

    data, seg, text, funcs = M.load(a.elf, a.functions_csv)
    binds, calls = scan(data, text, funcs, int(a.bind, 16), int(a.call, 16))

    client_sid = {}
    print("== sceSifBindRpc: client struct -> SID")
    for name, pc, cd, sid, mode in binds:
        print(f"  {name} @0x{pc:08X}: cd={h(cd)} sid={h(sid)} mode={h(mode)}")
        if cd is not None and sid is not None:
            client_sid[cd] = sid

    print("\n== sceSifCallRpc: by function")
    by_func = collections.defaultdict(list)
    for name, pc, cd, fno, mode, ssize, rsize in calls:
        by_func[name].append((pc, cd, fno, mode, ssize, rsize))
    for name, rows in by_func.items():
        for pc, cd, fno, mode, ssize, rsize in rows:
            sid = client_sid.get(cd)
            print(f"  {name} @0x{pc:08X}: sid={h(sid)} cd={h(cd)} fno={h(fno)} "
                  f"mode={h(mode)} ssize={h(ssize)} rsize={h(rsize)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
