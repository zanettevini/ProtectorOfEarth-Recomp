# 14: o repositório, o build completo e os primeiros testes por instrução

## 1. A pergunta

Fase 2 do master prompt: o C++ gerado compila e liga inteiro com o runtime?
As instruções traduzidas fazem o que o PS2 faz?

## 2. O repositório

- Organização copiada do Mind over Recomp (MoR): `ben10_poe.toml` no lugar
  do `crash_mom_manifest.toml`, `patches/ps2recomp/` no lugar de
  `patches/rexglue-sdk/`, `generated/` local, `docs/findings/`,
  `tools/native_progress.py` para o painel do README. Decisão D9.
- PS2Recomp como submódulo em `thirdparty/PS2Recomp`, fixado no commit
  `e563698` (D8).
- Caminho errado: o primeiro configure falhou com "include could not find
  requested file: <projeto>/ps2xRuntime/cmake/ReleaseMode.cmake". Quatro
  `CMakeLists.txt` do PS2Recomp usam `${CMAKE_SOURCE_DIR}`, que só aponta
  para a raiz dele quando ele é o projeto principal. Correção no patch
  `0001` (`${CMAKE_CURRENT_LIST_DIR}/..`).
- Os patches são aplicados pelo próprio `CMakeLists.txt` no configure, em
  ordem, só se ainda não estiverem (testa com `git apply --reverse
  --check`). Testado: a partir do submódulo limpo no commit fixado, o
  primeiro configure aplica os 5; o segundo não aplica nada.

## 3. O build completo

- Alvo `ben10_codegen`: roda o `ps2_recomp` com `ben10_poe.toml`. Mesmo
  resultado do [13](13-primeira-recompilacao.md): 0 instruções não
  suportadas, 0 erros.
- Alvo `ben10_poe`: os ~20 mil `.cpp` gerados + o `main` do runner do
  PS2Recomp + `ps2_runtime`. Unity build (32 arquivos por unidade) e
  cabeçalhos pré-compilados, como o `ps2EntryRunner` do PS2Recomp.
- Caminho errado: o primeiro build parou com "inlining failed in call to
  always_inline '_mm_blendv_ps': target specific option mismatch". O código
  gerado usa SSE4.1; o PS2Recomp só liga flags de CPU no modo Release, e o
  MSVC não precisa delas. Correção: `-march=x86-64-v2` para GCC/Clang no
  nosso `CMakeLists.txt` (a mesma escolha do Gitaroo Man Native).
- Resultado (Linux, GCC 13, `-O0`, 2 núcleos): **737 unidades em 4 min
  34 s, executável ligado** (116 MB com informação de debug). Depois dos
  patches 0002-0005, o jogo foi regenerado e compilado de novo (s.6).
- **Não testado**: build no Windows com MSVC; build otimizado; executar.

## 4. Os testes por instrução

- `tests/tools/make_test_elf.py` monta um ELF de PS2 com uma função pequena
  por caso (nenhum dado do jogo), mais o CSV e o TOML. O `ps2_recomp` de
  verdade traduz; o GoogleTest (`tests/instructions/`) prepara os
  registradores, chama a função gerada e confere.
- Isso testa o **texto C++ que o tradutor escreve** para cada instrução,
  não só as macros do runtime.
- A codificação de cada instrução foi conferida pelo disassembly que o
  próprio `ps2_recomp` escreve no código gerado.
- Valores esperados: o comportamento do PS2 no manual do R5900 e no
  interpretador do PCSX2 (commit `15afef6`), citado caso a caso.
- 34 casos: inteiros de 64 bits (ADDU, DADDU, DSRA32), LQ/SQ, delay slots
  (BEQ, BEQL, BNEL), FPU (CVT.W.S, ADD/SUB/MUL/DIV.S, SQRT.S), MMI (PCPYLD,
  PCPYUD, PEXTLW, PADDSH, PADDUH, PSUBUH, PPACH, PNOR, PLZCW), VU0 macro
  (VADD com máscara de destino). Um desligado de propósito (s.7).

## 5. O que os testes acharam

Primeira execução, antes de qualquer correção: **24 passaram, 8
falharam.** Cada falha foi conferida no código gerado antes de culpar o
tradutor:

| Falha | Causa | Afeta o Ben 10? |
|---|---|---|
| `LqIgnoresLowFourBits`, `SqIgnoresLowFourBits` | o tradutor passa `rs + imm` direto para `READ128`/`WRITE128`, que não alinham; no PS2, LQ/SQ ignoram os 4 bits baixos (PCSX2 `LQ`/`SQ`: `addr & ~0xf`) | 6.705 LQ/SQ; só importa quando o endereço é desalinhado |
| `CvtWsTruncatesTowardZero`, `...Negative...`, `CvtWsSaturatesPositive` | `FPU_CVT_W_S` era `(int32_t)nearbyintf(x)`: arredonda pelo modo atual (o mais próximo), e 3e9 dá `0x80000000` no x86. O PS2 trunca e satura (PCSX2 `CVT_W`). Só o código de VU troca o modo de arredondamento | **566 CVT.W.S** |
| `SqrtsReadsFt`, `SqrtsOfNegativeUsesAbsoluteValue` | o tradutor lê o operando de `fs`; no R5900 o SQRT.S lê de `ft` (PCSX2 `SQRT_S` usa `_FtValUl_`). Os 85 SQRT.S do jogo têm `fs = 0`, então **todos** liam `$f0` | **31 dos 85 SQRT.S** usam outro registrador e davam resultado errado |
| `PsubuhClampsAtZero` | PSUBUH virava `_mm_sub_epi16` (dá a volta); no PS2 satura em 0 (PCSX2 `_PSUBUH`). PADDUH tinha o mesmo erro (`_mm_add_epi16`) | 27 PSUBUH |

Achado lendo as linhas vizinhas: PADDUB/PSUBUB já usavam as instruções SSE
com saturação; só as de 16 bits estavam erradas.

Correções, cada uma num patch com cabeçalho (o que quebrou, por quê, a
correção, como foi achado):

| Patch | O quê |
|---|---|
| `0002-lq-sq-align-address.patch` | `(rs + imm) & ~0xF` em LQ/SQ |
| `0003-sqrt-s-reads-ft.patch` | `FPU_SQRT_S(ctx->f[ft])` |
| `0004-cvt-w-s-truncate-and-saturate.patch` | `ps2_cvt_w_s()`, com a regra do PCSX2 |
| `0005-padduh-psubuh-saturate.patch` | `_mm_adds_epu16` / `_mm_subs_epu16` |

Depois: **33 passaram, 0 falharam, 1 desligado.** O caso novo `PadduhSaturates`
foi escrito junto com o patch 0005 e passou de primeira.

## 6. No jogo de verdade

Depois dos patches, o código do jogo foi regenerado: os SQRT.S agora leem
`f[12]`, `f[2]`, `f[1]`... em vez de sempre `f[0]`, e o jogo compilou de
novo.

Estes erros **não fariam o jogo travar**: dariam números errados (raízes,
conversões para inteiro, máscaras de 16 bits) que apareceriam como física,
câmera ou animação estranhas, difíceis de achar rodando o jogo. É o motivo
de o master prompt pedir testes por instrução antes da primeira execução.

## 7. O que ficou de fora (aberto)

- **Operando denormal** (expoente 0, mantissa diferente de 0) em
  ADD/SUB/MUL.S: o PS2 trata como zero (PCSX2 `fpuDouble`); o runtime do
  PS2Recomp ainda não, e diz isso num comentário. O teste
  `DISABLED_MulsDenormalOperandIsZero` existe e está desligado de
  propósito: é uma diferença conhecida (KNOWN_ISSUES R10), não um teste
  enfraquecido.
- **LQC2/SQC2** (790 e 445 no jogo): também não alinham o endereço, mas o
  PCSX2 também não alinha esses dois. Sem evidência, não mexemos
  (KNOWN_ISSUES R11).
- **SQRT.S de -0**: o PCSX2 devolve -0; o runtime devolve +0 e diz que
  conferiu no hardware (ps2autotests). Sem teste até haver árbitro
  (KNOWN_ISSUES R12).
- Os mesmos 4 erros provavelmente existem no PS2Recomp original: vale
  enviar os patches para o projeto (ran-j/PS2Recomp).
- Cobertura: 22 das ~60 instruções R5900 específicas que o jogo usa têm
  teste. Faltam, entre outras: QFSRV, PMFHL, PEXT5/PPAC5, MADD.S/MSUB.S,
  RSQRT.S, LDL/LDR/SDL/SDR, MULT1/DIV1, as comparações C.*.S, e o resto das
  operações de VU0 macro.
