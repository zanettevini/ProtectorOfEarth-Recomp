# 06: conflitos entre o master prompt e a prática dos outros ports

## 1. A pergunta

O master prompt proíbe interpretador de CPU, dynarec e emulador escondido
(s.3, s.10). Os ports de referência ([02](02-projetos-de-referencia.md))
usam interpretador em três lugares. O que fazer em cada um?

## 2. Conflito A: o IOP

- **O que os outros fazem**: o `ps2xIOP` do PS2Recomp e o Gitaroo Man
  executam os módulos IRX originais num R3000A emulado.
- **Por que conflita**: o IOP é uma CPU do PS2. Executar o código dele por
  interpretação é "interpretador de CPU do PS2".
- **Caminho compatível**: o do BT2 Recomp. Não rodar IRX; implementar em C++
  nativo os **serviços** que o jogo pede ao IOP por RPC (leitura de disco,
  sistema de arquivos, controle, memory card, som). O master prompt permite
  implementação nativa de subsistemas (s.3).
- **Proposta**: adotar. Custo: precisamos descobrir cada RPC que o jogo usa
  (Fase 1).

## 3. Conflito B: código do EE gerado em tempo de execução

- **O que os outros fazem**: o Gitaroo Man cai num interpretador de EE.
- **Caminho compatível**: primeiro medir se o Ben 10 faz isso. Se fizer,
  normalmente é código conhecido (um overlay no disco, ou um trecho copiado
  para outro endereço). Overlays do disco podem ser recompilados
  estaticamente no build, como o BT2 faz com o overlay dele. Código
  realmente gerado na hora é bloqueio documentado (master prompt s.3).
- **Proposta**: medir antes; recompilar overlays estaticamente.

## 4. Conflito C: microprogramas de VU

- **O que os outros fazem**: o Gitaroo Man recompila o microprograma de VU1
  para C++ e mantém um interpretador de VU1 como alternativa e como oráculo
  de testes (comparação lado a lado).
- **Por que conflita em parte**: a recompilação é exatamente o que o master
  prompt pede (Fase 3). O que conflita é deixar o interpretador como
  alternativa no executável.
- **Proposta**: microprogramas traduzidos estaticamente no build. Um
  interpretador de VU **só no alvo de testes** (`tests/`), para comparar,
  nunca no executável do jogo. Se o jogo enviar um microprograma que não foi
  traduzido, o runtime para com erro e o hash do programa (não fazer
  fallback silencioso).

## 5. O mesmo raciocínio para o EE

Para validar instruções do EE (Fase 2) precisamos de uma referência. Opções:
capturas do PCSX2, ou um interpretador mínimo **só em testes**. A mesma
regra: nunca no executável final.

## 6. Decisão

O dono pediu o caminho mais fácil por enquanto. Resultado
([DECISIONS](../DECISIONS.md) D2, D3):

- A (IOP): `ps2xIOP` executando os IRX, **temporário**. Volta para serviços
  nativos antes da Fase 8.
- B (código em tempo de execução): **não existe neste jogo**, ao que tudo
  indica ([09](09-executavel.md) s.8). Nada a fazer.
- C (VU): qualquer implementação disponível para o primeiro frame,
  **temporário**; a tradução estática é viável ([11](11-microprogramas-vu.md)).

Todo uso temporário de interpretação fica como dívida em
[KNOWN_ISSUES](../KNOWN_ISSUES.md) e aparece no log.
