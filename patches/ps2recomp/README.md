# patches/ps2recomp/

Our fixes to [PS2Recomp](https://github.com/ran-j/PS2Recomp). The
`thirdparty/PS2Recomp` submodule stays at the pinned commit (`e563698`); the
`CMakeLists.txt` applies these patches in order at configure time, only the
ones not applied yet. Each file starts with a header: what broke, why, the
fix and how it was found.

| Patch | What | Found in |
|---|---|---|
| `0001-cmake-paths-as-submodule.patch` | CMake paths relative to the file itself, so it works as a submodule | findings/14 s.2 |
| `0002-lq-sq-align-address.patch` | LQ/SQ align the address to 16 bytes | findings/14 s.5 |
| `0003-sqrt-s-reads-ft.patch` | SQRT.S reads its operand from `ft` (R5900), not `fs` | findings/14 s.5 |
| `0004-cvt-w-s-truncate-and-saturate.patch` | CVT.W.S truncates toward zero and saturates | findings/14 s.5 |
| `0005-padduh-psubuh-saturate.patch` | PADDUH/PSUBUH saturate | findings/14 s.5 |
| `0006-kernel-data-in-kernel-ram.patch` | The runtime's own guest data (RPC/TLS pools, SIF command packets, callback stacks) moves to EE kernel RAM; the game's heap may grow up to the main stack | findings/15 s.15 |
| `0007-vu-max-mini-raw-bits.patch` | VU1 MAX/MINI compare raw bits and do not flush denormals (games use MAX as a move of integer data) | findings/18 s.9 |
| `0008-vu0-discard-vf0-writes.patch` | VU0 macro mode: writes to VF0 (QMTC2, LQC2, macro ops) and VI0 are discarded, as on hardware | findings/18 s.11 |
| `0009-mmi-word-halfword-permutes.patch` | MMI PROT3W, PEXEW, PEXCW, PEXEH, PEXCH move the right elements | findings/18 s.12 |
| `0010-vu0-macro-memory-ops.patch` | VU0 macro VLQI/VSQI/VLQD/VSQD/VILWR/VISWR use VU0 data memory; VSQI/VSQD operand fields | findings/18 s.12 |
| `0011-pad-keyboard-wasd-left-stick.patch` | Pad backend: W/A/S/D drive the left analog stick (arrows stay the D-pad) | findings/18 s.13 |
| `0012-cd-shared-sector-numbers.patch` | CD: sceCdSearchFile on the EE uses the IOP's virtual-disc LSNs, so LSNs the game passes to IOP modules match; faster `ownsRamRange` | findings/19 s.3 |
| `0013-vu-interpreter-hot-path.patch` | VU interpreter: cheaper hot path (pipeline commit only when due and only valid slots, fast XGKICK drain, decoded pairs by reference and decoded lazily after a code upload, bit scans for VI/lanes, inline operand normaliser); same results and cycle counts | findings/19 s.9, findings/20 |

Patches 0002-0010 and 0012 fix PS2 behaviour (0011 is a keyboard-mapping fix of the runtime, 0013 a speed-up with the same results) and should also go to upstream
PS2Recomp.

CMake keeps a stamp (`thirdparty/PS2Recomp/.ben10_patches_applied`, the
SHA-256 of the patch set). When the set changes (a patch added, edited,
merged or renamed), configure resets the submodule's files to the pinned
commit and applies every patch again, so work kept in the submodule itself
is lost: make it a patch.

To undo them all: `git -C thirdparty/PS2Recomp checkout .`, then
`git -C thirdparty/PS2Recomp clean -fd` (removes the new files, such as
`ps2xRuntime/include/ps2_kernel_ram.h` from 0006, and the stamp).
