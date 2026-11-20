# 15: first boot: binding the Sony libraries, one blocker at a time

## 1. The question

Master prompt phase 7: run the native executable and see how far the boot
gets, diagnosing one failure at a time without masking it.

## 2. How it was run

- Linux, `-O0` build (findings/14), under Xvfb (a virtual X display) with
  Mesa's software OpenGL, because the PS2Recomp runtime opens a raylib
  window. No sound device (raylib reports it and carries on).
- Only these disc files were available on the test machine:
  `SLUS_216.61`, `SYSTEM.CNF` and `MODULES/*.IRX`. `GAME.DIR`, `GAME.WAD`
  and `IOPRP300.IMG` were not (KNOWN_ISSUES B8).
- Each run: 60-90 s with `timeout -s TERM`, then `gdb -p <pid> -batch -ex
  "thread apply all bt"`, sampled 3-4 times. The generated functions are
  named after their PS2 address (`entry_001c7dc0_0x1c7dc0`), so a native
  stack shows where the guest is. The disassembly comments that `ps2_recomp`
  writes into the generated C++ show what the loop does.

Wrong turns:

- The first log showed nothing after "Starting execution": stdout was
  block-buffered into the file and lost when the process was stopped.
  `stdbuf -o0 -e0` fixed it.
- The first gdb attach went to the `timeout` process, not the game:
  `pgrep -x ben10_poe` picks the right PID.
- The runtime creates memory card folders (`mc0/`, `mc1/`) in the current
  directory: runs now start from `user/run/` (gitignored).

## 3. Blocker 1: `sceSifInitRpc` waits for an IOP that never answers

- The game thread spun in `0x001C7DC0`-`0x001C7DF0`, inside `FUN_001c7c68`.
- That function registers SIF command handlers `0x80000008`, `9`, `A` and
  `C`, reads SIF register `0x80000002`, sends a command and loops on a
  function that reads `0x00274A40 + 4*n` until it is non-zero, then writes 1
  to the register. That is the PS2SDK's `SifInitRpc`: handlers for RPC end,
  bind, call and rdata; `SIF_SYSREG_RPCINIT`; `while (!SifGetSreg(0))`.
- Cause: the executable has no symbols, so Ghidra named only the 42 syscall
  stubs (findings/12 s.3). The Sony library functions ran as ordinary game
  code. The PS2Recomp runtime does not emulate the IOP at hardware level: it
  answers these libraries **by name** (`sceSifInitRpc`, `sceSifCallRpc`...).
  With no names, no handler was bound and the game waited for a real IOP.
- Fix: bind by address, `"sceSifInitRpc@0x001C7C68"` in `ben10_poe.toml`
  `stubs`, the form PS2Recomp documents for stripped games.

## 4. Identifying a library family with evidence

Two new tools:

- `tools/analyzer/function_map.py`: per function, its direct calls (JAL) and
  the strings it builds with LUI + ADDIU/ORI.
- `tools/analyzer/rpc_calls.py`: every `sceSifBindRpc` call (client
  structure → RPC server id, SID) and every `sceSifCallRpc` call (client,
  function number, send size, receive size).

The Sony libraries leave names behind: `FUN_001c82b0` creates a semaphore
called "SceSifrpcBind", `FUN_001c8490` one called "SceSifrpcCall". The rest
follows from structure and calls, compared with the open-source PS2SDK
(`ee/rpc/cdvd/src`, commit `cf9557d`). Every binding's evidence is the
comment next to it in `ben10_poe.toml`.

| Library | Bound | Key evidence |
|---|---|---|
| libsif | `sceSifInitRpc`, `sceSifInitCmd`, `sceSifExitRpc`, `sceSifExitCmd`, `sceSifBindRpc`, `sceSifCallRpc`, `sceSifCheckStatRpc`, `sceSifGetSreg`, `sceSifAddCmdHandler`, `sceSifSendCmd` | semaphore names; the InitRpc sequence; the 0x00229258 "initialised" flag shared by InitRpc/ExitRpc |
| libcdvd | `sceCdInit`, `sceCdSearchFile`, `sceCdDiskReady`, `sceCdRead`, `sceCdSeek`, `sceCdNcmdDiskReady`, `sceCdReadClock`, `sceCdGetDiskType`, `sceCdMmode`, `sceCdSync`, `sceCdSyncS` | SIDs 0x80000592/593/595/597/59A; N-command 1 with 24 bytes (READ), 5 (SEEK), 0xE (DISKREADY); S-command 1 with 16 bytes (READCLOCK), 3 (GETDISKTYPE), 0x22 (MMODE); strings "Libcdvd bind err %d CD_Init %d", "N cmd wait", "S cmd wait" |
| IOP reboot | `sceSifRebootIop`, `sceSifSyncIop` | caller passes "cdrom0:\IOPRP300.IMG;1"; SIF register 4 bit 0x40000 (SMFLAG / BOOTEND) |

The RPC map also shows what comes next: memory card (SID `0x80000400`),
controllers (`0x80000100`/`101`), module loading (`0x80000006`), IOP heap
(`0x80000003`), file I/O (`0x80000001`), and **two custom servers of the
game**, `0x01212121` and `0x01212122` (probably the engine's `ALASD` audio
driver and `CDARB`: to confirm).

## 5. Blocker 2: `sceCdInit` retries forever

- With libsif bound, the log shows `[SifInitRpc] Initialized` and the game
  thread moved to `0x001C3F20`, in `FUN_001c3d98`.
- It binds SID `0x80000592` (the PS2SDK's `CD_SERVER_INIT`) and, while the
  returned server is 0, spins a delay loop and binds again.
- Cause: the runtime publishes an RPC server only when an IOP route serves
  it (`PS2IopTransport::canBindRpc`). The CD servers live in the console's
  IOP ROM, not on the disc, so no IRX registers them; the runtime answers
  libcdvd by name instead.
- Fix: bind the whole libcdvd family at once. Binding only `sceCdRead`
  would leave the game's own `sceCdSync` waiting on an internal flag the
  runtime never sets.
- The runtime reads the disc from the ELF's folder (`IoPaths::cdRoot`), so
  the disc files belong in `game/`.

## 6. Blocker 3: the IOP reboot

- Next stop: `0x00142E38`, in the game's `FUN_00142dc0`: InitRpc, CdInit,
  CdMmode, then `while (!f("cdrom0:\IOPRP300.IMG;1"))` and
  `while (!g())`, then everything again. That is the PS2SDK's IOP reset
  sequence: `FUN_001ca660` = `sceSifRebootIop`, `FUN_001ca610` =
  `sceSifSyncIop` (reads SIF register 4 and tests bit `0x40000`).
- Fix: bind both. The boot moves on.

## 7. Where it stops now

- The game thread waits at `0x001C8E70`, in `FUN_001c8de0`, which binds SID
  `0x80000001`: the IOP file I/O server (the PS2SDK's fileio, used by
  `sceOpen`/`sceRead`...). This is the next family to identify and bind.
- After that, reading `GAME.DIR`/`GAME.WAD` needs the real files in
  `game/`, which this test machine does not have (B8).

## 8. What was not checked

- Whether each runtime handler behaves like the Sony function it replaces
  for this game (the boot moving past a loop is the only check so far).
- `isceSifSendCmd` (`0x001C7A30`) stays game code: the runtime has no
  handler with that name.
- The `entry_*` labels inside bound functions: the bound address replaces
  the function start; an internal label reached by a jump from outside the
  function would still run the old code. None was seen in the samples.
