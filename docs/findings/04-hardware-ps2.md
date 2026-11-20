# 04: mapa do hardware do PS2 e o que cada parte exige

## 1. A pergunta

Quais peças do PS2 o runtime precisa imitar, e qual é a parte difícil de
cada uma?

## 2. Fonte

Conhecimento geral e público sobre o PS2. Os números são os amplamente
conhecidos; **antes de implementar, confirmar cada detalhe no código do
PCSX2** (referência do master prompt) e anotar o arquivo de onde veio.

## 3. As peças

| Peça | O que é | No port | Dificuldade |
|---|---|---|---|
| **EE (R5900)** | CPU principal MIPS III modificada, registradores de 128 bits, ~295 MHz | recompilada (PS2Recomp) | instruções MMI de 128 bits, FPU fora do padrão IEEE, delay slots, saltos indiretos |
| **FPU (COP1)** | ponto flutuante de 32 bits | recompilada | não tem infinito/NaN e arredonda diferente do x86: precisa de emulação de clamp em cada operação |
| **COP0** | controle do sistema, exceções, contadores | runtime nativo | só o que o jogo usar |
| **RAM** | 32 MB, vista por vários espelhos de endereço | runtime | máscara de endereço correta |
| **Scratchpad** | 16 KB de memória rápida em `0x70000000` | runtime | trivial |
| **VU0** | unidade vetorial; modo macro (COP2, instruções no fluxo do EE) e modo micro | macro: PS2Recomp; micro: a verificar | se o jogo usa VU0 em modo micro, precisa de tradução estática dos microprogramas |
| **VU1** | unidade vetorial que geralmente faz transformação de vértices e manda geometria para o GS | **tradução estática dos microprogramas** | o maior desafio: pipeline, flags, saltos, sincronização com VIF1/GIF |
| **VIF0/VIF1** | desempacotam dados e microprogramas para as VUs | runtime nativo | formatos de unpack, máscaras |
| **GIF** | caminho de dados até o GS (PATH1/2/3) | runtime nativo | arbitragem entre caminhos |
| **GS** | rasterizador com 4 MB de eDRAM | Vulkan ([05](05-renderizador-vulkan.md)) | formatos de textura, reinterpretação da memória, blending, feedback de framebuffer |
| **DMAC** | 10 canais de DMA | runtime nativo | cadeias de tags, ordem e tempo |
| **IPU** | decodificador MPEG-2 para vídeos | runtime nativo (ou decodificador de vídeo nativo) | só se o jogo usar vídeos MPEG |
| **IOP** | segunda CPU (R3000A) com 2 MB de RAM, controla disco, controle, memory card, som | **serviços nativos HLE**, sem rodar IRX ([06](06-conflitos-com-o-master-prompt.md)) | descobrir quais serviços RPC o jogo chama |
| **SIF** | ponte EE↔IOP (RPC) | runtime nativo | protocolo de RPC |
| **SPU2** | chip de som, 48 vozes, 2 MB de memória de som, ADPCM | nativo | streaming e tempo |
| **Timers / VSync / INTC** | contadores e interrupções | runtime nativo | o jogo costuma depender do ritmo de VBlank |

## 4. O que isso diz sobre a ordem do trabalho

- A Fase 1 deve medir, no ELF: uso de MMI, uso de COP2 (VU0 macro), presença
  de microprogramas de VU (procurar uploads via VIF `MPG`), uso de IPU, lista
  de IRX e de RPCs. Só então decidir o que implementar (master prompt Fase 4:
  "não implementar subsistemas inteiros sem evidência").
- A ordem natural: CPU/memória → syscalls e threads do kernel → SIF/serviços
  de IOP (arquivo, controle) → DMA/VIF/GIF → GS → VU1 → áudio → vídeo.

## 5. Em aberto

- Fazer a medição da s.4 com o ELF real.
- Anotar, para cada item, os arquivos de referência no código do PCSX2.
