# 07: phase 0, development environment

## 1. The question

What is on the development machine, and what is missing?

## 2. How it was checked

At first the project folder was only listed: no command had been run on the
machine yet, so compiler and SDK versions were not known. The owner then ran
the commands in s.5 and reported the results in the table below.

## 3. State

| Item | State |
|---|---|
| OS | Windows |
| Game files | US disc image present (s.7) |
| BIOS | absent (only needed for the PCSX2 reference, not for the port) |
| Git repository | did not exist yet (now published, see the README) |
| Git | 2.56.0 (ok) |
| CMake | 4.4.4 (ok, above the 3.21 minimum) |
| Ninja | 1.13.2 (ok) |
| Python | 3.12.10 (ok) |
| Vulkan SDK | 1.4.363.0 (ok) |
| Java (JDK) | JDK 21 installed by the owner (not checked with `java -version`) |
| MSVC (`cl`) | **installed**: "Build Tools for Visual Studio 2022" with the C++ x64 component (`vswhere`). Use it from the "Developer PowerShell for VS 2022" |
| Disk space | enough (the build needs ~10 GB) |
| SDL3, GoogleTest | come through CMake (FetchContent), no install needed |
| Ghidra 12.1.4 + ghidra-emotionengine-reloaded 12.1.4 | downloaded into the study folder's `tools\` (extension still as a `.zip`; `tools/ghidra_analyze.ps1` installs it) |
| PCSX2 | not checked |

## 4. What is needed

| Tool | Minimum version | For |
|---|---|---|
| Visual Studio 2022 (MSVC, "Desktop development with C++") | 17.x | PS2Recomp's main compiler |
| CMake | 3.21+ (PS2Recomp asks for 3.20) | build |
| Ninja | any recent | fast builds |
| Git | any recent | version control + submodules |
| Python | 3.11+ | tool scripts |
| Vulkan SDK (LunarG) | 1.3+ | graphics backend, validation layers |
| SDL3 | 3.2+ (can come through CMake) | window, input, audio |
| GoogleTest | through CMake (FetchContent) | tests |
| Ghidra | 12.1.x | ELF analysis |
| JDK | whatever the Ghidra version requires (Gitaroo cites JDK 25 with Ghidra 12.1) | running Ghidra |
| ghidra-emotionengine-reloaded | the release matching the installed Ghidra | R5900/MMI/VU0 support in Ghidra |
| PCSX2 (stable) | 2.x | behaviour reference and captures |
| 7-Zip | any | extracting the ISO |

Memory: BT3-Recomp asks for ~16 GB of RAM and ~10 GB free for the build;
large generated files use a lot of RAM to compile (MoR recommends limiting
parallel jobs).

## 5. Commands for the environment report

Run in PowerShell and paste the output into `NEXT_TASK.md` or a
conversation:

```powershell
git --version
cmake --version
ninja --version
python --version
java -version
where cl
$env:VULKAN_SDK
Get-PSDrive C | Select-Object Used,Free
```

## 6. Open

- Install a JDK compatible with the chosen Ghidra version (Ghidra 11 and
  later need JDK 21 or newer; check the README of the downloaded version).
- ~~Ghidra 12.1.x + ghidra-emotionengine-reloaded~~: downloaded.

## 7. Disc received

- File: `Ben 10 - Protector of Earth (USA).iso`, 3,400,499,200 bytes.
- It could not be read directly: at 3.4 GB it cannot be copied to the agent
  environment in one go (400 MB per-file limit). The script
  `tools/collect_disc.ps1` extracts only what phase 1 needs (SYSTEM.CNF, boot
  ELF, IRX, manifest, SHA-1) into `out\disc\`.
