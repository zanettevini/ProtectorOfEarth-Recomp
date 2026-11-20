# 10: IOP modules (IRX) and `IOPRP300.IMG`

## 1. The question

Which modules does the game load on the IOP? That decides the size of the
native-services work (D2 in [DECISIONS](../DECISIONS.md)).

## 2. How it was measured

Name and version of each IRX taken from its `.iopmod` section (version field
at +0x18, name at +0x1A), SHA-1 of each file, and a search for each module's
name inside `SLUS_216.61`.

## 3. The 15 modules on the disc

| File | Name in the module | Version | Named by the ELF? | What it is |
|---|---|---|---|---|
| `SIO2MAN.IRX` | sio2man | 3.01 | yes | SIO2 bus (base for pad and memory card) |
| `MTAPMAN.IRX` | multitap_manager | 3.10 | yes | multitap |
| `PADMAN.IRX` | padman | 4.22 | yes | pads |
| `MCMAN.IRX` | mcman | 2.30 | yes | memory card (low level) |
| `MCSERV.IRX` | mcserv | 2.10 | yes | memory card (RPC server) |
| `LIBSD.IRX` | Sound_Device_Library | 3.03 | yes | SPU2 access |
| `CDARB.IRX` | CD_Arbiter | 2.03 | yes | **not Sony's, by its name**: arbitrates disc access (probably between audio streaming and loading) |
| `ALASD.IRX` | Argonaut_Audio_Driver | 4.06 | yes | **third-party audio driver** ("Argonaut") |
| `SN_ALASD.IRX` | Argonaut_Audio_Driver | 4.06 | no | same size as `ALASD.IRX`, different content (another build) |
| `SN_CDARB.IRX` | CD_Arbiter | 2.03 | no | **identical** to `CDARB.IRX` (same SHA-1) |
| `CDVDSTM.IRX` | cdvd_st_driver | 2.02 | no | Sony disc streaming |
| `SCREAM.IRX` | Scream_Library | 2.00 | no | Sony sound library |
| `STREAM.IRX` | multi_streamer | 1.02 | no | ADPCM audio streaming |
| `USBD.IRX` | USB_driver | 2.04 | no | USB |
| `USBKB.IRX` | USB_Keyboard_Driver | 2.02 | no | USB keyboard |

"Named by the ELF" = the file name appears as a string in the executable
(the list sits together, near file offset `0x0013BD90`). The unnamed ones
may be leftovers on the disc or be loaded another way; confirm in Ghidra.

Also: `IOPRP300.IMG` (275 KB) is an IOP module replacement image. The game
reboots the IOP with it at boot (string `cdrom0:\IOPRP300.IMG`). With native
services it does not need to be read; with `ps2xIOP` (running the IRX) it
matters.

## 4. What this means

- **Small surface**: 8 named modules. Five are standard, well-documented
  libraries (pad, multitap, memory card, SIO2, libsd).
- The two hard ones are the **game's/engine's own**: `ALASD` (audio driver)
  and `CDARB` (disc arbitration). There is no documentation for them: the
  RPC protocol will have to come from the EE side (the game's
  `sceSifCallRpc` calls) and, if needed, from disassembling the IRX.
- For native services the work is: disc files (`GAME.DIR` / `GAME.WAD`),
  pad, memory card, and the `ALASD`/`CDARB` protocol for audio.

## 5. Open

- ~~Identify the RPC (SIF) server numbers the EE calls and tie each one to a
  module~~: mostly done in [15](15-first-boot.md) s.6.
- Confirm that `SCREAM`, `STREAM`, `CDVDSTM`, `USBD`, `USBKB` are never
  loaded.
- Copy `IOPRP300.IMG` and `GAME.DIR` in the next collection (the script has
  already been updated for that).
