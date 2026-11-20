# Situação do projeto

**Fases 0 e 1: concluídas. Fase 2 (recompilador): em andamento.**
O código do jogo é traduzido, compila e liga inteiro no Linux. Ainda não
foi executado.

Leia primeiro: [INITIAL_AUDIT.md](INITIAL_AUDIT.md) (o que se sabe do jogo)
e o painel do [README](../README.md#-rumo-ao-nativo).

## Feito

- Estudo, análise do disco, do ELF, dos IRX e dos microprogramas de VU
  ([findings/01](findings/01-ps2recomp.md)-[11](findings/11-microprogramas-vu.md)).
- Ghidra headless: 4.420 funções, API de script nomeada
  ([findings/12](findings/12-analise-ghidra.md)).
- Primeira recompilação: 0 instruções não suportadas
  ([findings/13](findings/13-primeira-recompilacao.md)).
- Repositório no formato do Mind over Recomp, PS2Recomp como submódulo
  fixado, 5 patches aplicados pelo CMake
  ([findings/14](findings/14-repositorio-build-e-testes.md)).
- Build completo do jogo (`ben10_poe`) compila e liga.
- Testes por instrução de ponta a ponta; 4 erros do tradutor achados e
  corrigidos (LQ/SQ, SQRT.S, CVT.W.S, PADDUH/PSUBUH).

## Testes feitos

| O quê | Onde | Resultado |
|---|---|---|
| `ctest` (34 casos: 33 ativos, 1 desligado de propósito) | Linux, GCC 13 | **33/33 passam** |
| Os mesmos, antes dos patches 0002-0005 | Linux | 24 passavam, 8 falhavam (findings/14 s.5) |
| Build completo `ben10_poe` (antes e depois dos patches) | Linux, GCC 13, `-O0` | compila e liga |
| `tools/ghidra_analisar.sh` | Linux | reproduz a mesma lista de funções (20.234 entradas iguais) |
| Patches aplicados pelo CMake | Linux | 5 aplicados no submódulo limpo; nada reaplicado no 2º configure |
| `tools/coletar_disco.ps1` (versão do estudo) | Windows | ok |

**Não testado**: nada no Windows além da coleta do disco (MSVC,
`ghidra_analisar.ps1`, a versão do repositório de `coletar_disco.ps1`);
build otimizado; **executar o jogo**.

## Não resolvido

Ver [KNOWN_ISSUES.md](KNOWN_ISSUES.md): dívidas temporárias T1 (IOP
interpretado) e T2 (VU interpretado), e os riscos R10-R13 sobre instruções.

## Próxima tarefa

Ver [NEXT_TASK.md](NEXT_TASK.md).
