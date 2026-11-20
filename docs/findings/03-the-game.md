# 03: the game, its versions and what is known

## 1. The question

Which version of the game to use, and what is already known about its files?

## 2. How it was studied

Public databases (PSXDataCenter, Wikipedia, TCRF). At first the disc had not
been analysed; section 5 lists what [09](09-executable.md) later confirmed.

## 3. PS2 versions

| Serial | Region | Note |
|---|---|---|
| SLUS-21661 | NTSC-U (USA) | released October 2007 |
| SLES-54951 | PAL (Europe) | |
| SLES-54952 | PAL (Europe) | listed by the PCSX2 Wiki as the Italian/Spanish version; Redump lists an "EnFrDeEsIt" version |

Choice: **SLUS-21661**. Reasons: NTSC at 60 Hz (PAL is usually 50 Hz), it is
the version TCRF gives cheat/debug codes for, and the reference ports also
use SLUS versions. The ELF was expected to be `SLUS_216.61` (the US naming
pattern), to be confirmed in the disc's `SYSTEM.CNF`.

One secondary source gives `SLUS-21699` for the game; PSXDataCenter and
sale listings use `SLUS-21661`. 21661 was treated as right until the disc
was seen (it was).

## 4. What is known about the game

- Developer: High Voltage Software; publisher: D3 Publisher.
- Engine: **Quantum3**, High Voltage's own. Its formats have no public
  documentation.
- The PS2, Wii and PSP versions are nearly identical and made by the same
  team. That may help: symbols or strings of the Wii/PSP builds could name
  functions of the PS2 build (to investigate, without copying anything into
  the repository).
- Local co-op for 2 players.
- According to TCRF, a text file inside the game's archive lists debug
  options (ON/OFF): debug cheats, debug text, fly mode, level select, demo
  level select, debug sound. Turning them on means re-importing the file into
  the archive and building another disc image.
- The debug level select shows unfinished versions of some levels.

Why this matters for the port:

- The level select and fly mode would be valuable test tools (going straight
  to a level, like MoR's `--load_save`). Since **we never modify the original
  game**, the right way is to find the function that reads those options and
  answer it with an override, behind a developer option.
- The archive format has to be understood for phase 4 (file access).

## 5. Confirmed on the disc ([09](09-executable.md))

- Serial SLUS-21661, ELF `SLUS_216.61`, revision `VER = 1.01`, NTSC.
- The disc has `GAME.DIR` (30 KB, probably the index) and `GAME.WAD`
  (2.46 GB, the data).
- The debug options TCRF lists exist as strings in the executable.
- The code is shared with the Wii version (an `ass_Wii...` string).

## 6. Open

- Compare the ISO's SHA-1 with Redump.
- ~~List the disc's IRX modules~~: [10](10-iop-modules.md).
- ~~Runtime-loaded code (overlays)~~: none found ([09](09-executable.md) s.8).
- ~~Symbols or `.mdebug`~~: none (stripped; `.mdebug` is empty).
- Identify middleware (video, audio) by module names and strings
  (audio: [10](10-iop-modules.md)).
- The `GAME.DIR` / `GAME.WAD` format.
