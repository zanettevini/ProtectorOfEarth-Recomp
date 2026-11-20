#!/usr/bin/env bash
# ghidra_analyze.sh: headless Ghidra analysis and export of the function list
# that ps2_recomp reads (docs/findings/12). Linux/macOS version of
# tools/ghidra_analyze.ps1.
#
# Steps:
#   1. installs the ghidra-emotionengine-reloaded extension into Ghidra
#      (unzips the .zip into Ghidra/Extensions/), if it is not there yet;
#   2. imports game/SLUS_216.61 with the r5900:LE:32:default processor and runs
#      auto-analysis; then tools/ghidra_scripts/ApplyScriptApiNames.java
#      (creates and names the ~1,586 script API functions);
#   3. re-analyses (to follow the calls out of the new functions) and exports
#      with PS2Recomp's ExportPS2Functions.java to generated/ghidra/functions.csv.
#
# The Ghidra project lives in generated/ghidra/project/ (it contains the game
# binary: never versioned).
#
# Usage:
#   GHIDRA_DIR=/path/ghidra_12.1.4_PUBLIC [EE_EXT_ZIP=/path/extension.zip] tools/ghidra_analyze.sh
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
: "${GHIDRA_DIR:?set GHIDRA_DIR (the Ghidra 12.1.x folder)}"
headless="$GHIDRA_DIR/support/analyzeHeadless"
elf="$root/game/SLUS_216.61"
scripts="$root/tools/ghidra_scripts"
project="$root/generated/ghidra/project"
out="$root/generated/ghidra"

[[ -f "$elf" ]] || { echo "Missing $elf (the ELF from your disc)"; exit 1; }
mkdir -p "$project" "$out"

ext="$GHIDRA_DIR/Ghidra/Extensions/ghidra-emotionengine-reloaded"
if [[ ! -d "$ext" ]]; then
    zip="${EE_EXT_ZIP:-$(ls "$GHIDRA_DIR"/Extensions/Ghidra/*emotionengine*.zip 2>/dev/null | head -1)}"
    [[ -n "$zip" && -f "$zip" ]] || { echo "ghidra-emotionengine-reloaded extension zip not found"; exit 1; }
    echo "Installing extension: $zip"
    unzip -q "$zip" -d "$GHIDRA_DIR/Ghidra/Extensions"
fi

export GHIDRA_MAXMEM="${GHIDRA_MAXMEM:-4G}"

echo "Importing and analysing..."
"$headless" "$project" ben10 -import "$elf" -processor "r5900:LE:32:default" \
    -scriptPath "$scripts" -postScript ApplyScriptApiNames.java -overwrite \
    > "$out/step1.log" 2>&1

echo "Re-analysing and exporting..."
"$headless" "$project" ben10 -process SLUS_216.61 -scriptPath "$scripts" \
    -postScript ExportPS2Functions.java "$out/ghidra_export.toml" "$out/functions.csv" \
    > "$out/step2.log" 2>&1

grep -h -E "ApplyScriptApiNames.java>|ExportPS2Functions.java>" "$out/step1.log" "$out/step2.log"
echo "Done: $out/functions.csv"
