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

Patches 0002-0007 fix PS2 behaviour and should also go to upstream
PS2Recomp.

To undo them all: `git -C thirdparty/PS2Recomp checkout .`, then
`git -C thirdparty/PS2Recomp clean -f ps2xRuntime/include/ps2_kernel_ram.h`
(the one new file, from 0006).
