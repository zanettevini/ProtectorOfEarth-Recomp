# 01: PS2Recomp, evaluated as the project's base

## 1. The question

The master prompt (s.4) asks to evaluate PS2Recomp before writing a
recompiler from scratch, and to fork it if it fits.

## 2. How it was studied

Reading the README and repository page of
[ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp) and the READMEs of
projects that already use it (see [02](02-reference-projects.md)). At this
stage **the source had not been cloned or built**: everything below is what
the documentation said; later findings check it against the code.

Wrong turn avoided: the master prompt's link to PS2Recomp was incomplete
(`https://github.com/`). The right repository is `ran-j/PS2Recomp`.

## 3. What it is

An experimental static recompiler from PS2 ELFs to C++, with a runtime to
run the generated code. Inspired by N64Recomp. About 3.3k stars and 150
forks at the time of the study.

Modules (according to the README):

| Module | Role |
|---|---|
| `ps2xAnalyzer` | reads the ELF and functions, writes the TOML config (stubs, skips, patches) |
| `ps2xRecomp` | reads TOML + ELF, decodes R5900 instructions, writes C++ |
| `ps2xRuntime` | guest memory, function registration, syscall dispatch, hardware stubs |
| `ps2xIOP` | runs IRX modules (R3000A), a virtual IOP kernel, generic HLE fallbacks |
| `ps2xStudio`, `ps2xTest` | UI and tests (not documented in the README) |
| `android/`, `vita/` | extra targets (not documented) |

Relevant technical points:

- Translates R5900 instructions to C++, **including MMI and VU0 macro mode**.
- Output as a single file or one file per function.
- Preferred workflow for stripped retail games: open the ELF in Ghidra, run
  `ExportPS2Functions.java`, and feed the exported TOML/CSV to `ps2_recomp`.
  `ps2_analyzer` is for quick tests or ELFs with debug symbols.
- TOML keys: `general.input`, `general.ghidra_output`, `general.output`,
  `general.single_file_output`, `general.low_memory_mode`,
  `general.output_worker_threads`, `general.patch_syscalls` / `patch_cop0` /
  `patch_cache`, `general.stubs` (including `handler@0xADDRESS`),
  `general.skip`, `patches.instructions`.
- A recompiled `SYSCALL` calls `runtime->handleSyscall(...)`.
- Unresolved static `J/JAL` calls fall back to `runtime->lookupFunction(0x...)`
  (a table lookup, **not** interpretation).
- `skip` functions become `ps2_stubs::TODO_NAMED(...)` wrappers.
- *Game overrides*: per-game C++ modules, registered with
  `PS2_REGISTER_GAME_OVERRIDE`, that rebind EE functions by address during
  `loadELF`.
- Requirements: CMake 3.20+, a C++20 compiler (mostly tested with MSVC),
  SSE4/AVX host CPU.
- License: **GPL-3.0**.

## 4. What is stubbed or limited

- GS, VU and much of the system are **basic stubs** (per the README).
- The README itself warns VU and GS performance is very poor.
- VU1 (microprograms) was described as **not** covered: VU0 macro mode only.
- `ps2xIOP` **runs the original IRX code** on an R3000A. That is a PS2 CPU
  interpreter, which conflicts with the master prompt. See
  [06](06-master-prompt-conflicts.md) s.2.

### 4.1 Correction (findings/13 s.5)

At commit `e563698` the runtime **does have** VU0 micro mode and VU1
(`ps2xRuntime/src/lib/vu/`), by interpretation. The README read for this
study was out of date on that point.

## 5. Conclusion

- **Use it as the base: yes**, as a submodule, like the projects in
  [02](02-reference-projects.md). It covers phase 2 and part of phase 3 (ELF,
  decoding, C++ generation, MMI, VU0 macro, syscalls, overrides).
- **Not to use from it** in the long run: the interpreted IOP path and the
  GS/VU stubs, to be replaced with our own native code.
- **License**: anything linked with the PS2Recomp runtime inherits GPL-3.0.
  The project must be GPL-3.0 (Gitaroo Man Native licenses its own code MIT,
  but its binaries end up GPL-3.0 anyway).
- The master prompt requires documented semantics and a test for every
  instruction. PS2Recomp does not promise that; that test coverage is our
  work.

## 6. Open

- ~~Clone and build the recompiler~~: done on Linux
  ([13](13-first-recompilation.md)). Still to do: build on Windows with MSVC
  and run the `ps2xTest` suites.
- ~~Instruction coverage~~: 0 unsupported instructions in this game
  ([13](13-first-recompilation.md) s.3).
- ~~How the runtime handles delay slots and likely branches~~: tested in
  [14](14-repository-build-and-tests.md). Indirect jumps and runtime-loaded
  code: still to check at run time.
- ~~Whether `ps2xIOP` can serve RPC without running IRX~~: it has generic HLE
  services for a few SIDs and runs the IRX for the rest
  ([15](15-first-boot.md) s.5).
