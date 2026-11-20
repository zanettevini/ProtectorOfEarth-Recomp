# 16: `GAME.DIR` and `GAME.WAD`, the game's file system

## 1. The question

Almost all of the game's data is in two files at the disc root: `GAME.DIR`
(30,460 bytes) and `GAME.WAD` (2,456,123,392 bytes) (findings/09 s.3). What
is their layout, and how does the game read them?

## 2. How it was found

- The boot stopped in `FUN_0012b7a0`, which prints "InitFilesystem" and
  opens `cdrom0:\GAME.DIR` (findings/15 s.8). Its disassembly, in the
  comments of the generated C++, gives the read sequence (s.4).
- The structure was checked with a short Python script against the user's
  own copy of `GAME.DIR` (not in the repository). Only the layout and
  counts are recorded here, never the table's contents.

## 3. Layout

`GAME.DIR`, little-endian:

| Offset | Size | What |
|---|---|---|
| +0x00 | 4 | entry count (423 on this disc) |
| +0x04 | 72 × count | entries |

Each 72-byte entry:

| Offset | Size | What |
|---|---|---|
| +0x00 | 64 | file name, zero-padded (no folders) |
| +0x40 | 4 | size in bytes |
| +0x44 | 4 | start, in 2048-byte sectors from the start of `GAME.WAD` |

Checks that the reading is right:

- 4 + 423 × 72 = 30,460, the exact file size.
- Sorted by start sector, every file ends before the next one begins (422 of
  422 gaps), and the last one ends at byte 2,456,123,392: exactly the size
  of `GAME.WAD`.
- The game itself reads the count, then count × 0x48 bytes (s.4).

So `GAME.WAD` is a plain concatenation of files on 2048-byte (CD sector)
boundaries, with no header; `GAME.DIR` is its index.

## 4. How the game reads it (`FUN_0012b7a0`)

1. `sceOpen("cdrom0:\GAME.DIR", 1)` (read-only) in a loop until the result
   is not negative; the fd is kept at `0x002661C0`.
2. `sceRead(fd, 0x002661C4, 4)`: the count. Prints " Files %d".
3. Allocates count × 0x48 bytes (`FUN_001383d0`); the pointer is kept at
   `0x002661C8`. `sceRead` of the whole table, `sceClose`.
4. `sceCdSearchFile(0x002662F0, "\GAME.WAD;1")` in a loop until it returns
   1; the start sector of `GAME.WAD` on the disc is then copied to
   `0x002662E8`. From here on, data is read by disc sector (libcdvd
   `sceCdRead`), not by name: start of `GAME.WAD` + the entry's start
   sector.

## 5. What is inside (by extension only)

| Extension | Files | Note |
|---|---|---|
| `.pss` | 177 | Sony's PS2 MPEG-2 stream container: video for the IPU (findings/09 s.7: `libipu` is linked) |
| `.psm` | 175 | (format TBD) |
| `.bik` | 69 | Bink video names; whether the PS2 build plays them is TBD (they may be leftovers of the shared multi-platform data, findings/03 s.4) |
| `.txt` | 2 | text (one probably holds the debug options TCRF describes, findings/03 s.4) |

This corrects findings/09 s.7 ("no video strings"): the executable has no
`.pss` names because the file names live in `GAME.DIR`.

## 6. What this means for the port

- File access needs no IOP file server: the native side only has to serve
  `GAME.DIR` by name and `GAME.WAD` by sector, which the runtime already
  does through `sceCdSearchFile`/`sceCdRead` on `game/`.
- A future native loader can open `GAME.WAD` directly and seek to
  `start × 2048`, with no disc image.

## 7. Open

- The `.psm` format; what a level loads first.
- Whether the `.bik` files are used on the PS2.
- The `.txt` files' role (debug options).
