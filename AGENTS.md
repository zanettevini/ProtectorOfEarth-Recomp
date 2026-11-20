# AGENTS.md: regras para agentes de IA no Protector of Earth Recomp

Este arquivo é para **agentes de IA** (Claude Code, Codex, Cursor, Copilot,
Gemini CLI, Aider...) que ajudam no *Protector of Earth Recomp*, um port
nativo não oficial para PC de *Ben 10: Protector of Earth* (PlayStation 2,
High Voltage Software / D3 Publisher, 2007).

Ele segue a estrutura do `AGENTS.md` do projeto *Mind over Recomp*, adaptada
para PS2 (MIPS R5900, little-endian) e para o PS2Recomp no lugar do ReXGlue.
O master prompt do projeto ([docs/MASTER_PROMPT.md](docs/MASTER_PROMPT.md)) tem prioridade quando os
dois discordarem. Para o Claude Code, coloque `@AGENTS.md` no `CLAUDE.md`.
Humanos: as mesmas regras, curtas, estão no [CONTRIBUTING.md](CONTRIBUTING.md).

As regras marcadas **(rígida)** são checadas em revisão: um pull request que
quebra uma delas não entra como está.

## 1. O que é o projeto

- O executável principal do jogo (`SLUS_216.61`, versão americana 1.01) é
  **recompilado estaticamente** para C++ pelo
  [PS2Recomp](https://github.com/ran-j/PS2Recomp) (submódulo fixado, mais os
  nossos patches). O código gerado roda sobre
  um runtime nativo em C++20 (memória, kernel do EE, DMA, VIF/GIF, GS em
  Vulkan, áudio, entrada).
- **Sem emulação da CPU** em produção: nem interpretador, nem dynarec, nem
  PCSX2 embutido (master prompt s.3). O PCSX2 é só **referência** para testes
  e capturas.
- Objetivo de longo prazo: substituir peças emuladas por nativas uma de cada
  vez, com a versão recompilada mantida como gabarito dos testes.
- Leia antes, conforme precisar: [docs/02-how-the-port-works.md](docs/02-how-the-port-works.md)
  (visão geral), [docs/01-building.md](docs/01-building.md) (build e patches),
  [docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md) e [docs/NEXT_TASK.md](docs/NEXT_TASK.md)
  (onde parou), [docs/findings/](docs/findings/README.md) (tudo que já se
  sabe: procure ali antes de investigar, a resposta costuma estar lá).

## 2. Legal: nenhum dado do jogo no repositório (rígida)

Cada pessoa constrói o port a partir da própria cópia do jogo. O repositório
guarda só o código, as notas e a arte do próprio projeto.

- Nunca commitar nem colar: `*.iso`, `*.bin`, `*.chd`, o ELF do jogo
  (`SLUS_*`, `SLES_*`), módulos `*.IRX` do disco, BIOS, nada de `iso/`,
  `game/`, `generated/` (aquilo **é** o código do jogo traduzido, e a
  análise do Ghidra), `out/`, `user/`, `logs/`, capturas brutas (`*.gs`,
  `*.p2s`, `*.ppm`).
  Eles estão no `.gitignore`: nunca force-add, nunca enfraqueça o `.gitignore`.
- Nunca copiar conteúdo do jogo para arquivos versionados: texturas, modelos,
  áudio, textos, o conteúdo do `game.dir`, microprogramas de VU, listagens de
  disassembly maiores que algumas linhas ilustrativas. **Descreva, não cole**
  (um endereço, um tamanho, um layout de campos, o que faz).
- `assets/` só para arte original ou de licença livre com crédito em
  `CREDITS.md`.
- Antes de cada commit: `git diff --cached --name-only` e confira a lista.

## 3. Escreva o que você descobrir (findings)

Engenharia reversa que não é escrita se perde. Todo pull request que
*aprende algo sobre o jogo ou sobre o PS2* registra isso.

- **Investigação nova**: um novo `docs/findings/NN-assunto-curto.md`
  (NN = próximo número livre) e uma linha na tabela de
  [docs/findings/README.md](docs/findings/README.md).
- **Acréscimo pequeno** a um assunto conhecido: uma nova seção numerada no
  finding existente (`grep -rl` o endereço ou o assunto antes).
- **Correções triviais**: sem finding.

Um finding contém:

1. O que foi visto: o sintoma ou a pergunta.
2. Como foi descoberto: ferramentas, traces, medições, e os **caminhos
   errados**.
3. O que é: funções (`sub_00XXXXXX` / `FUN_00XXXXXX`), endereços, offsets de
   struct, formatos, os nomes do próprio jogo quando conhecidos.
4. O que mudamos e como foi verificado (números, antes/depois, comparação
   com o PCSX2).
5. O que ainda está aberto ou não testado.

Voz de todo arquivo versionado: **neutra e factual**. Sem dados pessoais, sem
specs do PC de uma pessoa específica, sem citações de chat. Nome oficial
desconhecido: escreva **(nome a definir)**. Não invente nomes.

Idioma: a documentação de estudo está em português (pt-BR). Identificadores
de código, nomes de arquivo e mensagens de log ficam em inglês.

## 4. Comentários: muitos, explicando o porquê

O projeto é lido por quem está aprendendo recompilação.

- Todo arquivo novo começa com um comentário de cabeçalho: para que serve,
  como se liga ao jogo, que formatos/offsets usa.
- Comente toda linha não óbvia, todo endereço do guest e todo offset
  (`// +0x24 = ponteiro para a lista de entidades, findings/NN s.M`).
- **Toda entrada do [`ben10_poe.toml`](ben10_poe.toml) (config do PS2Recomp) tem comentário de
  *por que + como descobrimos*** (rígida).
- Aponte o finding que explica o código (`findings/NN s.M`).

## 5. Trabalhando com o código do jogo

- **Verifique antes de mexer na config (rígida).** Confira no Ghidra (com a
  extensão *ghidra-emotionengine-reloaded*) o início e o fim de uma função
  antes de adicioná-la, pulá-la ou transformá-la em stub. Uma fronteira
  errada corta uma função real e a corrompe em silêncio. Depois de cada
  recompilação, leia o log procurando chamadas não resolvidas
  (`lookupFunction`) e instruções não suportadas.
- **Sobrescrever uma função do jogo**: um *game override* registrado no
  runtime (`PS2_REGISTER_GAME_OVERRIDE` / `bindAddressHandler` no PS2Recomp),
  em `src/`, nunca editando o gerado.
- **Nunca editar `generated/`** (é reconstruído) **nem
  `thirdparty/PS2Recomp` diretamente.** Correções do PS2Recomp vão em
  `patches/ps2recomp/NNNN-nome.patch`, com cabeçalho (o que quebrou, por quê,
  a correção, como foi achado) e uma linha na tabela de
  [patches/ps2recomp/README.md](patches/ps2recomp/README.md). O CMake aplica
  sozinho. Para fazer um patch: edite o submódulo, `git -C thirdparty/PS2Recomp
  diff` só daquele assunto, cabeçalho em cima; confira que um configure limpo
  aplica todos em ordem.
- **Memória do guest a partir do C++ (rígida):** nunca `rdram + addr` cru.
  Use os acessores do runtime, que aplicam a máscara de endereço (KUSEG /
  KSEG0 / KSEG1 apontam para a mesma RAM física) e checam limites. O PS2 é
  **little-endian** como o x86-64, mas o mapeamento de endereços não é
  trivial: scratchpad (`0x70000000`), registradores de hardware
  (`0x10000000`-`0x1FFFFFFF`) e espelhos da RAM.
- Um ponteiro lido de um objeto do jogo pode ser lixo: confira se é legível
  antes de seguir.
- **Instrução não suportada nunca é ignorada em silêncio** (master prompt
  s.10): o recompilador falha ou gera uma parada explícita com o endereço.
- **Windows também compila**: não dê nomes de APIs Win32 ou macros de
  `windows.h` a funções e variáveis (`near`, `far`, `min`, `max`,
  `PlaySound`...). Nada só-POSIX fora de código de plataforma com `#ifdef`.

## 6. Regras de design

- **O painel do README é medido, não estimado.** Quando uma peça vira código
  do port (ou um marco é atingido), mude o status em
  `tools/native_progress.py` com a evidência (finding, teste, log) e rode
  `python3 tools/native_progress.py --readme`. Nunca edite o bloco à mão.
- **Nativo em vez de emulado.** Nenhuma funcionalidade nova é feita
  **simulando apertos de botão**: encontre a decisão do próprio jogo e
  responda a ela.
- **Sem interpretador de CPU em produção.** Um interpretador (de EE, IOP ou
  VU) só pode existir como **oráculo de teste**, compilado fora do executável
  final. Ver [findings/06](docs/findings/06-conflitos-com-o-master-prompt.md)
  e `docs/DECISIONS.md`.
- **Mantenha os padrões do original.** O jogo deve parecer e jogar como o
  original até o jogador mudar uma opção. Melhorias (resolução, widescreen,
  60 fps) ficam atrás de uma opção.
- **Os saves dos jogadores são sagrados.** Nunca mover ou apagar saves ou
  configurações numa mudança de formato: copie e mantenha o antigo como
  backup.
- **Dois jogadores.** O jogo tem cooperativo local para 2: código que trata
  "o jogador" provavelmente precisa funcionar para os dois.

## 7. Testes

Diga o que testou e o que não testou. "Não testado no Linux" é aceitável;
dizer que algo funciona sem ter rodado não é (master prompt s.10).

- Toda instrução suportada tem teste automatizado (GoogleTest) comparado com
  uma referência confiável (master prompt Fase 2). Os testes de instrução são
  de ponta a ponta: um caso novo entra em `tests/tools/make_test_elf.py` e
  em `tests/instructions/`, com o valor esperado citando a fonte (função do
  PCSX2 ou seção do manual). Rode `ctest --preset linux-tests` (ou
  `windows-tests`) antes de cada commit que toca o recompilador ou o
  runtime.
- Teste que falha por uma diferença conhecida e documentada pode ficar
  `DISABLED_`, com o motivo no comentário e uma linha no KNOWN_ISSUES.
  Nunca mude o valor esperado para o teste passar.
- Componentes de sistema: compare o comportamento observável com o PCSX2.
- Gráficos: compare a imagem e o estado do GS do **mesmo frame** com o PCSX2
  e informe a diferença média.
- **Teste em cópias, nunca em saves reais.**
- Pare um jogo de teste pelo PID. Nunca mate processos pelo nome: você pode
  fechar um jogo que alguém está jogando.
- Não compile enquanto o jogo está rodando.
- Nunca enfraqueça uma asserção para o build passar.

## 8. Pull requests e commits

- Um assunto por pull request. Não reformate arquivos que você não tocou.
- Mensagem de commit: uma linha curta de resumo, depois o que mudou e por
  quê, na voz neutra da seção 3.
- A descrição do PR diz: o que mudou, por quê, como foi testado (e o que não
  foi), qual finding documenta, e capturas para mudanças visíveis.
- Ajuda de IA é bem-vinda: diga isso na descrição. A pessoa que abre o PR é
  responsável por ter rodado no jogo real.
- Licença: o projeto é GPL-3.0, porque o PS2Recomp é GPL-3.0 (ver
  [findings/01](docs/findings/01-ps2recomp.md) s.5).

## 9. Mapa rápido

| Onde | O quê |
|---|---|
| `ben10_poe.toml` | config do recompilador: stubs, skips, tudo comentado |
| `CMakeLists.txt`, `CMakePresets.json` | o build: patches, codegen, jogo, testes |
| `src/` | o código nativo do port (vazio por enquanto; ver src/README.md) |
| `tests/` | GoogleTest; `tests/tools/make_test_elf.py` monta o ELF de teste |
| `tools/coletar_disco.ps1` | tira o ELF e os IRX do .iso para `game/` |
| `tools/ghidra_analisar.sh` / `.ps1`, `tools/ghidra_scripts/` | Ghidra headless: lista de funções para o recompilador |
| `tools/analyzer/ps2_elf_audit.py` | auditoria do ELF (seções, overlays de VU, instruções) |
| `tools/native_progress.py` | o painel e os marcos do README |
| `patches/ps2recomp/` | nossas correções do PS2Recomp |
| `thirdparty/PS2Recomp/` | submódulo: recompilador + runtime |
| `docs/findings/` | as notas de investigação |
| `docs/PROJECT_STATUS.md`, `NEXT_TASK.md`, `DECISIONS.md`, `KNOWN_ISSUES.md` | acompanhamento (master prompt s.9) |
