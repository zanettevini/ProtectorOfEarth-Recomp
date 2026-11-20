# 18. The memory card library, and the menus

Status: in progress. The game sees a memory card, creates its save, shows the
title screen and the main menu, and loads its first level; the level shows
only a blue screen so far.

## 1. What was seen

With patch 0006 the game drew its first screen: the autosave warning that
no memory card is inserted in slot 1 ([15](15-first-boot.md) s.16). The
game's memory card library (Sony's libmc, linked into the executable) asks
`MCSERV.IRX`, which runs in the IOP interpreter (debt T1) with no card
behind it.

## 2. The library, identified

`tools/analyzer/rpc_calls.py` lists one bind to SID `0x80000400` (the
memory card server) and ten calls to it. The PS2SDK's `libmc.c` keeps two
tables of wire numbers: one for the old MCMAN/MCSERV (`0x70`-`0x80`) and one
for XMCMAN/XMCSERV, where init is `0xFE`, get-info `0x01`, open `0x02`,
close `0x03`, read `0x05`, write `0x06`, chdir `0x0C`, getdir `0x0D`, delete
`0x0F` and format `0x10`. The game's calls use exactly the second set.

| Address | Function | Evidence |
|---|---|---|
| `0x001C2088` | `sceMcInit` | binds `0x80000400`, sends `0xFE`, 12-byte reply |
| `0x001C22C8` | `sceMcOpen` | function `0x02`, 0x414-byte request |
| `0x001C23F0` | `sceMcMkdir` | calls `sceMcOpen(port, slot, name, 0x40)`, stores command `0x0B` |
| `0x001C2428` | `sceMcClose` | function `0x03` |
| `0x001C2598` | `sceMcRead` | function `0x05`; end callback `0x001C24E8` |
| `0x001C26B0` | `sceMcWrite` | function `0x06` |
| `0x001C2828` | `sceMcSync` | no RPC; see below |
| `0x001C2948` | `sceMcGetInfo` | function `0x01`; keeps `a2`, `a3`, `t0` (type, free, format pointers) |
| `0x001C2AC8` | `sceMcGetDir` | function `0x0D`; keeps `a0`-`a3`, `t0`, `t1` |
| `0x001C2CA8` | `sceMcChdir` | function `0x0C`; end callback `0x001C2C18` copies the current directory |
| `0x001C2DE8` | `sceMcFormat` | function `0x10` |
| `0x001C2EB8` | `sceMcDelete` | function `0x0F` |

- Requests that carry a path are 0x414 bytes: the PS2SDK's name parameter
  (port, slot, flags, max entries, a pointer, then a 1024-byte name).
- `sceMcSync(mode, *cmd, *result)`: returns -1 when the library's current
  command word (`0x00227618`) is 0; otherwise polls (mode != 0) or waits on
  the library's semaphore (`0x0022761C`), stores the command in `*cmd`, the
  reply word (`0x00273880`) in `*result`, clears the current command and
  returns 1. Same behaviour as the PS2SDK's `mcSync`.
- Seek, flush, set-file-info, unformat and rename are not in the
  executable (the linker dropped what the game does not call).

## 3. What we changed

The whole family is bound in `ben10_poe.toml` to the runtime's memory card
handlers (`MemoryCard.cpp` in PS2Recomp), which keep each card as a folder on
the PC and report each command through `sceMcSync`, like the library.
Bound together because every call goes through the library's current
command word, its semaphore and its reply buffer.

## 4. What it does now (Linux, Xvfb, keyboard input sent with `xdotool`)

The runtime logs each command (`[MC] ...`):

1. `sceMcInit`, then `GetInfo` on port 0: type 2 (PS2 card), formatted,
   8,192 free clusters.
2. `GetDir '/BASLUS-21661/*'` → -4 (no entry). The game asks whether to
   create a save file. Pressing X (the runtime maps the X key to the Cross
   button):
3. The save folder is created with its icons and a 24,536-byte save file,
   read back right away. The runtime puts card 0 in `mc0/` **next to the
   game's ELF** (here `game/mc0/BASLUS-21661/`): `BASLUS-21661` (the save),
   `icon.sys`, `list.ico`, `copy.ico`, `del.ico`.
4. The autosave notice, then the **title screen** ("Press Start"), the
   **main menu** (New Game, Continue, Options), the difficulty menu, the
   save slot menu, and a **loading screen** with a tip for the first level.
5. While on these screens the game calls `GetInfo` once per frame (it
   watches for the card being removed).

No intro video appeared between the notice and the title screen; whether
the game has one there and skips it is not known yet ([16](16-game-dir-and-wad.md)
s.6 on the Bink videos).

Speed: about 7 frames per second (the generated code is built at `-O0`,
the VU programs run in the interpreter, the GS is drawn with software
OpenGL under Xvfb).

The heap break pointer grows to `0x01FC9000` when the level starts loading,
92 KB below the runtime's limit for this game (`0x01FE0000`,
[15](15-first-boot.md) s.15, KNOWN_ISSUES R19).

A second run, with the save already on the card, goes straight from the
autosave notice to the title screen (about 900 frames). New Game → Normal
→ the first empty slot ("Checking memory card", then the slot list) → the
loading screen. After about 450 frames of loading the prompt changes to
"X Continue". Pressing X gives a **solid blue picture** (the game's
640x448 area, uniform colour) while the frame counter keeps advancing at
about 5 frames per second for at least 3 minutes. Samples of the game
thread at that point are almost all in the IOP interpreter's disc read
(`IopCdvd::Impl::readSectors` → `IopMemory::ownsRamRange`), the same slow
path as [15](15-first-boot.md) s.12: some IOP module (CDARB's own reads for
ALASD, [17](17-cdarb-and-alasd.md)) is reading the disc through the
interpreter. Not known yet whether the blue screen is the level waiting
for that stream or a rendering problem.

The heap break pointer reaches `0x01FD7000` in the level, 36 KB below the
runtime's limit for this game.

## 6. The blue screen: a switch Ghidra split, and a case nobody exported

- The blue picture of s.4 was not the level: the game's main thread had
  stopped. The EE scheduler's table (gdb) showed it **dormant**, and the
  log held one `guest-branch:missing-target` report: an indirect jump
  (`jr`) at `0x00186980` to `0x00186B98`, which is in no function. The
  frame counter kept moving because the VBlank handler still ran.
- `0x00186980` is the `jr` of a 158-case switch (`FUN_00186960`: bound
  check `sltiu $v0, $a2, 0x9E`, table at `0x0023DD40`). Ghidra did not keep
  it whole: 155 of its case blocks became separate entries in the function
  list (the export's pointer pass, `collectDataFunctionPointerEntries`,
  takes a data pointer as a function when a `jr $ra` follows within 8
  words), and the recompiler, which cannot resolve that `jr` as a switch,
  jumps to the case by address. The 3 other cases (`0x00186B10`,
  `0x00186B98`, `0x00186BB4`) branch back into shared code instead of
  returning, so they were left in the gaps.
- A survey of the executable's switches (the same pattern: `sltiu`, then
  `lui`/`addiu` of the table and a `jr` that is not `jr $ra`) finds 91
  tables. The recompiler turned most of them into C++ `switch`es; two
  `jr`s it could not resolve, `0x00186980` and the script interpreter's
  `0x00193BF8` (256 cases, all inside `FUN_00193b00`'s range, where the
  recompiler keeps labels the jump can resume at).
- Wrong turns: first in `CreatePointerTargetFunctions.java` (Ghidra
  step 1). Creating a function at every switch case outside a function
  made 394 functions, 220 of them inside the script interpreter; judging
  by the range of the function holding the `jr` did not work either,
  because at step 1 Ghidra has not split the switch yet (that happens in
  the export, step 2).
- Fix, in our copy of `ExportPS2Functions.java`: a new pass,
  `collectSplitSwitchEntries`, finds the switch tables and, when more than
  half of a table's targets are already entries, adds the rest. It adds 7
  entries: the 3 cases above and 4 of the switch at `0x00123FC0`.

## 7. An empty virtual method

- Next stop: an indirect call (`jalr`) at `0x0018BB30` to `0x002069F0`,
  slot 5 of the vtable at `0x00236430`. The code there is `jr $ra` +
  `nop`: an empty method. `CreatePointerTargetFunctions.java` had
  rejected it: no prologue, and the previous `jr $ra` is followed by two
  stray `addiu $sp, $sp, 16` words, not padding.
- Fix: a new rule c in that script, a whole leaf function of one or two
  instructions (`jr $ra` first, or second after an instruction that is not
  a jump or branch). 16 more functions (4,801 in total).

## 8. The first level: HUD only

- With s.6 and s.7 the game stays in the level. After "X Continue" on the
  loading screen it draws the HUD (the health bar and the Omnitrix) over a
  black picture, still black a few minutes later
  (`docs/images/screenshots/05-first-level-hud-only.png`).
- The frame counter slows to about 1 frame per second. Samples of the game
  thread: the runtime's software GS rasteriser
  (`GSCpuBackend::DrawTriangle`, texture sampling, alpha test) and, again,
  the IOP interpreter's disc reads. So triangles are being drawn; why the
  world does not show is not known yet (wrong buffer, wrong blending,
  geometry from the VU programs, which run interpreted, T2).

## 9. Open

- Where saves should live: next to the disc files is not a good place for
  players' data; a per-user folder is the plan (KNOWN_ISSUES R21).
- The black world in the first level (s.8), and the speed there.
- The heap: 36 KB left under `0x01FE0000` in the first level (R19).
- The memory card is still the runtime's generic handler (status
  "emulated" on the README board); the native goal is the port's own save
  system.
