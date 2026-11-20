# 11: microprogramas de VU dentro do ELF

## 1. A pergunta

O master prompt (Fase 3) pede para investigar se os microprogramas de VU0
e VU1 podem ser identificados e traduzidos estaticamente no build.

## 2. Resposta curta

**Sim, e este jogo facilita.** Os microprogramas estão no próprio ELF,
numa seção `.vutext`, e a toolchain deixou uma **tabela de overlays**
dizendo onde cada pedaço está na RAM do EE e para qual endereço da
micro-memória da VU ele vai.

## 3. Como foi achado

`readelf -S` mostrou seções `.vutext`, `.DVP.ovlystrtab`, `.DVP.ovlytab` e
31 seções `.DVP.overlay..*`. "DVP" é o nome que a toolchain do PS2 usa para
a parte de VU/VIF. O layout da tabela foi deduzido dos bytes e conferido
pela coerência dos valores (s.4); está implementado em
`tools/analyzer/ps2_elf_audit.py` (`parse_dvp_overlays`).

## 4. O que é

- `.vutext`: 58 KB em `0x0020FAE0`-`0x0021DFD0`, dentro do segmento
  carregado. É de lá que o jogo copia (por DMA/VIF, comando `MPG`) o código
  para a VU.
- `.DVP.ovlytab`: 32 entradas de 12 bytes:
  - +0x0: deslocamento do nome em `.DVP.ovlystrtab`;
  - +0x4: **LMA**, o endereço no EE (todos caem dentro do `.vutext`);
  - +0x8: **VMA**, o endereço de destino na micro-memória da VU.
- As 31 seções `.DVP.overlay..*` (fora do segmento carregado) têm os mesmos
  tamanhos dos pedaços; a entrada 12 e a 22 usam a mesma seção, por isso
  são 32 entradas para 31 seções.

As entradas formam **grupos** que começam em VMA 0 e sobem de 0x800 em
0x800 (2 KB por pedaço):

Tamanho = distância entre o LMA do grupo e o LMA do grupo seguinte (o
último vai até o fim do `.vutext`). Caminho errado: a primeira versão desta
tabela usou o tamanho das seções `.DVP.overlay`, que não bate para a seção
compartilhada pelas entradas 12 e 22.

| Grupo | Entradas | VMA máxima | Tamanho | Leitura |
|---|---|---|---|---|
| A | #00 | 0x0000 | 0x830 (2,0 KB) | cabe no VU0 (4 KB) ou no VU1 |
| B | #01 | 0x0000 | 0x260 (0,6 KB) | idem |
| C | #02 | 0x0000 | 0x3A0 (0,9 KB) | idem |
| D | #03-#10 | 0x3800 | 0x3BB0 (14,9 KB) | **só cabe no VU1** (16 KB) |
| E | #11-#12 | 0x0800 | 0xD00 (3,3 KB) | cabe no VU0 ou no VU1 |
| F | #13-#20 | 0x3800 | 0x3CB0 (15,2 KB) | **só VU1** |
| G | #21-#22 | 0x0800 | 0xD00 (3,3 KB) | cabe no VU0 ou no VU1 |
| H | #23-#28 | 0x2800 | 0x2CC0 (11,2 KB) | **só VU1** |
| I | #29-#31 | 0x1000 | 0x1798 (5,9 KB) | **só VU1** |

Conclusão: há pelo menos quatro programas de VU1 grandes (D, F, H, I) (provavelmente variações
do renderizador: modelos com/sem pele, partículas etc., **nomes a
definir**) e cinco pequenos (A, B, C, E, G) que podem ser do VU0 ou do VU1. O EE tem 6 `VCALLMS`
([09](09-executavel.md) s.5), então o VU0 em modo micro **é usado**.

Os nomes das seções são gerados pela toolchain (contêm números, não nomes
legíveis), então não ajudam a dizer o que cada programa faz.

## 5. O que isso muda no plano

- A lista de microprogramas é **fechada e conhecida no build**: um
  tradutor estático de VU (o "vu1lift" do Gitaroo Man, ou um próprio)
  pode ler a `.DVP.ovlytab`, extrair cada grupo do `.vutext` e gerar C++.
  Não é preciso census em tempo de execução para descobrir os programas;
  o census só serve para confirmar.
- No runtime, quando chegar um `MPG` do VIF, o código enviado pode ser
  reconhecido pelo endereço de origem (LMA) ou por hash, e ligado à versão
  traduzida. Programa desconhecido = parar com erro (D3).
- Faltam: o decodificador de instruções de VU (par upper/lower de 64 bits),
  a semântica de flags (MAC, status, clipping), o pipeline (latência do
  `FMAC`, `Q`, `P`) e os pontos de entrada (endereços que o jogo usa em
  `MSCAL`/`MSCNT`/`VCALLMS`).

## 6. Em aberto

- Confirmar cada grupo como VU0 ou VU1 (pelo canal de DMA que o envia:
  VIF0 ou VIF1).
- Listar os pontos de entrada usados pelo EE.
- Medir quantas instruções de VU há no total.
