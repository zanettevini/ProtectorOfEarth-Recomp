# 13: primeira recompilação com o PS2Recomp

## 1. A pergunta

O PS2Recomp consegue traduzir o executável inteiro? Quais instruções ele
não suporta? O C++ gerado compila?

## 2. Como foi feito

- PS2Recomp clonado do `ran-j/PS2Recomp`, commit `e563698`.
- Só o recompilador compilado (`-DPS2X_BUILD_RECOMP=ON`, o resto `OFF`),
  CMake + Ninja + GCC 13, Linux. Compilou sem erros.
- Entrada: o TOML e o CSV exportados do Ghidra ([12](12-analise-ghidra.md)).
- Saída: C++ num diretório local fora do git (é o código do jogo).

Caminho errado: configurar só o subdiretório `ps2xRecomp` falha
(`EnableFastReleaseMode` é definido no `CMakeLists.txt` da raiz).
Configurar a raiz com as opções `PS2X_BUILD_*` funciona.

## 3. Resultado do recompilador

| Medida | Valor |
|---|---|
| Entradas processadas | 20.234 (as 4.420 funções + rótulos executáveis do CSV) |
| Recompiladas | 20.192; stubs: 42 (os syscalls) |
| Falhas de decodificação | **0** |
| **Instruções não suportadas** | **0** |
| Erros | 0 |
| Avisos | 2.016, todos do tipo "JR/JALR não resolvido" |
| Pontos de entrada retomáveis | 86.057 (para continuar no meio de uma função depois de um salto indireto) |
| Tempo | ~2 s |
| Saída | 20.237 arquivos C++, 158 MB |

Os 2.016 avisos são saltos indiretos (`JR` por tabela e `JALR`) que a
análise estática não resolveu. O recompilador gera **entradas de despacho
por endereço** para eles (busca em tabela de funções já compiladas), sem
interpretar. Ainda é preciso conferir em execução se todo alvo real existe
nessa tabela.

## 4. O C++ gerado compila?

Teste de sintaxe (`g++ -std=c++20 -msse4.1 -mavx -fsyntax-only`) contra os
cabeçalhos do `ps2xRuntime`:

- 12 arquivos aleatórios: **12/12 ok**.
- 21 arquivos escolhidos por conterem as instruções mais específicas do
  PS2 (VCALLMS, VU0 macro, QMTC2, MMI como PCPYLD/PEXTLB/MTSAB, LQ/SQ,
  MADD.S, RSQRT.S): **21/21 ok**.

Não testado: compilar e ligar todos os 20.237 arquivos (pede muita RAM e
tempo), nem rodar o código.

## 5. Como o VU0 em modo micro é tratado

`VCALLMS` vira `runtime->executeVU0Microprogram(...)`. No runtime atual
essa função executa o microprograma do VU0 pela implementação de VU do
próprio PS2Recomp (`ps2xRuntime/src/lib/vu/`, por interpretação). Há
também implementação de VU1 (`ps2_vu1_core.cpp` e afins). Isso
**corrige** [01](01-ps2recomp.md) s.4: a documentação lida antes dizia que
só havia VU0 em modo macro. É a dívida temporária T2.

## 6. Registrador `gp`

O relatório do ELF mostra `gp=0x0`, mas o `.reginfo` declara
`0x0025D3F0`. O código de entrada (crt0) gerado escreve o `gp` ele mesmo,
então isso não deve atrapalhar. Confirmar na primeira execução.

## 7. Em aberto

- Compilar e ligar tudo com o `ps2xRuntime` no Windows (MSVC).
- Primeira execução: até onde o boot chega.
- Conferir os alvos dos 2.016 saltos indiretos em execução.
- Testes por instrução (master prompt, Fase 2): nenhum escrito ainda.
