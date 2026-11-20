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
  `GAME.DIR` and `IOPRP300.IMG` were added before the run in s.8.
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

## 7. Blocker 4: the file I/O server

- The game thread waited at `0x001C8E70`, in `FUN_001c8de0`, which binds SID
  `0x80000001` (the IOP file I/O server) and spins while the bind returns
  server 0. Same cause as blocker 2: that server lives in the IOP ROM.
- `function_map.py --string SceStdio` found the library: Sony's EE-side file
  I/O, where each call creates a semaphore named after itself:
  `FUN_001c90b0` "SceStdioOpenSema", `FUN_001c9340` "SceStdioCloseSema",
  `FUN_001c94c0` "SceStdioReadSema", `FUN_001c9730` "SceStdioWriteSema".
  All four go through one RPC wrapper (`FUN_001c87e8`); `FUN_001c8de0` (the
  server bind) is only called from the open function; nothing else in the
  game calls the wrapper or the library's fd table (`FUN_001c88d8`,
  `FUN_001c8760`). No lseek or ioctl is linked.
- Arguments, from the generated code's disassembly: open keeps `a0` (path)
  and `a1` (flags) and saves the variadic registers; read and write take
  `a0` = fd, `a1` = buffer, `a2` = size. That is the runtime's `sceOpen` /
  `sceRead` / `sceWrite` / `sceClose` signature.
- Fix: bind the four together (`ben10_poe.toml`). Mixing them would hand
  runtime file descriptors to the library's own table.

## 8. Blocker 5: the game's file system (`GAME.DIR`, `GAME.WAD`)

- Next stop: `FUN_0012b7a0` (it prints "InitFilesystem"). It opens
  `cdrom0:\GAME.DIR` in a loop until the open succeeds, reads a 4-byte
  count, allocates count × 72 bytes, reads the table, then calls
  `sceCdSearchFile("\GAME.WAD;1")` in a loop until it returns 1. The
  layout of both files is in [16](16-game-dir-and-wad.md).
- With `GAME.DIR` copied into `game/`, the open and both reads succeed (the
  loop moved on) and the game waits on `GAME.WAD`, which the test machine
  still does not have (B8).

- `GAME.WAD` (2,456,123,392 bytes) was then copied from the maintainer's
  disc in 350 MiB parts and joined; its SHA-1 matches the one computed on
  the original. The search succeeds (the runtime reports the file at disc
  sector `0x100000`) and the boot moves on.

## 9. Blocker 6: IOP heap and IOP module loading

- Next stop: `0x001C9A70`, in `FUN_001c9a50`, binding SID `0x80000003`,
  the IOP heap server (IOP ROM again).
- `rpc_calls.py` shows one bind and two calls on the same client struct
  (`0x00277B40`): function 1 and function 2, 4 bytes each way. That is the
  PS2SDK's iopheap: `SifInitIopHeap`, `SifAllocIopHeap`, `SifFreeIopHeap`.
- The loader client (`0x00277F00`, SID `0x80000006`) is next in the same
  area. Its shared worker `FUN_001ca248` takes the function number as an
  argument; `FUN_001ca470` calls it with 0 (`LF_F_MOD_LOAD`), which is
  `SifLoadModule(path, arg_len, args)`. `FUN_001c9d80` sends function 6
  (`LF_F_MOD_BUF_LOAD`); its wrapper `FUN_001ca220` is
  `SifLoadModuleBuffer`. Function 7 (`FUN_001c9f88`, stop) and 8
  (`FUN_001ca190`, unload) have no runtime handler and stay game code;
  their only caller is `FUN_001b2af8`. If it runs, it will spin in the
  loader bind: a known gap.
- Fix: bind the heap family and the two load entry points.
- Result: the game loads exactly the 8 modules [10](10-iop-modules.md)
  found named in the executable, in this order: SIO2MAN, MTAPMAN, PADMAN,
  MCMAN, MCSERV, LIBSD, CDARB, ALASD. They run in PS2Recomp's IOP
  interpreter (temporary debt T1). Then the video mode is set
  (`GsSetCrt`: interlaced, NTSC, frame mode) and the GS interrupt mask
  written (`GsPutIMR`).

## 10. Blocker 7: a function the function list did not have

- The runtime stopped with `guest-branch:missing-target`: an indirect call
  (`JALR` at `0x001A08A0`) to `0x0014B820`, which was in no function of the
  Ghidra list.
- `0x0014B820` starts with a stack-frame prologue and is referenced only by
  a word in `.data` (`0x00236124`): a function reached only through a
  pointer (a virtual table or callback table). Ghidra's auto-analysis
  follows calls, so it missed it, like the script API functions
  ([12](12-ghidra-analysis.md) s.4).
- New Ghidra script `tools/ghidra_scripts/CreatePointerTargetFunctions.java`,
  run by `tools/ghidra_analyze.sh` / `.ps1` after `ApplyScriptApiNames.java`:
  every word in a loaded data block that points into code outside any
  function becomes a function if the target starts with
  `addiu $sp, $sp, -N` or directly follows a `jr $ra` and its delay slot
  (allowing nop padding). Of 3,673 distinct pointer targets, 2,863 were
  already inside functions, 364 functions were created, 446 were rejected
  (mostly switch-table targets and numbers that happen to look like code
  addresses; they are listed in the Ghidra log).
- Wrong turns: the first version read every memory block, including
  non-loaded ones, and stopped on an unreadable address; the second
  accepted only prologues and created 13 functions, missing 351 leaf
  functions (small getters with no stack frame).
- Result: 4,784 functions (was 4,420); the recompiler still reports 0
  unsupported instructions and 0 errors, and the missing-target stop is
  gone.

## 11. Where it stops now: the pad library

- The game thread loops in game code `FUN_00123180`, calling
  `FUN_001c1830` again and again. `FUN_001c1830(port, slot)` reads byte
  0x70 of the pad's DMA frame (6 = stable) and byte 0x71 (2 = request
  busy) and returns 5 in that case: that is `scePadGetState`.
  `FUN_001c1660` copies the newest of the two 128-byte frames (compares
  their counters at +0x58 and +0xD8) after `InvalidDCache`
  (`FUN_001c5620`, with the cache loop `FUN_001c5578`).
- The PADMAN module runs in the IOP interpreter, but the frame it should
  write over SIF DMA never says "stable", so the game waits.
- Next: identify the libpad family (`FUN_001c10c8` binds SIDs
  `0x80000100`/`0x80000101` and prints the libpad/padman version
  mismatch message; `FUN_001c1478` is the port open, by its error strings;
  every RPC is function 1 with a 128-byte command buffer, so the command
  number in the buffer tells the functions apart) and bind it whole to the
  runtime's `scePad*` handlers.

## 12. What was not checked

- Whether each runtime handler behaves like the Sony function it replaces
  for this game (the boot moving past a loop is the only check so far).
- `isceSifSendCmd` (`0x001C7A30`) stays game code: the runtime has no
  handler with that name.
- The `entry_*` labels inside bound functions: the bound address replaces
  the function start; an internal label reached by a jump from outside the
  function would still run the old code. None was seen in the samples.
