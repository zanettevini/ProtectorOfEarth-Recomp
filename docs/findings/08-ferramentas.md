# 08: ferramentas de análise e referência

## 1. Ghidra + Emotion Engine: Reloaded

- Extensão [chaoticgd/ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded)
  (Apache-2.0). Baseada na antiga `beardypig/ghidra-emotionengine`, que foi
  arquivada.
- Desmonta e decompila instruções próprias do EE (MMI, VU0 em modo macro).
- Recupera tipos, funções e variáveis globais quando o ELF tem seção
  `.mdebug` (analisador STABS).
- **Importa save states do PCSX2**: permite olhar a memória do jogo rodando,
  inclusive código carregado depois do boot.
- Analisador de referências constantes do R5900 conserta referências a
  globais.
- A versão da extensão precisa casar com a versão do Ghidra.
- O PS2Recomp traz o script `ExportPS2Functions.java` que exporta a lista de
  funções do Ghidra para o recompilador ([01](01-ps2recomp.md)).

## 2. PCSX2 como referência

- Só referência (master prompt s.2). Usos: comparar estado da CPU e da
  memória, capturas do GS (dump de frames do GS), save states para o Ghidra,
  saves de memory card para chegar a fases avançadas.
- O Gitaroo Man usa um cliente **PINE** (protocolo de IPC do PCSX2) para ler
  memória do jogo rodando. Vale copiar a ideia.
- Licença GPL-3.0: ler o código como referência é permitido; copiar trechos
  exige manter a licença e o crédito.

## 3. Extração do disco

- O ISO é um sistema de arquivos ISO 9660 / UDF comum: dá para abrir com
  7-Zip. O `SYSTEM.CNF` na raiz diz o nome do ELF de boot.
- O resultado fica em `iso/` (ignorado pelo git) e **nunca** é modificado.
- Ferramenta planejada: `tools/extract_iso.py`, que extrai, confere o hash do
  ISO contra o Redump e grava um manifesto (nomes, tamanhos, hashes) em
  `out/`, sem conteúdo do jogo.
