# Roteiro de desenvolvimento

As fases são as do master prompt (`MASTER_PROMPT.md` s.6). Aqui fica o que
cada uma depende e o primeiro passo concreto.

| Fase | Assunto | Depende de | Primeiro passo | Situação |
|---|---|---|---|---|
| 0 | Ambiente e repositório | — | instalar JDK/Ghidra; criar o repositório | quase concluída |
| 1 | Análise do executável | disco, Ghidra | — | **concluída** ([INITIAL_AUDIT](INITIAL_AUDIT.md)) |
| 2 | Recompilador | 1 | mais testes por instrução (R13) | **em andamento**: build completo liga; 33 testes passam; 4 erros do tradutor corrigidos ([findings/14](findings/14-repositorio-build-e-testes.md)) |
| 3 | Runtime do EE + VU | 2, D3 | testes por instrução (MMI, FPU); census de microprogramas de VU | não iniciada |
| 4 | Serviços do sistema | 1, D2 | lista de RPCs do IOP que o jogo usa; serviço de arquivos | não iniciada |
| 5 | Gráficos Vulkan | 3, 4, D4 | integrar paraLLEl-GS; primeiro frame | não iniciada |
| 6 | Áudio e entrada | 4 | SDL3 para controle; SPU2 | não iniciada |
| 7 | Primeira execução | 3-6 | do boot até o menu | não iniciada |
| 8 | Compatibilidade | 7 | todas as fases, chefes, cutscenes, comparação com PCSX2 | não iniciada |
| 9 | Melhorias de PC | 8 | resolução, widescreen, opções | não iniciada |

## Prioridades imediatas

1. Primeira execução (Linux): até onde o boot chega.
2. Build e testes no Windows (MSVC).
3. Mais testes por instrução (R13).
4. Fase 1 completa: `INITIAL_AUDIT.md`, `EXECUTABLE_ANALYSIS.md`, inventário
   de funções, cobertura de instruções, dependências de hardware.
