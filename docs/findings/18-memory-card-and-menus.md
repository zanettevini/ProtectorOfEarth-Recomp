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

## 9. Black models: VU1 MAX flushed integer colours

How it was found (gdb on the running level):

- A tally of 6,000 calls to the GS rasteriser (`GSCpuBackend::DrawPrimitive`)
  showed that almost all draws are textured triangles into the frame
  buffer at page 80 (640x448, 32-bit), Z buffer at page 220 (Z24, test
  GEQUAL). Their vertex colour was **RGBA = 0**, while untextured
  triangles had sane colours (e.g. 112, 105, 80, 128). With texture
  function MODULATE (`tfx = 0`) that gives black; alpha 0 then fails the
  alpha test, whose fail mode (`AFAIL = FB_ONLY`) still writes the black
  colour. The display buffer is page 0, 16-bit (`DISPFB1/2`, `PMODE
  0x8027`); the game copies page 80 into it with a textured sprite.
- The black triangles come from VU1 (`VU1Interpreter::finishXgkick` →
  `GS::processGIFPacket`); their GIF tags are `PRIM 0x5C` (triangles,
  Gouraud, textured, blended) with registers ST, RGBAQ, XYZF2.
- The VIF1 streams (decoded in gdb) unpack the vertex colours as V4-8
  unsigned, with `CL = 3, WL = 1`; that part is right.
- VU1 micro memory dumped at an XGKICK and read with the new
  `tools/analyzer/vu_disasm.py`: the unlit textured program (around
  `0x0400`-`0x0640`) loads the colour (`LQ vf07, 2(vi02)`), converts only
  the alpha (`ITOF0.w`, `MUL.w`, `FTOI0.w`) and copies the vector into the
  packet with **`MAX.xyzw vf09, vf07, vf07`**. R, G, B are still integers
  (0-255), whose bit patterns are denormal floats.
- The runtime's VU1 interpreter passed MAX/MINI operands through its
  denormal flush (`normalizeOperand`), so the copy turned R, G, B into 0.
  MAX/MINI are comparisons that return one operand unchanged; the game's
  use of MAX as a move depends on that.

Fix: PS2Recomp patch `0007-vu-max-mini-raw-bits.patch`: MAX/MINI (all six
forms) compare raw bit patterns in sign-magnitude order and return the
chosen operand's bits. New PS2Recomp test (fails before, passes after);
`ps2x_tests` 447/447, project `ctest` 35/35.

Result: 324 of 400 VU1 packets sampled now carry colours (the lit
program's colours were checked in gdb: about 100-117 per channel before
`FTOI0`). On screen, one piece of textured ground appears in the lower left;
the rest of the picture is still black (s.10).

## 10. Still black after the fix: what one frame does

Measured in gdb on the running level (the frame buffer read from the GS
backend's VRAM with the CT32 page/block/column layout):

- Textures are fine at draw time: every texture checked had data in VRAM
  when the triangle using it was drawn. (Read at the end of the frame they
  look empty: the game copies the finished frame, 640x448, into pages
  371-510, over its texture area, and re-uploads its textures every frame.
  Wrong turn: that end-of-frame read first suggested missing uploads.)
- Z is not the problem: the Z buffer at page 220 stays near 0 and the 3D
  test is GEQUAL.
- Following two pixels of the frame buffer (page 80) through one frame
  (about 10,600 draws):
  1. the sky clear paints them blue (`0x00FF8A40`);
  2. the first group of textured triangles draws the scene correctly
     (e.g. `0x7F648694`, `0x7FD2FAE0`), up to about draw 4,500;
  3. an alpha-only pass (sprites with `FBMSK = 0x00FFFFFF`) clears alpha;
  4. a second group of textured triangles then covers both pixels with
     black (`0x7F000000`): **giant triangles** spanning more than the screen
     (vertices such as (-552, -1552), (645, 551), (412, 1720), (1688, -70)
     after the 1728/1824 offset) with depths near 2^24 (e.g. 16,649,703),
     i.e. vertices behind the camera whose depth wrapped;
  5. the alpha pass again, the HUD, and the copy to the 16-bit display
     buffer.
- So the scene is drawn, then overdrawn by triangles that should have been
  clipped or culled. The game's VU1 programs clip with `CLIP`/`FCGET`/
  `FCAND` and a clipping routine (around `0x2000`-`0x24B0`, s.9), and mark
  culled vertices with the ADC bit (the GS front end honours ADC). Most
  likely the VU1 interpreter's clip flags or the clipping routine go wrong
  (debt T2). Not proven yet.
- First check, on 8 triangles of the program at `0x2028` (gdb on
  `VU1Interpreter::execLower`): the three `CLIPw.xyz` results read back by
  `FCGET` match the values computed by hand from the vertices and `w`
  (`0x01`, `0x41`, `0x41`: x outside on the + side, shifted by 6 bits per
  judgement), with the 4-instruction distance the program relies on. In
  that program only the z+ judgement (mask 16) sends a triangle to the
  clipping routine at `0x2300`; x/y judgements go to `0x24B0`. So the flags
  themselves look right in this sample; the giant triangles' origin (which
  program, which vertices) is the next thing to pin down.

## 11. The visibility test and VF0 (patch 0008)

The first suspect for the giant triangles was the EE test that decides
whether a model is drawn with a clipping VU1 program or a faster one that
does not clip. In `FUN_0010dd30` (the model draw function):

- The model's bounding sphere (`obj+0xE0`, radius `obj+0xEC` times the scale
  `obj+0xFC`) is moved to world space with the object matrix (`obj+0x20`).
- `FUN_0019e1d8` tests it against 4 planes at `camera+0x160`: 0 = outside
  (culled), 2 = not outside. If 2, `FUN_0019e238` tests it against the 4
  planes at `camera+0x100`: 1 = crosses a plane, 2 = fully inside. Both
  transform the centre with VU0 macro instructions
  (`vmulax`/`vmadday`/`vmaddaz`/`vmaddw ..., $vf0w`), take the minimum
  distance with `vminiy/z/w` and compare it with the radius (`c.lt.s`).
- "Fully inside" selects the program without clipping (MSCAL offset 0);
  otherwise the clipping variant (offset 2). An object flag
  (`obj+0xC & 0x400000`) forces the clipping variant.

A gdb script read the planes and the sphere at each call, recomputed the
result and compared it with `$v0` on return. About 1 call in 100 disagreed,
always in one guest thread, whose `vu0_vf[0]` was `(1, 0, -16.59, 0)`
instead of `(0, 0, 0, 1)`: the `vmaddw ..., $vf0w` dropped the planes'
distance terms. The game's compiler emits `qmtc2 $reg, $vf0` as a scratch
move (120 sites); the hardware ignores writes to VF0, the translation did
not. Patch 0008 discards them (and any macro-mode write to VF0 or VI0).
After it, 3,000 sampled calls matched. The giant triangles stayed: the
classification was right, the problem was elsewhere.

## 12. The giant triangles: the game's software skinning (patches 0009, 0010)

- **Which object.** Forcing every call of `FUN_0019e238` to answer "crosses
  a plane" (gdb, `$v0 = 1` on return) made the whole scene appear: the
  giant triangles came from objects drawn without clipping. Forcing it for
  one object at a time found the RV (`Col RV`, object `0x01F1B440`); its
  VU1 matrix (`vf11`-`vf14` at the giant draw) matched that object's world
  matrix times the camera's view-projection.
- **Which data.** The model has 7 records of 32 bytes (material, DMA
  pointer, flags, two LOD floats; header `+0x20`/`+0x24` give the range
  drawn by `FUN_0010dd30`). Record byte `+0xA` is a bone index: `0xFF` uses
  the object matrix, otherwise the matrix at scratchpad
  `0x70000000 + index * 64`. A record is drawn when its first float times
  the camera's LOD scale (`camera+0x344`) is larger than the distance. The
  vertices of record 0 (`0xFF`, 32 vertices) were far outside the model's
  bounding sphere (radius 4.4, vertices up to 72 units away), so the
  no-clip program put some of them behind the camera.
- **Who wrote them.** The model data is fine as loaded: a Python decoder of
  the game's LZ format (`FUN_001973e8`; token byte b: `0x00`-`0x3F` = a
  literal run of b + 1 bytes; `0x40`-`0x7F` = b - 0x3E copies of the next
  byte; `0x80`-`0xFF` = with the next byte, a 16-bit v: copy
  `(v & 0xF) + 2` bytes from `output + (signed 12-bit v >> 4) - 1`)
  matched the bytes in memory, and a
  hardware watchpoint saw the decompressor write them. A second write came
  later from `FUN_0015f198`, called by the draw function: the game skins
  this record **on the EE and VU0 every frame** and writes the result back
  into the DMA packet.
- **Bug 1, PROT3W.** `FUN_0015f198` packs two skinned vertices into 24
  bytes with `prot3w` + `pextuw`. PROT3W gave `{w1, w2, w3, w0}` instead of
  `{w1, w2, w0, w3}`, so every second vertex got its w (0) as x. PEXEW,
  PEXCW, PEXEH and PEXCH had similar wrong shuffles (not used here). Patch
  0009 fixes all five (checked against the EE manual's definitions and
  QEMU's tx79 implementation; PREVH not changed, unverified).
- **Bug 2, VU0 data memory.** The bone matrices go to VU0 data memory with
  `vsqi vfN, (vi1++)`; three VU0 microprograms (`VCALLMS 0x0`, `0x38`,
  `0x140`: load bones, blend position and normal with up to three bones
  and 12-bit weights) read them back with `LQ`. The translation of the
  macro-mode VU memory instructions wrote EE RAM at `vi * 16` (kernel RAM at
  `0x0`-`0x3FF`) and took the stored register of VSQI/VSQD from the wrong
  field (all four stores wrote `vf1`). A gdb trace of the microprograms
  showed the "bone matrices" were zeros or vertex data. Patch 0010 points
  VLQI/VSQI/VLQD/VSQD/VILWR/VISWR at VU0 data memory (EE `0x11004000`) with
  the right fields.
- **Result.** After the three patches the record's vertices are inside the
  bounding sphere (largest distance 4.40 for radius 4.41), no giant
  triangle is drawn, and the first level renders: Ben, the RV, the fence,
  the shop, the rocks (screenshot `06-first-level.png`). The first frames
  after the level loads are still black for a while (the camera intro or a
  fade, not yet checked).
- **Wrong turns.** Before the skinning: XGKICK timing, textures, Z, VU1 clip
  flags (s.10), the visibility test (s.11), the LZ decompressor and the
  LOD selection were all checked and found right.
- **Note on VU0 micro mode.** `VCALLMS` runs the microprogram to the end
  at once. The skinning loop overlaps the EE and VU0 (`qmfc2.ni` right
  after `vcallms`), but each program here only reads registers the EE wrote
  before the call and the EE only reads results of the previous program,
  so running it at once gives the hardware's results. Other code may not be
  so lucky (KNOWN_ISSUES R22).

## 13. Moving in the level: WASD and the left stick (patch 0011)

Holding the arrow keys or W did not move Ben. A gdb read of the 8 first
bytes the game gets from `scePadRead` (bound to the runtime, findings/15)
while the keys were held: `00 73 EF FF 80 80 80 80` for both W and Up. Mode
`0x73` (DualShock), the D-pad up bit set, and both sticks centred
(`0x80`): the game moves the player only with the left stick. The runtime's
pad backend reported analog mode but mapped W/A/S/D to the D-pad, like the
arrows. Patch 0011 maps W/A/S/D to the left stick at full tilt; with it,
holding W makes Ben walk towards the RV and the camera follows (screenshot
`07-first-level-walking.png`). Pressure values, the right stick and a
partial tilt from the keyboard are not handled.

## 14. Where the memory cards live: a per-user folder (src/saves/save_location.cpp)

- Before: the runtime puts card 0 in `mc0/` next to the game's ELF (s.4),
  so in `game/mc0/`, and card 1 in `game/mc1/`.
- Now a game override sets the runtime's memory card root
  (`IoPaths::mcRoot`) before the game starts. `PS2Runtime::loadELF` sets
  the I/O paths from the ELF and then applies the overrides; the memory
  card stubs and the IOP's file system read the paths at every call, and
  card 1's folder is derived from card 0's (`mc0` -> sibling `mc1`).
- The folder: `BEN10_SAVE_DIR` if set; on Windows
  `%APPDATA%\ProtectorOfEarth-Recomp`; elsewhere
  `$XDG_DATA_HOME/protectorofearth-recomp` or
  `~/.local/share/protectorofearth-recomp`. The cards are `mc0/` and
  `mc1/` inside it.
- First run: a card folder missing in the new place but present next to
  the ELF is **copied** (AGENTS.md s.6: saves are copied, the old data is
  kept). If the folder cannot be created or the copy fails, the cards stay
  next to the ELF and the log says so (`[saves] ...`).
- Tested on Linux: with `BEN10_SAVE_DIR` pointing to an empty test folder,
  the first run copied both cards, the game found the save (`GetDir`
  7 entries, the 24,536-byte save read) and went straight to the title
  screen; the second run copied nothing and used the same folder. Without
  `BEN10_SAVE_DIR`, the cards went to `~/.local/share/protectorofearth-recomp/`.
  The old `game/mc0/` was left unchanged. **Untested on Windows** (the
  `%APPDATA%` branch).

## 15. Open

- The first level renders (s.12) and Ben walks (s.13), but it runs at about
  2-3 frames per second (frames counted by the game over 30 s, -O0 build,
  software OpenGL under Xvfb); the rest of the level is not played yet.
- The heap: 36 KB left under `0x01FE0000` in the first level (R19).
- The memory card is still the runtime's generic handler (status
  "emulated" on the README board); the native goal is the port's own save
  system.
