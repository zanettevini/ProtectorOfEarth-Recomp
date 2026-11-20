#!/usr/bin/env bash
# ghidra_analisar.sh: análise headless do Ghidra e exportação da lista de
# funções que o ps2_recomp lê (docs/findings/12). Versão Linux/macOS de
# tools/ghidra_analisar.ps1.
#
# Passos:
#   1. instala a extensão ghidra-emotionengine-reloaded no Ghidra (descompacta
#      o .zip em Ghidra/Extensions/), se ainda não estiver;
#   2. importa game/SLUS_216.61 com o processador r5900:LE:32:default e roda a
#      análise automática; depois tools/ghidra_scripts/ApplyScriptApiNames.java
#      (cria e nomeia as ~1.586 funções da API de script);
#   3. reanalisa (para seguir as chamadas das funções novas) e exporta com o
#      ExportPS2Functions.java do PS2Recomp para generated/ghidra/functions.csv.
#
# O projeto do Ghidra fica em generated/ghidra/projeto/ (contém o binário do
# jogo: nunca versionado).
#
# Uso:
#   GHIDRA_DIR=/caminho/ghidra_12.1.4_PUBLIC [EE_EXT_ZIP=/caminho/extensao.zip] tools/ghidra_analisar.sh
set -euo pipefail

raiz="$(cd "$(dirname "$0")/.." && pwd)"
: "${GHIDRA_DIR:?defina GHIDRA_DIR (pasta do Ghidra 12.1.x)}"
headless="$GHIDRA_DIR/support/analyzeHeadless"
elf="$raiz/game/SLUS_216.61"
scripts="$raiz/tools/ghidra_scripts"
projeto="$raiz/generated/ghidra/projeto"
saida="$raiz/generated/ghidra"

[[ -f "$elf" ]] || { echo "Falta $elf (o ELF do seu disco)"; exit 1; }
mkdir -p "$projeto" "$saida"

ext="$GHIDRA_DIR/Ghidra/Extensions/ghidra-emotionengine-reloaded"
if [[ ! -d "$ext" ]]; then
    zip="${EE_EXT_ZIP:-$(ls "$GHIDRA_DIR"/Extensions/Ghidra/*emotionengine*.zip 2>/dev/null | head -1)}"
    [[ -n "$zip" && -f "$zip" ]] || { echo "Zip da extensao ghidra-emotionengine-reloaded nao encontrado"; exit 1; }
    echo "Instalando extensao: $zip"
    unzip -q "$zip" -d "$GHIDRA_DIR/Ghidra/Extensions"
fi

export GHIDRA_MAXMEM="${GHIDRA_MAXMEM:-4G}"

echo "Importando e analisando..."
"$headless" "$projeto" ben10 -import "$elf" -processor "r5900:LE:32:default" \
    -scriptPath "$scripts" -postScript ApplyScriptApiNames.java -overwrite \
    > "$saida/passo1.log" 2>&1

echo "Reanalisando e exportando..."
"$headless" "$projeto" ben10 -process SLUS_216.61 -scriptPath "$scripts" \
    -postScript ExportPS2Functions.java "$saida/ghidra_export.toml" "$saida/functions.csv" \
    > "$saida/passo2.log" 2>&1

grep -h -E "ApplyScriptApiNames.java>|ExportPS2Functions.java>" "$saida/passo1.log" "$saida/passo2.log"
echo "Pronto: $saida/functions.csv"
