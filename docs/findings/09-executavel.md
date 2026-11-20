# 09: o executável principal (SLUS_216.61)

## 1. A pergunta

Fase 1 do master prompt: formato, ponto de entrada, layout de memória,
seções, distribuição de instruções e dependências de hardware do ELF.

## 2. Como foi medido

- `tools/coletar_disco.ps1` montou o ISO só para leitura e copiou
  `SYSTEM.CNF`, o ELF e os IRX para `Estudo\ISO\extraido\` (fora do git).
- `readelf -h -l -S` (binutils) para cabeçalhos.
- `tools/analyzer/ps2_elf_audit.py` (escrito para isto) para a tabela de
  overlays de VU, a classificação de instruções e as referências a
  hardware.
- Busca de strings ASCII (≥5 caracteres) por categorias. Só nomes e
  contagens vão para este finding.

Caminho errado: a primeira versão do script tirava os bits altos de
qualquer endereço e contou constantes `0x5000000x` como registradores de
timer (240 "acessos" falsos). Corrigido: só os segmentos 0x1, KSEG0
(0x8/0x9), KSEG1 (0xA/0xB) e o scratchpad (0x7) contam como hardware.
Depois da correção, TIMER caiu para 28.

## 3. Identidade do disco

| Campo | Valor |
|---|---|
| ISO | 3.400.499.200 bytes, SHA-1 `D212E3CBE2CF234B98C4D0CB35E0EDC555B83D6E` (ainda não comparado com o Redump) |
| `SYSTEM.CNF` | `BOOT2 = cdrom0:\SLUS_216.61;1`, `VER = 1.01`, `VMODE = NTSC` |
| Arquivos na raiz | `SLUS_216.61` (1,46 MB), `SYSTEM.CNF`, `GAME.DIR` (30 KB), `GAME.WAD` (2,46 GB), `IOPRP300.IMG` (275 KB), `MODULES\` (15 IRX) |

Revisão do disco: **1.01** (campo `VER`). O serial SLUS-21661 está
confirmado pelo nome do ELF.

## 4. Cabeçalho e memória

| Campo | Valor |
|---|---|
| Formato | ELF32, little-endian, `EXEC`, máquina MIPS (8) |
| Flags | `0x20924001`: `noreorder`, `5900`, `eabi64`, `mips3` |
| Ponto de entrada | `0x00100008` |
| Segmentos | **um só** `LOAD`: vaddr `0x00100000`, 0x1553A8 bytes no arquivo, 0x185FCC na memória (RWE) |
| Fim da imagem na RAM | `0x00285FCC` (~1,5 MB dos 32 MB) |
| `$gp` (`.reginfo`) | `0x0025D3F0` |
| Símbolos | **nenhum** (stripped); `.mdebug.eabi64` existe mas tem tamanho 0 |

Seções carregadas:

| Seção | Endereço | Tamanho | O que é |
|---|---|---|---|
| `.text` | 0x00100000 | 0x10FAE0 (1,06 MB) | código do EE |
| `.vutext` | 0x0020FAE0 | 0x0E4F0 (58 KB) | **microprogramas de VU guardados na RAM do EE** ([11](11-microprogramas-vu.md)) |
| `.data` | 0x0021E000 | 0x1AD98 | dados |
| `.vudata` | 0x00238D98 | 0 | vazio |
| `.rodata` | 0x00238E00 | 0x1C5A8 | constantes e strings |
| `.sbss` / `.bss` | 0x00255400 / 0x00255480 | 0x34 / 0x30B4C | zerados no boot |

Seções não carregadas: `.DVP.ovlystrtab`, 31 × `.DVP.overlay..*`,
`.DVP.ovlytab` (metadados dos microprogramas, ver [11](11-microprogramas-vu.md)).

## 5. Distribuição de instruções no `.text`

278.200 palavras de 32 bits.

| Categoria | Palavras | % |
|---|---|---|
| MIPS comum (opcode primário) | 164.489 | 59,1 |
| SPECIAL (ALU, shifts, JR/JALR, SYSCALL) | 64.989 | 23,4 |
| FPU (COP1 + LWC1/SWC1) | 33.169 | 11,9 |
| LQ/SQ (load/store de 128 bits) | 6.705 | 2,4 |
| COP2 (VU0 em modo macro + LQC2/SQC2/QMTC2/QMFC2) | 6.596 | 2,4 |
| MMI (SIMD de 128 bits) | 768 | 0,3 |
| REGIMM | 621 | 0,2 |
| COP0 | 278 | 0,1 |
| **Não decodifica** | 585 | 0,2 |

Pontos que importam para o recompilador:

- **64 bits é comum**: 11.969 `LD`, 12.409 `SD`, 19.226 `DADDU`; `LDL/LDR/SDL/SDR`
  ~800 cada (acesso desalinhado). A semântica de 64 bits precisa estar certa
  desde o começo.
- **Branches "likely"**: ~3.300 (`BEQL` 1.544, `BNEL` 1.550...). O delay slot
  só executa se o salto for tomado.
- **VU0 em modo macro**: 2.795 operações vetoriais, 1.721 `QMTC2`, 819
  `QMFC2`. O PS2Recomp declara suporte; precisa de teste.
- **VCALLMS**: 6 ocorrências. O jogo **dispara microprogramas no VU0**, não
  só no VU1. Ver [11](11-microprogramas-vu.md).
- **MMI**: 768, concentradas em `POR` (112, normalmente usado para copiar
  registradores de 128 bits), `PCPYLD`/`PCPYUD`, `PEXTLB`, adições
  empacotadas. Cerca de 60 mnemônicos diferentes.
- **FPU**: dominada por `MUL.S` (3.825), `ADD.S`, `SUB.S`, comparações;
  `DIV.S` 213, `SQRT.S` 85, `RSQRT.S` 36. Física e câmera dependem disso:
  o clamp da FPU do EE (sem infinito/NaN) é obrigatório.
- **Saltos indiretos**: 5.776 `JR` (retornos + tabelas de salto) e 1.568
  `JALR` (chamadas por ponteiro: tabelas virtuais e a API de script, s.7).
- **Funções**: 2.293 alvos únicos de `JAL`. É um **limite inferior**: as
  funções chamadas só por ponteiro não aparecem. O inventário real sai do
  Ghidra.
- `BREAK` 165: em código MIPS compilado costuma vir de checagem de divisão
  por zero; confirmar no Ghidra.
- `SYSCALL` 159: 131 deles são os stubs da `libkernl` (um por número de
  serviço, de 0 a 130, mais negativos para as versões de interrupção). A
  contagem não diz quais serviços o jogo usa; isso sai das chamadas a esses
  stubs.

### 5.1 As 585 palavras que não decodificam

A maioria aparece isolada (550 trechos, o maior com 14 palavras), em
especial perto do começo do `.text` (`0x00100328` em diante). Os padrões
(opcode 0x13, COP0 com campos inválidos, SPECIAL com funct 0x01/0x0E) e os
valores parecem **dados dentro do `.text`**, não instruções. Hipótese a
confirmar no Ghidra antes da Fase 2: o recompilador **não pode** traduzir
essas palavras como código.

## 6. Hardware referenciado diretamente

Pares `LUI` + offset que apontam para registradores (limite: só pega
endereços montados no próprio código; acessos por tabela de ponteiros não
aparecem).

| Bloco | Acessos | Observação |
|---|---|---|
| TIMER | 28 | contadores 0, 1, 3 |
| GS privilegiado (`0x12000000`) | 28 | PMODE, DISPFB/DISPLAY, CSR... (configuração de vídeo) |
| Scratchpad | 17 | uso direto de `0x70000000` |
| VIF1 (regs) | 15 | |
| VIF1 FIFO | 6 | escrita direta no FIFO do VIF1 |
| GIF (regs) | 6 | |
| VIF0 (regs / FIFO) | 3 / 1 | |

Nenhum acesso direto aos canais do DMAC por `LUI` (devem passar pela
`libdma`, por tabela). Strings confirmam o uso dos canais 1 e 2 (VIF1 e
GIF) e o reset dos canais do IPU.

## 7. Bibliotecas e strings

Etiquetas de versão de bibliotecas da Sony encontradas (formato
`PsIIlibXXXX NNNN`): `libdma`, `libpad`, `libmc`, `libvu0`, `libcdvd`,
`libkernl`, `libscf`, `libipu`. Também há mensagens da `libgraph`
(`sceGsPutDrawEnv`, `sceGsSyncPath`).

Strings relevantes (descritas, não copiadas por inteiro):

- Caminhos: `cdrom0:\GAME.DIR`, `\GAME.WAD`, `cdrom0:\MODULES\%s`,
  `cdrom0:\IOPRP300.IMG`, e um caminho para um ELF de uma pasta `SCE_DD`
  **que não existe neste disco** (código morto ou recurso de disco de
  demonstração; investigar se é chamado).
- Opções de debug confirmadas: `DEBUG_CHEATS`, `DEBUG_TEXT`, `DEBUG_FLY`,
  `DEBUG_SOUND`, `LEVEL_SELECT`, `DEMO_LEVEL_SELECT`, `START_LEVEL`,
  `LOAD_TO_START_LEVEL`, `LEVEL_WALK`.
- **1.583 nomes `ass_*`**, por exemplo `ass_SpawnFirst`,
  `ass_SetDialogTextScale`, `ass_GetEngineVersion`, `ass_GetBuildIdent`, 67
  `ass_net_*`. Parecem a **API exposta ao sistema de scripts** do motor (o
  jogo cita "Strats" e versões de "WAD" de mapa e de strats). Uma tabela
  nome→ponteiro daria nome a até 1.583 funções de uma vez. A investigar no
  Ghidra.
- Existe `ass_WiiDisplayHomeMenuBanned`: o código é **compartilhado com a
  versão de Wii**.
- O `GAME.WAD` tem versão checada pelo motor ("wad not supported by this
  engine build").
- Nenhuma string de vídeo (`.pss`, MPEG). A `libipu` está ligada, mas
  falta saber se há vídeos (provavelmente dentro do `GAME.WAD`).

## 8. Código carregado em tempo de execução

- O ELF tem **um único segmento** e não há overlays de código do EE
  (nenhuma seção de overlay além das de VU).
- O único carregamento de outro executável sugerido é o caminho `SCE_DD`,
  cujo arquivo não está no disco.
- Conclusão provisória: **todo o código do EE é estático**. O
  "Conflito B" de [06](06-conflitos-com-o-master-prompt.md) provavelmente
  não existe neste jogo. Confirmar procurando cópias de código para a RAM e
  `FlushCache` seguido de salto para dados.

## 9. Em aberto

- Comparar o SHA-1 com o Redump.
- ~~Inventário de funções~~, ~~585 palavras~~, ~~tabela `ass_*`~~: feitos em [12](12-analise-ghidra.md).
- Ver se o caminho `SCE_DD` é alcançável.
- Quais serviços do kernel são chamados (chamadas aos stubs de syscall).
