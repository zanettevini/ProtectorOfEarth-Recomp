# patches/ps2recomp/

Nossas correções do [PS2Recomp](https://github.com/ran-j/PS2Recomp). O
submódulo `thirdparty/PS2Recomp` fica no commit fixado (`e563698`); o
`CMakeLists.txt` aplica estes patches em ordem, no configure, só os que
ainda não estão aplicados. Cada arquivo começa com um cabeçalho: o que
quebrou, por quê, a correção e como foi achado.

| Patch | O quê | Achado em |
|---|---|---|
| `0001-cmake-paths-as-submodule.patch` | caminhos de CMake relativos ao próprio arquivo, para funcionar como submódulo | findings/14 s.2 |
| `0002-lq-sq-align-address.patch` | LQ/SQ alinham o endereço em 16 bytes | findings/14 s.5 |
| `0003-sqrt-s-reads-ft.patch` | SQRT.S lê o operando de `ft` (R5900), não de `fs` | findings/14 s.5 |
| `0004-cvt-w-s-truncate-and-saturate.patch` | CVT.W.S trunca em direção a zero e satura | findings/14 s.5 |
| `0005-padduh-psubuh-saturate.patch` | PADDUH/PSUBUH saturam | findings/14 s.5 |

Os patches 0002-0005 são correções do comportamento do PS2 e devem ir
também para o PS2Recomp original.

Para desfazer todos: `git -C thirdparty/PS2Recomp checkout .`
