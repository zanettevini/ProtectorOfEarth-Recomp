# AGENTS.md: rules for AI coding agents working on Protector of Earth Recomp

This file is for **AI coding agents** (Claude Code, Codex, Cursor, Copilot,
Gemini CLI, Aider, ...) helping with *Protector of Earth Recomp*, an
unofficial native PC port of *Ben 10: Protector of Earth* (PlayStation 2,
High Voltage Software / D3 Publisher, 2007).

It follows the structure of the `AGENTS.md` of *Mind over Recomp*, adapted
to the PS2 (MIPS R5900, little-endian) and to PS2Recomp instead of ReXGlue.
The project's master prompt ([docs/MASTER_PROMPT.md](docs/MASTER_PROMPT.md))
wins when the two disagree. For Claude Code, put `@AGENTS.md` in `CLAUDE.md`.
Humans: the same rules, shorter, are in [CONTRIBUTING.md](CONTRIBUTING.md).

The rules marked **(hard)** are checked in review: a pull request that breaks
one can't be merged as it is.

## 0. Language (hard)

Everything in the repository is written in **English**: docs, findings,
code comments, script messages, commit messages, pull requests. Talking to
the maintainer in another language is fine; the repository is English only.

## 1. What the project is

- The game's main executable (`SLUS_216.61`, USA release 1.01) is
  **statically recompiled** into C++ by
  [PS2Recomp](https://github.com/ran-j/PS2Recomp) (pinned submodule, plus
  our patches). The generated code runs on a native C++20 runtime (memory,
  EE kernel, DMA, VIF/GIF, GS, audio, input).
- **No CPU emulation** in the shipped program: no interpreter, no dynarec, no
  embedded PCSX2 (master prompt s.3). PCSX2 is only a **reference** for tests.
  The temporary exceptions are listed in docs/KNOWN_ISSUES.md (T1, T2).
- Long-term goal: replace emulated pieces with native ones one at a time,
  keeping the recompiled original as the answer key for tests.
- Read first, as needed: [docs/02-how-the-port-works.md](docs/02-how-the-port-works.md)
  (big picture), [docs/01-building.md](docs/01-building.md) (build and
  patches), [docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md) and
  [docs/NEXT_TASK.md](docs/NEXT_TASK.md) (where work stopped),
  [docs/findings/](docs/findings/README.md) (everything learned so far:
  search it before investigating something, the answer is often there).

## 2. Legal: no game data in the repository (hard)

Everyone builds the port from their own copy of the game. The repository holds
only the project's own code, notes and art.

- Never commit or paste: `*.iso`, `*.bin`, `*.chd`, the game's ELF
  (`SLUS_*`, `SLES_*`), `*.IRX` modules from the disc, BIOS files, anything
  in `iso/`, `game/`, `generated/` (that IS the game's code, translated, and
  the Ghidra analysis), `out/`, `user/`, `logs/`, raw captures (`*.gs`,
  `*.p2s`, `*.ppm`). They are gitignored: never force-add them, never weaken
  `.gitignore`.
- Never copy game content into tracked files: textures, models, audio, text,
  the contents of `GAME.DIR`/`GAME.WAD`, VU microprograms, disassembly
  listings longer than a few illustrative lines. **Describe it, don't paste
  it** (an address, a size, a field layout, what it does).
- `assets/` is only for original art or free-licensed third-party art with
  its licence and source credited in `CREDITS.md`.
- Before every commit: `git diff --cached --name-only` and check the list.

## 3. Write down what you find (findings)

Reverse engineering that isn't written down is lost. Every pull request that
*learns something about the game or the PS2* records it.

- **New investigation**: a new `docs/findings/NN-short-topic.md` (NN = next
  free number) plus one row in the table of
  [docs/findings/README.md](docs/findings/README.md).
- **Small addition** to a known topic: a new numbered section in the
  existing finding (`grep -rl` the address or topic first).
- **Trivial fixes**: no finding needed.

A finding contains:

1. What was seen: the symptom or the question.
2. How it was found: tools, traces, measurements, and the **wrong turns**.
3. What it is: functions (`FUN_00XXXXXX`, or the library name once
   identified), addresses, struct offsets, formats, the game's own names
   when known.
4. What we changed and how it was verified (numbers, before/after, PCSX2
   comparison).
5. What is still open or untested.

Voice for every tracked file: **neutral and factual**. No personal details,
no PC specs of a specific person, no chat quotes. Unknown official names:
write **(name TBD)**. Don't guess names.

## 4. Comments: heavy, and they explain why

This project is read by people learning how recompilation works.

- Every new file starts with a header comment: what it is for, how it hooks
  into the game, the formats/offsets it relies on.
- Comment every non-obvious line, every guest address and every offset
  (`// +0x24 = entity list pointer, findings/NN s.M`).
- **Every [`ben10_poe.toml`](ben10_poe.toml) entry gets a *why + how we
  found it* comment** (hard).
- Point to the finding that explains the code (`findings/NN s.M`).

## 5. Working with the game's code

- **Verify before you change the config (hard).** Check a function's start
  and end in Ghidra (with the *ghidra-emotionengine-reloaded* extension)
  before adding, skipping or stubbing it. A wrong boundary splits a real
  function and corrupts it silently. After each codegen, read the report for
  unresolved calls and unsupported instructions.
- **Binding a Sony library function to a runtime handler**
  (`"handlerName@0xADDRESS"` in `ben10_poe.toml` `stubs`): only with
  evidence (a semaphore or error string the library leaves, the RPC server
  and function number, the structure compared with the open-source PS2SDK).
  `tools/analyzer/function_map.py` and `tools/analyzer/rpc_calls.py` collect
  that evidence. Bind a library family together: binding `sceCdRead` alone
  leaves the game's own `sceCdSync` waiting forever (findings/15).
- **Overriding a game function**: a *game override* registered in the
  runtime (`PS2_REGISTER_GAME_OVERRIDE` / `bindAddressHandler`), in `src/`,
  never by editing generated code.
- **Never edit `generated/`** (it is rebuilt) **or `thirdparty/PS2Recomp`
  directly.** PS2Recomp fixes go in `patches/ps2recomp/NNNN-name.patch`,
  with a header (what broke, why, the fix, how it was found) and a row in
  [patches/ps2recomp/README.md](patches/ps2recomp/README.md). CMake applies
  them by itself. To make one: edit the submodule, `git -C
  thirdparty/PS2Recomp diff` of that topic only, header on top; check that a
  clean configure applies all of them in order.
- **Guest memory from C++ (hard):** never raw `rdram + addr`. Use the runtime
  accessors, which apply the address mask (KUSEG / KSEG0 / KSEG1 alias the
  same physical RAM) and bounds checks. The PS2 is **little-endian** like
  x86-64, but its address map is not trivial: scratchpad (`0x70000000`),
  hardware registers (`0x10000000`-`0x1FFFFFFF`) and RAM mirrors.
- A pointer read from a game object can be garbage: check it's readable
  before following it.
- **An unsupported instruction is never ignored silently** (master prompt
  s.10): the recompiler fails or emits an explicit stop with the address.
- **Windows builds too**: don't name functions or variables after Win32 APIs
  or `windows.h` macros (`near`, `far`, `min`, `max`, `PlaySound`...). No
  POSIX-only calls outside `#ifdef`'d platform code.

## 6. Design rules

- **The README board is measured, not estimated.** When a piece becomes the
  port's own code (or a milestone is reached), change its status in
  `tools/native_progress.py` with the evidence (finding, test, log) and run
  `python3 tools/native_progress.py --readme`. Never edit the block by hand.
- **Native over emulated.** Never drive a feature by **faking controller
  presses**: find the game's own decision and answer it.
- **No CPU interpreter in the shipped program.** An interpreter (EE, IOP or
  VU) may exist only as a **test oracle**, built outside the game executable.
  Current temporary exceptions: docs/KNOWN_ISSUES.md T1/T2.
- **Keep the original's defaults.** The game must look and play like the
  original unless the player changes a setting. Enhancements (resolution,
  widescreen, 60 fps) go behind an option.
- **Players' saves are sacred.** Never move or delete saves or settings in a
  format change: copy, keep the old data as a backup.
- **Two players.** The game has local co-op for 2: code that handles "the
  player" probably needs to work for both.

## 7. Testing

Say what you tested and what you didn't. "Untested on Linux" is fine;
claiming something works that wasn't run is not (master prompt s.10).

- Every supported instruction has an automated test (GoogleTest) compared
  against a trusted reference (master prompt phase 2). Instruction tests are
  end to end: a new case goes into `tests/tools/make_test_elf.py` and
  `tests/instructions/`, with the expected value citing its source (a PCSX2
  function or a manual section). Run `ctest --preset linux-tests` (or
  `windows-tests`) before every commit that touches the recompiler or the
  runtime.
- A test that fails because of a known, documented difference may stay
  `DISABLED_`, with the reason in its comment and a line in KNOWN_ISSUES.
  Never change an expected value to make a test pass.
- System components: compare observable behaviour with PCSX2.
- Graphics: compare the image and GS state of the **same frame** with PCSX2
  and give the mean difference.
- **Test on copies, never on real saves.**
- Stop a test run by its PID with `kill -TERM`. **Never** send it
  `ABRT`/`SEGV`/`QUIT`: a core dump of the guest memory can exhaust RAM.
  Never kill by name on a machine where someone may be playing.
- Don't build while the game is running.

## 8. Pull requests and commits

- One topic per pull request. Don't reformat files you didn't otherwise touch.
- Commit messages: a short summary line, then what changed and why, in the
  neutral voice of section 3.
- The pull request description says: what changed, why, how it was tested
  (and what wasn't), which finding documents it, and screenshots for visible
  changes.
- AI assistance is welcome: say so in the description. The human who opens
  the PR is responsible for it having been run.
- Licence: the project is GPL-3.0, because PS2Recomp is GPL-3.0
  (findings/01 s.5).

## 9. Quick map

| Where | What |
|---|---|
| `ben10_poe.toml` | recompiler config: stubs, bindings, skips, all commented |
| `CMakeLists.txt`, `CMakePresets.json` | the build: patches, codegen, game, tests |
| `src/` | the port's native code (see src/README.md) |
| `tests/` | GoogleTest; `tests/tools/make_test_elf.py` builds the test ELF |
| `tools/collect_disc.ps1` | takes the ELF and IRX modules from the `.iso` into `game/` |
| `tools/ghidra_analyze.sh` / `.ps1`, `tools/ghidra_scripts/` | headless Ghidra: the function list for the recompiler |
| `tools/analyzer/` | ELF audit, per-function call/string map, RPC call map, IRX imports/exports |
| `tools/native_progress.py` | the README board and milestones |
| `patches/ps2recomp/` | our PS2Recomp fixes |
| `thirdparty/PS2Recomp/` | submodule: recompiler + runtime |
| `docs/findings/` | the investigation notes |
| `docs/PROJECT_STATUS.md`, `NEXT_TASK.md`, `DECISIONS.md`, `KNOWN_ISSUES.md` | tracking (master prompt s.9) |
