# 01: PS2Recomp, avaliação como base do projeto

## 1. A pergunta

O master prompt (s.4) pede para avaliar o PS2Recomp antes de escrever um
recompilador do zero, e para fazer fork se ele servir.

## 2. Como foi estudado

Leitura do README e da página do repositório
[ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp) e de READMEs de projetos
que já o usam (ver [02](02-projetos-de-referencia.md)). **O código-fonte
ainda não foi clonado nem compilado.** Tudo abaixo é o que a documentação
diz; a Fase 2 precisa confirmar no código.

Caminho errado evitado: o link do master prompt para o PS2Recomp estava
incompleto (`https://github.com/`). O repositório certo é `ran-j/PS2Recomp`.

## 3. O que é

Recompilador estático experimental de ELFs de PS2 para C++, com um runtime
para executar o código gerado. Inspirado no N64Recomp. Autor brasileiro
(ran-j). Por volta de 3,3 mil estrelas e 150 forks na data do estudo.

Módulos (segundo o README):

| Módulo | Função |
|---|---|
| `ps2xAnalyzer` | lê o ELF e as funções, gera a config TOML (stubs, skips, patches) |
| `ps2xRecomp` | lê TOML + ELF, decodifica instruções R5900, gera C++ |
| `ps2xRuntime` | memória do guest, registro de funções, dispatch de syscalls, stubs de hardware |
| `ps2xIOP` | executa módulos IRX (R3000A), kernel virtual do IOP, fallbacks HLE genéricos |
| `ps2xStudio`, `ps2xTest` | interface e testes (não documentados no README) |
| `android/`, `vita/` | alvos extras (não documentados) |

Pontos técnicos relevantes:

- Traduz instruções R5900 para C++, **com MMI e VU0 em modo macro**.
- Saída em arquivo único ou um arquivo por função.
- Fluxo preferido para jogos comerciais sem símbolos: abrir o ELF no Ghidra,
  rodar o script `ExportPS2Functions.java` e alimentar o `ps2_recomp` com o
  TOML/CSV exportado. O `ps2_analyzer` é para testes rápidos ou ELFs com
  símbolos de debug.
- Chaves TOML: `general.input`, `general.ghidra_output`, `general.output`,
  `general.single_file_output`, `general.low_memory_mode`,
  `general.output_worker_threads`, `general.patch_syscalls` / `patch_cop0` /
  `patch_cache`, `general.stubs` (incluindo `handler@0xENDEREÇO`),
  `general.skip`, `patches.instructions`.
- `SYSCALL` recompilado chama `runtime->handleSyscall(...)`.
- Chamadas estáticas `J/JAL` não resolvidas caem em
  `runtime->lookupFunction(0x...)` (procura em tabela, **não** interpreta).
- Funções em `skip` viram wrappers `ps2_stubs::TODO_NAMED(...)`.
- *Game overrides*: módulos C++ por jogo, registrados com
  `PS2_REGISTER_GAME_OVERRIDE`, que religam funções do EE por endereço
  durante o `loadELF`.
- Requisitos: CMake 3.20+, compilador C++20 (testado principalmente com
  MSVC), CPU com SSE4/AVX.
- Licença: **GPL-3.0**.

## 4. O que é stub ou limitado

- GS, VU e boa parte do sistema são **stubs básicos**.
- O próprio README avisa que o desempenho de VU e GS é muito ruim.
- VU1 (microprogramas) **não** é coberto: só VU0 em modo macro.
- O `ps2xIOP` **executa o código original dos IRX** num R3000A. Isso é um
  interpretador de CPU do PS2, o que conflita com o master prompt. Ver
  [06](06-conflitos-com-o-master-prompt.md) s.1.

### 4.1 Correção (findings/13 s.5)

No commit `e563698` o runtime **já tem** VU0 em modo micro e VU1
(`ps2xRuntime/src/lib/vu/`), por interpretação. O README lido neste
estudo estava desatualizado nesse ponto.

## 5. Conclusão

- **Usar como base: sim**, como fork em submódulo, igual aos projetos de
  [02](02-projetos-de-referencia.md). Ele resolve as Fases 2 e parte da 3
  (ELF, decodificação, geração de C++, MMI, VU0 macro, syscalls, overrides).
- **Não usar dele**: o caminho de IOP por interpretação e os stubs de GS/VU,
  que serão trocados por código nativo próprio.
- **Licença**: tudo que for ligado ao runtime do PS2Recomp herda GPL-3.0. O
  projeto deve ser GPL-3.0 (o Gitaroo Man Native usa MIT no próprio código,
  mas os binários acabam GPL-3.0 de qualquer jeito).
- O master prompt pede que cada instrução tenha semântica documentada e
  teste. O PS2Recomp não promete isso; essa cobertura de testes é trabalho
  nosso.

## 6. Em aberto

- ~~Clonar e compilar o recompilador~~ feito no Linux ([13](13-primeira-recompilacao.md)). Falta compilar no Windows com MSVC e rodar os testes do `ps2xTest`.
- ~~Cobertura de instruções~~: 0 instruções não suportadas neste jogo ([13](13-primeira-recompilacao.md) s.3).
- Verificar como o runtime trata delay slots, `likely` branches, saltos
  indiretos (`JR`/`JALR` por tabela) e código carregado em tempo de execução.
- Ler o README do `ps2xIOP` para ver se dá para usar só a parte HLE sem
  executar IRX.
