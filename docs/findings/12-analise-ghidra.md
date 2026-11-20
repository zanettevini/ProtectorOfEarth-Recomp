# 12: análise no Ghidra e inventário de funções

## 1. A pergunta

Fase 1: inventário de funções do `SLUS_216.61`, e se as ~585 palavras
suspeitas do `.text` ([09](09-executavel.md) s.5.1) são dados.

## 2. Como foi feito

- Ghidra 12.1.4 + ghidra-emotionengine-reloaded 12.1.4 (as mesmas versões
  baixadas pelo dono), em modo headless (`analyzeHeadless`), JDK 21.
  Executado no Linux; o equivalente para Windows é
  `tools/ghidra_analisar.ps1` (**não testado**).
- Processador: `r5900:LE:32:default` (o único que a extensão define).
- Análise automática padrão (42 s). A extensão rodou os analisadores
  próprios do R5900 (referências constantes, correção de acesso
  desalinhado, overlays DVP).
- Script novo `tools/ghidra_scripts/ApplyScriptApiNames.java` (s.4).
- Exportação com o `ExportPS2Functions.java` do PS2Recomp.

Caminho errado: a primeira passada do `ApplyScriptApiNames.java` foi com
`-noanalysis`. As funções novas foram criadas, mas as chamadas que saem
delas não foram seguidas. Uma segunda passada com análise não mudou a
contagem (4.420), então nada se perdeu; mas o procedimento certo é
reanalisar depois de criar funções.

## 3. Resultados da análise automática

| Medida | Valor |
|---|---|
| Funções (só análise automática) | 3.575 |
| Rótulos executáveis exportados | 16.423 |
| Funções da `libkernl` reconhecidas pela extensão | 42 stubs de syscall com nome (`CreateThread`, `WaitSema`, `AddDmacHandler`, `FlushCache`, `sceSifSetDma`...), em `0x001C49E0`-`0x001C5210` |

Os 42 stubs já saem como `stubs` no TOML do PS2Recomp: o runtime tem
tratador para cada um.

## 4. A tabela da API de script

- Em `0x002225B0`, no `.data`, há **1.586 pares de 32 bits** `{ponteiro
  para o nome, ponteiro para a função}`. 1.585 nomes começam com `ass_`;
  1 com `ass` sem sublinhado.
- Achada procurando, no segmento carregado, palavras que apontam para as
  strings `ass_*` ([09](09-executavel.md) s.7): 1.585 ponteiros, todos com
  passo de 2 palavras, e a palavra seguinte sempre dentro do `.text`.
- Cada alvo é único (1.586 alvos para 1.586 nomes).
- Antes do script, só 741 alvos eram funções no Ghidra; 629 eram apenas
  rótulos e 236 nem isso. São funções chamadas **só por ponteiro** (pelo
  interpretador de script do motor), que a análise automática não vê.
- `ApplyScriptApiNames.java`: **845 funções criadas, 1.586 nomeadas, 0
  falhas.**

## 5. Inventário final

| Medida | Valor |
|---|---|
| Funções | **4.420** (2.783 `FUN_*`, 1.586 da API de script, 51 com nome de biblioteca) |
| Tamanho mediano | 84 bytes |
| Maior função | 13.676 bytes (`FUN_00110850`; outras duas acima de 11 KB) |
| Cobertura do `.text` | 95,5% dos bytes estão dentro de alguma função |
| Lacunas | 2.168, somando 50 KB; a maior tem 20 KB (s.6) |

O CSV completo fica fora do git (`Estudo\ISO\ghidra_saida\`): é derivado
do binário do jogo.

## 6. A região de dados no começo do `.text`

- `0x001002E0`-`0x00105200` (20.256 bytes) não pertence a nenhuma função.
- **Todas as 585 palavras que não decodificam estão nela**, e nenhuma cai
  dentro de uma função.
- Conteúdo: 49% de palavras zero, 58% de valores menores que 0x10000,
  ~600 valores com padrão de `float` de 32 bits, nenhum `JAL` apontando
  para dentro.
- Conclusão: é uma **tabela de dados guardada no `.text`** (o que contém:
  a definir). O PS2Recomp só traduz o que está no CSV de funções, então ela
  já fica de fora. O risco R7 de [KNOWN_ISSUES](../KNOWN_ISSUES.md) fica
  mitigado.

## 7. Em aberto

- O que é a tabela de 20 KB (quem lê esses endereços).
- Conferir as três funções acima de 11 KB (podem ser várias funções
  coladas).
- As outras lacunas menores (2.167): alinhamento, ou funções ainda não
  descobertas.
- Testar o `tools/ghidra_analisar.ps1` no Windows.
