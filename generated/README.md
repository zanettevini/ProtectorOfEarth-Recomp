# generated/

Tudo aqui é feito na sua máquina a partir do SEU disco e **nunca é
versionado** (`.gitignore`):

| Pasta | O que é | Quem faz |
|---|---|---|
| `generated/ghidra/` | o projeto do Ghidra e `functions.csv`, a lista de funções que o recompilador lê | `tools/ghidra_analisar.sh` / `.ps1` |
| `generated/default/` | o C++ gerado: o código do jogo traduzido, ~20 mil arquivos | alvo `ben10_codegen` do CMake |

Para refazer do zero, apague a pasta e rode os dois passos de novo
(docs/01-building.md).
