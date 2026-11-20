# Como o port funciona

Se for ler um documento só, leia este.

## O problema

O `SLUS_216.61` é código de máquina para a CPU principal do PlayStation 2,
o **Emotion Engine** (EE): um MIPS modificado, com registradores de 128
bits, instruções SIMD próprias (MMI) e uma unidade vetorial acoplada (VU0).
O seu PC tem uma CPU x86-64, que não entende essas instruções. O jogo
também espera o resto do console:

* o **IOP**, uma segunda CPU que cuida do disco, controle, memory card e
  som, rodando módulos próprios (`.IRX`);
* as **unidades vetoriais** VU0 e VU1, que rodam microprogramas próprios
  (a VU1 transforma a geometria 3D antes de desenhar);
* o **GS** (Graphics Synthesizer), o chip gráfico, com 4 MB de memória
  própria, alimentado por pacotes de comandos;
* o **SPU2** (som), **DMA**, temporizadores e interrupções.

## Três jeitos de resolver

| Jeito | O que é | Exemplos |
|---|---|---|
| **Emulação** | traduzir as instruções *enquanto o jogo roda* | PCSX2 |
| **Decompilação** | pessoas reescrevem o jogo inteiro em C/C++, função por função | Super Mario 64, OpenGOAL (Jak) |
| **Recompilação estática** | uma ferramenta traduz *todas* as funções para C++ *antes*; o resultado compila num `.exe` nativo | Mind over Recomp, Gitaroo Man Native, **este projeto** |

Usamos **recompilação estática**: o jogo roda nativo cedo, e depois as
peças emuladas são trocadas por código próprio aos poucos (renderizador,
som, controles, melhorias). Uma decompilação pode vir depois, função por
função, sobre um jogo que já roda.

## O caminho

```
 Seu disco (.iso)
      |
      |  tools/coletar_disco.ps1 (ou bsdtar)
      v
 game/SLUS_216.61  (o executável)   game/MODULES/*.IRX, GAME.DIR, GAME.WAD
      |                                   |
      |  Ghidra + extensão do EE          |  lidos pelo runtime quando o
      |  + ApplyScriptApiNames.java      |  jogo roda, sem mudança
      |  (tools/ghidra_analisar.*)        |
      v                                   |
 generated/ghidra/functions.csv           |
 (onde começa e termina cada função)      |
      |                                   |
      |  ps2_recomp + ben10_poe.toml      |
      |  (o tradutor, com nossos patches) |
      v                                   |
 generated/default/*.cpp  (o código do jogo, como C++)
      |                                   |
      |  compilador C++ comum             |
      |  + runtime do PS2Recomp           |
      |  + nosso código em src/           |
      v                                   |
 ben10_poe  (programa nativo) <-----------+
```

### O que o tradutor faz

1. Lê o ELF: um segmento só, carregado em `0x00100000` (findings/09).
2. Para cada função do CSV, decodifica cada instrução e escreve o C++
   equivalente. Os registradores do EE viram campos de uma estrutura
   (`ctx->r[]`, 128 bits cada); a memória do PS2 (32 MB) é um vetor de bytes
   (`rdram`). Um exemplo: `addu $v0, $a0, $a1` vira uma soma de 32 bits com o
   resultado estendido para 64 bits, como no PS2.
3. Desvios com *delay slot* (a instrução depois de um desvio executa antes
   de ele acontecer) viram `goto` dentro da função.
4. Saltos que só se conhecem rodando (tabelas, ponteiros de função) passam
   por uma tabela de despacho: endereço do PS2 → função C++ já compilada.
   Nada é interpretado.
5. `SYSCALL` vira uma chamada ao runtime, que responde como o kernel do PS2.

### O que o runtime faz

O runtime do PS2Recomp é a parte "PS2" em volta do código traduzido:
memória, kernel (threads, semáforos), DMA, VIF/GIF, um GS em software, VU0 e
VU1, o IOP e o som. Hoje quase tudo isso ainda imita o hardware, e duas
partes por interpretação (o IOP e as VUs: dívidas temporárias T1 e T2 em
docs/KNOWN_ISSUES.md). O painel "Rumo ao nativo" do README mede quanto já
foi trocado por código do port.

## Como se confere que está certo

- **Instrução por instrução**: os testes de `tests/` montam um ELF pequeno,
  passam pelo mesmo tradutor e comparam o resultado com o comportamento do
  PS2 (PCSX2 como referência). Foram eles que acharam os 4 primeiros erros
  do tradutor (findings/14).
- **O jogo inteiro**, mais tarde: comparar com o PCSX2 o estado da memória e
  as imagens do mesmo quadro.
