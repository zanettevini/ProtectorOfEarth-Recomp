# Créditos

## Projeto

- **Mantenedor do projeto**: direção, testes no jogo real.
- **Claude** (Anthropic), pelo Claude Code: assistente de IA que escreveu a
  maior parte do código, das ferramentas e da documentação.

## Software em que o port se apoia

| Projeto | Para quê | Licença |
|---|---|---|
| [PS2Recomp](https://github.com/ran-j/PS2Recomp) (ran-j e colaboradores) | o recompilador e o runtime (submódulo `thirdparty/PS2Recomp`); `tools/ghidra_scripts/ExportPS2Functions.java` é uma cópia dele | GPL-3.0 |
| [GoogleTest](https://github.com/google/googletest) | testes automatizados (baixado pelo CMake) | BSD-3-Clause |
| Dependências do PS2Recomp (ELFIO, toml11, fmt, libdwarf, rabbitizer, raylib...) | baixadas pelo CMake do PS2Recomp | as de cada projeto |

## Referências (lidas, não copiadas)

| Projeto | Para quê |
|---|---|
| [PCSX2](https://github.com/PCSX2/pcsx2) | comportamento de referência do PS2: os valores esperados dos testes citam o interpretador dele (GPL-3.0) |
| [Mind over Recomp](https://github.com/Maxilure/MindOver-Recomp) (Maxilure) | a organização do repositório, as regras do AGENTS.md e a ideia do painel `native_progress.py` (GPL-3.0) |
| [Gitaroo Man Native](https://github.com/erinxocon/gitaroo-man-native), [BT2 Recomp](https://github.com/oric212/BT2-Recomp), [BT3-Recomp](https://github.com/z3xox/BT3-Recomp) | outros ports de PS2 feitos com o PS2Recomp (docs/findings/02) |
| [Ghidra](https://github.com/NationalSecurityAgency/ghidra) e [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) | análise do executável (Apache-2.0) |
| [The Cutting Room Floor](https://tcrf.net/) | as opções de debug do jogo (docs/findings/03) |
