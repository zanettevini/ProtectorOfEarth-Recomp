# Auditoria inicial (Fases 0 e 1)

Resumo do que foi medido até agora. Os detalhes e os métodos estão nos
findings; este arquivo é o relatório para ler primeiro.

## 1. Ambiente ([findings/07](findings/07-ambiente-fase0.md))

| Ferramenta | Situação |
|---|---|
| Windows 11, Git, CMake 4.4, Ninja, Python 3.12, Vulkan SDK 1.4 | ok |
| MSVC (Build Tools 2022, C++ x64) | ok, usar o "Developer PowerShell for VS 2022" |
| JDK 21 + Ghidra 12.1.4 + extensão EE | baixados |
| PCSX2 (referência) | não verificado |

## 2. O disco ([findings/09](findings/09-executavel.md) s.3)

SLUS-21661, revisão 1.01, NTSC. Um ELF (`SLUS_216.61`, 1,46 MB), 15 IRX,
`IOPRP300.IMG`, e os dados em `GAME.DIR` (índice) + `GAME.WAD` (2,46 GB).

## 3. O executável ([findings/09](findings/09-executavel.md))

- ELF32 MIPS R5900, entrada `0x00100008`, um único segmento de
  `0x00100000` a `0x00285FCC`, sem símbolos.
- 278.200 palavras de código; ≥2.293 funções (só contando `JAL`).
- Muito código de 64 bits, ~3.300 branches "likely", 6.705 LQ/SQ de 128
  bits, 768 MMI, 6.596 operações COP2 (VU0 macro), 33.169 de FPU.
- **Todo o código do EE parece estático**: nenhum overlay de código.
- ~585 palavras de dados dentro do `.text` precisam ser marcadas.
- 1.583 nomes `ass_*` (API de script do motor) podem dar nome a muitas
  funções.

## 4. IOP ([findings/10](findings/10-modulos-iop.md))

8 módulos carregados: sio2man, multitap, padman, mcman, mcserv, libsd e
dois próprios do motor, `ALASD` (driver de áudio "Argonaut") e `CDARB`
(arbitragem de disco).

## 5. VU ([findings/11](findings/11-microprogramas-vu.md))

Os microprogramas estão no `.vutext` (58 KB), com uma tabela de 32
pedaços (origem no EE → destino na VU). Pelo menos 4 programas grandes só
cabem no VU1. O EE também dispara microprogramas no VU0 (`VCALLMS`).
**Tradução estática viável.**

## 6. Dependências de hardware

| Peça | Usada? | Evidência |
|---|---|---|
| EE + FPU + MMI + VU0 macro | sim | contagens da s.3 |
| VU0 modo micro | sim | 6 `VCALLMS` |
| VU1 | sim | programas de até 16 KB; mensagens da `libgraph` sobre VU1 |
| VIF0, VIF1, GIF | sim | acessos diretos a registradores e FIFOs |
| GS | sim | registradores privilegiados (configuração de vídeo) |
| DMAC | sim | `libdma`; canais 1 e 2 citados |
| IPU | provável | `libipu` ligada, reset dos canais do IPU; vídeos não confirmados |
| Timers, scratchpad | sim | acessos diretos |
| SPU2 | sim, via IOP | `LIBSD` + `ALASD` |
| Memory card, controle, multitap | sim, via IOP | módulos carregados |
| USB / teclado USB | provavelmente não | módulos no disco, não citados |

## 7. Ghidra e primeira recompilação ([findings/12](findings/12-analise-ghidra.md), [13](findings/13-primeira-recompilacao.md))

- **4.420 funções**, 95,5% do `.text` coberto. 845 delas só apareceram
  lendo a tabela da API de script (1.586 nomes do próprio jogo).
- As 585 palavras suspeitas estão todas numa tabela de dados de 20 KB
  fora de qualquer função.
- PS2Recomp: **0 instruções não suportadas**, 0 falhas; 2.016 saltos
  indiretos ficam para despacho por endereço. Amostra do C++ gerado (33
  arquivos, incluindo os mais específicos do PS2) compila.

## 8. Lista de tarefas priorizada

1. ~~Ghidra~~, ~~inventário~~, ~~tabela `ass_*`~~, ~~primeira recompilação~~: feitos.
2. Criar o repositório git e adicionar o fork do PS2Recomp (commit
   `e563698`); compilar com MSVC; rodar os testes dele.
3. Compilar todo o C++ gerado e ligar ao `ps2xRuntime`.
4. Testes por instrução para as categorias mais usadas e mais arriscadas
   (64 bits, likely, FPU com clamp, LQ/SQ, MMI usadas, VU0 macro).
5. Primeira execução com `ps2xIOP` (D2 temporário): até onde o boot chega.
