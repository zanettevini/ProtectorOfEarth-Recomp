# Screenshots

Pictures captured from this port while it runs, to document its progress.
They are listed here in the order they were taken.

| File | What it shows | Captured |
|---|---|---|
| `01-first-screen-memory-card.png` | The first screen the port drew: the game's autosave warning that no memory card is inserted in slot 1 ([findings/15](../../findings/15-first-boot.md) s.16) | Linux, Xvfb with Mesa software OpenGL, 1280x1024 window |
| `02-title-screen.png` | The title screen, once the memory card was found ([findings/18](../../findings/18-memory-card-and-menus.md) s.4) | same; the game's 640x448 area cut out of the window |
| `03-main-menu.png` | The main menu | same |
| `04-level-loading.png` | The loading screen of the first level, with a tip | same |
| `05-first-level-hud-only.png` | The first level: the HUD is drawn, the 3D world is still black ([findings/18](../../findings/18-memory-card-and-menus.md) s.8) | same |
| `06-first-level.png` | The first level rendered: Ben, the RV, the fence and the rocks, after the VF0, MMI and VU0 memory fixes ([findings/18](../../findings/18-memory-card-and-menus.md) s.12) | same |
| `07-first-level-walking.png` | Ben walking towards the RV, moved with the keyboard (W, the left stick) ([findings/18](../../findings/18-memory-card-and-menus.md) s.13) | same |
| `08-windows-tutorial.png` | The first level's Omnitrix tutorial, in the first Windows (MSVC Release) build ([findings/19](../../findings/19-windows-build-and-speed.md) s.1) | Windows, the game's 638x448 area cut out of a desktop screenshot |
| `09-first-level-combo.png` | Ben attacking in the first level (the HUD shows the combo name), after the pad fix ([findings/20](../../findings/20-vu1-census-and-gs-thread.md) s.9) | Linux, Xvfb, the game's 640x448 area cut out of the window |

## Whose work this is

What these pictures show (text, fonts, artwork, characters) is the content
of *Ben 10: Protector of Earth*, developed by High Voltage Software and
published by D3 Publisher in 2007. *Ben 10* and its characters are
trademarks of Cartoon Network. It is **not** the work of this project, of
its maintainer or of its contributors; the project is not affiliated with
or endorsed by any of them.

The screenshots are here only to show what the port can run so far,
published with the maintainer's approval (decision D13). They are not a
substitute for the game: no game files are included in this repository,
and the port is built from a copy of the disc you own.

## Rules for adding one

- Only pictures captured from this port (not from an emulator or another
  source), to document a milestone or a finding.
- Credit stays as above; add a row to the table with what it shows, where
  it comes from and how it was captured.
- Keep them few and small: they are evidence, not a gallery.
