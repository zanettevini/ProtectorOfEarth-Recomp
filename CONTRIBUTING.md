# Contributing to Protector of Earth Recomp

Thanks for helping. Code, reverse engineering, testing, bug reports,
corrections and art are all welcome, and every contributor is credited in
[CREDITS.md](CREDITS.md).

## Using an AI coding agent?

Most of this project is written with AI assistance. Your agent should follow
**[AGENTS.md](AGENTS.md)**: most agents read it from the repository root by
themselves; otherwise paste it into the agent's instructions. It has the full
rules. The short version is below.

## The rules, short

1. **English only** in the repository: docs, comments, script messages,
   commits.
2. **No game data in the repository.** No disc images, the game's ELF, IRX
   modules, BIOS, nothing from `game/` or `generated/` (the generated C++ is
   the game's code), no textures, audio or text from the game. Describe,
   don't paste.
3. **Write down what you learn.** If your change needed you to figure out how
   part of the game or the PS2 works, add a finding in
   [docs/findings/](docs/findings/README.md) (a new numbered file, or a
   section in an existing one) and a row in its table. Wrong turns belong in
   it too.
4. **Comment heavily**, and explain *why*. Every new file gets a header
   comment; every `ben10_poe.toml` entry gets a "why + how we found it"
   comment.
5. **Check Ghidra** for a function's boundaries before touching it in the
   config. Bind a Sony library function to a runtime handler only with
   evidence.
6. **PS2Recomp fixes become patches** in `patches/ps2recomp/`, with a
   header. Never edit the submodule or `generated/` as if they were our code.
7. **Test against a reference.** A new or fixed instruction gets a case in
   `tests/`, with the expected value citing its source (PCSX2 or the
   manual). Run `ctest` before the commit. Never change an expected value to
   make a test pass.
8. **No CPU emulation in the shipped program.** Interpreters only as test
   oracles (the temporary exceptions are in docs/KNOWN_ISSUES.md, T1/T2).
9. **Keep the original as it was by default.** New behaviour goes behind an
   option.
10. **Never risk players' saves.** Copy, don't move or delete.
11. **Neutral voice** in docs, comments and commits. Unknown official names:
    **(name TBD)**.

## Pull requests

- One topic per pull request.
- Say what changed, why, **how you tested it and what you didn't test**
  (Windows? Linux? compared with PCSX2?), and link the finding.
- If an AI agent helped, say so. You're responsible for having run the
  change.
- Contributions are licensed GPL-3.0 like the project. Add yourself to
  [CREDITS.md](CREDITS.md) with what you did.

Building: [docs/01-building.md](docs/01-building.md). How it all fits
together: [docs/02-how-the-port-works.md](docs/02-how-the-port-works.md).
