# Credits

## Project

- **Project maintainer**: direction, testing on the real game.
- **Claude** (Anthropic), via Claude Code: AI assistant that wrote most of
  the code, tools and documentation.

## The game

*Ben 10: Protector of Earth* (PlayStation 2, 2007) was developed by High
Voltage Software and published by D3 Publisher. *Ben 10* and its characters
are trademarks of Cartoon Network. This project is unofficial and is not
affiliated with or endorsed by any of them. The screenshots in
[docs/images/screenshots/](docs/images/screenshots/README.md) show the
game's own content as run by this port; that content is their work, not
this project's.

## Software the port is built on

| Project | Used for | License |
|---|---|---|
| [PS2Recomp](https://github.com/ran-j/PS2Recomp) (ran-j and contributors) | the recompiler and runtime (submodule `thirdparty/PS2Recomp`); `tools/ghidra_scripts/ExportPS2Functions.java` is a copy of its script, with one pass added (docs/findings/18 s.6) | GPL-3.0 |
| [GoogleTest](https://github.com/google/googletest) | automated tests (fetched by CMake) | BSD-3-Clause |
| PS2Recomp's dependencies (ELFIO, toml11, fmt, libdwarf, rabbitizer, raylib...) | fetched by PS2Recomp's CMake | each project's own |

## References (read, not copied)

| Project | Used for |
|---|---|
| [PCSX2](https://github.com/PCSX2/pcsx2) | PS2 reference behaviour: the tests' expected values cite its interpreter (GPL-3.0) |
| [PS2SDK](https://github.com/ps2dev/ps2sdk) | the open-source libcdvd/libsif, used to recognise the Sony library functions in the stripped executable (docs/findings/15) |
| [Mind over Recomp](https://github.com/Maxilure/MindOver-Recomp) (Maxilure) | the repository organisation, the AGENTS.md rules and the `native_progress.py` board idea (GPL-3.0) |
| [Gitaroo Man Native](https://github.com/erinxocon/gitaroo-man-native), [BT2 Recomp](https://github.com/oric212/BT2-Recomp), [BT3-Recomp](https://github.com/z3xox/BT3-Recomp) | other PS2 ports built with PS2Recomp (docs/findings/02) |
| [Ghidra](https://github.com/NationalSecurityAgency/ghidra) and [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) | executable analysis (Apache-2.0) |
| [The Cutting Room Floor](https://tcrf.net/) | the game's debug options (docs/findings/03) |
