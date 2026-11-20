# src/

O código nativo do **próprio port**: o que substitui, peça por peça, o PS2
que o PS2Recomp ainda emula. Por enquanto está vazio: o executável usa o
`main` e o runtime do PS2Recomp.

Cada assunto vai numa pasta própria quando existir, como no Mind over
Recomp, por exemplo:

| Pasta (planejada) | O quê |
|---|---|
| `src/iop/` | serviços nativos que respondem aos módulos do IOP (findings/10) |
| `src/vu/` | microprogramas de VU traduzidos no build (findings/11) |
| `src/gs/` | o renderizador Vulkan |
| `src/input/`, `src/audio/`, `src/saves/` | controles (SDL3), som, saves no PC |
| `src/overrides/` | funções do jogo trocadas por código nativo, por endereço |

Toda vez que uma peça vira código daqui, atualize `tools/native_progress.py`
e rode `python3 tools/native_progress.py --readme` (AGENTS.md s.6).
