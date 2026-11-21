# Building

From your `.iso` to the `ben10_poe` executable, plus the tests. Four stages:
take the executable off the disc, analyse it in Ghidra, translate it to C++
and compile.

| Stage | Linux | Windows |
|---|---|---|
| Take the files off the disc | `bsdtar`/`7z` (s.3) | `tools\collect_disc.ps1` |
| Headless Ghidra | `tools/ghidra_analyze.sh` (**tested**) | `tools\ghidra_analyze.ps1` (untested) |
| Configure, translate, compile | `linux-*` presets (**tested**, GCC 13) | `windows-release` (**tested**, MSVC 17.14); `windows-debug` untested |
| Tests | `ctest --preset linux-tests` (**tested**: 33/33) | `ctest --preset windows-tests` (untested) |

## 1. What to install

| Tool | Version | For |
|---|---|---|
| Git | any recent | the repository and submodule; CMake uses it to apply the patches |
| CMake | 3.21+ | the build |
| Ninja | any recent | the build generator |
| Python | 3.10+ | tools and test generation |
| C++20 compiler | Windows: Visual Studio 2022 (or just its "Build Tools"), with "Desktop development with C++"; Linux: GCC 13+ or Clang 17+ | |
| JDK | 21 (Eclipse Temurin, for example) | Ghidra |
| Ghidra | 12.1.x | the function list |
| ghidra-emotionengine-reloaded | the same version as Ghidra | PS2 CPU support in Ghidra |

Linux (Ubuntu/Debian): the libraries the PS2Recomp runtime needs for its
window (raylib):

```bash
sudo apt install build-essential cmake ninja-build git python3 unzip \
    libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
    libxi-dev libasound2-dev libwayland-dev libxkbcommon-dev
```

Memory and disk: the generated C++ is ~160 MB in ~20,000 files. The `-O0`
build used little memory with 2 parallel jobs; with more jobs, watch the
RAM. Keep ~10 GB free.

## 2. Clone

```bash
git clone --recursive https://github.com/zanettevini/ProtectorOfEarth-Recomp.git
cd ProtectorOfEarth-Recomp
```

Already have the folder but `thirdparty/PS2Recomp` is empty?

```bash
git submodule update --init
```

Don't apply patches by hand: CMake's configure applies the ones in
`patches/ps2recomp/` by itself (docs/DECISIONS.md D10).

## 3. The disc files

USA release only: **SLUS-21661** (`SYSTEM.CNF` with `VER = 1.01`). The
runtime treats the ELF's folder as the disc's root, so the disc files go in
`game/` next to `game/SLUS_216.61` (findings/15 s.5).

**Windows**: put the `.iso` in an `iso\` folder at the repository root and
run, in PowerShell at the root:

```powershell
powershell -ExecutionPolicy Bypass -File tools\collect_disc.ps1
```

It mounts the `.iso` read-only, copies the ELF, the IRX modules,
`GAME.DIR` and `IOPRP300.IMG` into `game\`, and writes the SHA-1, the file
list and an environment report into `out\disc\`. `GAME.WAD` (2.4 GB) is not
copied by default; copy it into `game\` by hand to run the game.

**Linux**:

```bash
mkdir -p game
bsdtar -xf /path/to/game.iso -C game
```

(or `7z x /path/to/game.iso -ogame`)

## 4. The function list (Ghidra)

The recompiler needs to know where each function starts and ends; the
executable has no symbols. Ghidra finds them, and a script of ours names the
1,586 functions of the game's script API (docs/findings/12).

Put the zip of the ghidra-emotionengine-reloaded extension in
`<Ghidra>/Extensions/Ghidra/` (the script installs it from there).

**Linux**:

```bash
GHIDRA_DIR=/path/to/ghidra_12.1.4_PUBLIC tools/ghidra_analyze.sh
```

**Windows** (PowerShell, at the root):

```powershell
powershell -ExecutionPolicy Bypass -File tools\ghidra_analyze.ps1 -GhidraDir "C:\path\to\ghidra_12.1.4_PUBLIC"
```

It takes a few minutes and writes `generated/ghidra/functions.csv`. The log
should show `created=845 renamed=1586 ... failed=0`, `switch tables=91
split=84 chunked=4 case entries added=348` and `Exported 4801 functions and
16232 executable labels`.

## 5. Translate and compile

**Windows**: open the **"x64 Native Tools Command Prompt for VS 2022"**
(Start menu). It puts Microsoft's 64-bit compiler on the PATH; in a plain
prompt `cl` does not exist. At the repository root:

```bat
cmake --preset windows-release
cmake --build --preset windows-release --target ben10_codegen
cmake --preset windows-release
cmake --build --preset windows-release --target ben10_poe
```

`windows-release` is the tested one (optimised; about 8.5 minutes the first
time on a desktop, findings/19 s.1). `windows-debug` builds without
optimisation and runs much slower. If `vcvars64.bat` is called from a
script with its output redirected and seems to hang, set
`VSCMD_SKIP_SENDTELEMETRY=1` first and give it `< nul`.

**Linux**:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug --target ben10_codegen
cmake --preset linux-debug
cmake --build --preset linux-debug --target ben10_poe
```

Why configure twice: the executable is made from the files in
`generated/default/`, which only exist after `ben10_codegen`. The second
configure finds them (the message says `ben10_poe: 20235 generated files`).

What to expect:

- configure: `PS2Recomp: applied 0001-...` up to `0005-...` (the first time
  only);
- `ben10_codegen`: the report ends with `Unhandled instructions: 0` and
  `errors: 0`;
- `ben10_poe`: on Linux, ~5 minutes on 2 cores; the executable is
  `out/build/<preset>/ben10_poe` (`ben10_poe.exe` on Windows).

## 6. Run

```bash
out/build/linux-debug/ben10_poe game/SLUS_216.61
```

Windows: `out\build\windows-release\ben10_poe.exe game\SLUS_216.61`.

The game reaches its first level, but runs at about 5-6 frames per second
(KNOWN_ISSUES B13). Set `BEN10_FPS_LOG=1` to print the frame rate about once
a second (findings/19 s.2).

## 7. Tests

No game needed.

```bash
cmake --preset linux-tests          # Windows: windows-tests
cmake --build --preset linux-tests
ctest --preset linux-tests
```

Expected result: all pass and 1 shows as disabled
(`MulsDenormalOperandIsZero`, a known difference: KNOWN_ISSUES R10).

## 8. Known build problems

| Message | Cause | What to do |
|---|---|---|
| `include could not find requested file: .../ps2xRuntime/cmake/ReleaseMode.cmake` | patch 0001 not applied | configure again; if it persists, `git -C thirdparty/PS2Recomp status` and check the submodule is at commit `e563698` |
| `Patch 00NN-... did not apply` | the submodule is at another commit | `git submodule update --init`, then configure again (configure resets the submodule's files and reapplies the patches when the patch set changes) |
| `inlining failed ... target specific option mismatch` (GCC/Clang) | SSE4 flags missing | `CMakeLists.txt` already sets `-march=x86-64-v2`; check `BEN10_ARCH_FLAGS` was not cleared from the cache |
| `ben10_poe: generated/default/ is empty` | codegen has not run | step 5, `ben10_codegen`, then configure again |
| `C1002: compiler is out of heap space` on `generated/vu1/vu1_native_*.cpp` (MSVC), or the machine slows down while those compile | too many translated VU1 files compiling at once (findings/21 s.6) | configure prints `Translated VU1 code: at most N files compile at once`; lower it with `-DBEN10_VU1_COMPILE_JOBS=2` (Ninja generators only) |
