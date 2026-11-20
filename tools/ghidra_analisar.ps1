# ghidra_analisar.ps1: análise headless do Ghidra e exportação da lista de
# funções que o ps2_recomp lê (docs/findings/12). Versão Windows de
# tools/ghidra_analisar.sh.
#
# Passos:
#   1. instala a extensão ghidra-emotionengine-reloaded no Ghidra (descompacta
#      o .zip de Extensions\Ghidra\ em Ghidra\Extensions\), se ainda não estiver;
#   2. importa game\SLUS_216.61 com o processador r5900:LE:32:default, analisa
#      e roda tools\ghidra_scripts\ApplyScriptApiNames.java;
#   3. reanalisa e exporta com o ExportPS2Functions.java do PS2Recomp para
#      generated\ghidra\functions.csv.
#
# O projeto do Ghidra fica em generated\ghidra\projeto\ (contém o binário do
# jogo: nunca versionado).
#
# Pré-requisitos: JDK 21 e Ghidra 12.1.x (com o .zip da extensão em
# <Ghidra>\Extensions\Ghidra\).
#
# Como rodar (PowerShell, na raiz do repositório):
#   powershell -ExecutionPolicy Bypass -File tools\ghidra_analisar.ps1 -GhidraDir "C:\caminho\ghidra_12.1.4_PUBLIC"
#
# SITUAÇÃO: a versão Linux (ghidra_analisar.sh) foi testada e reproduz a
# mesma lista de funções. Esta versão Windows AINDA NÃO FOI TESTADA.

param(
    [string]$GhidraDir = $env:GHIDRA_DIR
)

$ErrorActionPreference = 'Stop'

if (-not $GhidraDir) { throw "Diga onde esta o Ghidra: -GhidraDir `"C:\...\ghidra_12.1.4_PUBLIC`"" }
$raiz     = Split-Path -Parent $PSScriptRoot
$headless = Join-Path $GhidraDir "support\analyzeHeadless.bat"
$scripts  = Join-Path $raiz "tools\ghidra_scripts"
$elf      = Join-Path $raiz "game\SLUS_216.61"
$saida    = Join-Path $raiz "generated\ghidra"
$projeto  = Join-Path $saida "projeto"

foreach ($p in @($headless, $elf)) {
    if (-not (Test-Path -LiteralPath $p)) { throw "Faltando: $p" }
}
New-Item -ItemType Directory -Force -Path $projeto | Out-Null

# --- 1. Extensão do Emotion Engine -----------------------------------------
$extDestino = Join-Path $GhidraDir "Ghidra\Extensions\ghidra-emotionengine-reloaded"
if (-not (Test-Path -LiteralPath $extDestino)) {
    $zip = Get-ChildItem -LiteralPath (Join-Path $GhidraDir "Extensions\Ghidra") -Filter "*emotionengine*.zip" | Select-Object -First 1
    if (-not $zip) { throw "Zip da extensao ghidra-emotionengine-reloaded nao encontrado em $GhidraDir\Extensions\Ghidra" }
    Write-Host "Instalando extensao: $($zip.Name)"
    Expand-Archive -LiteralPath $zip.FullName -DestinationPath (Join-Path $GhidraDir "Ghidra\Extensions")
}

# Mais memória para a análise (o padrão do Ghidra headless é 2 GB).
$env:GHIDRA_MAXMEM = "4G"

# --- 2. Importar, analisar, nomear a API de script -------------------------
Write-Host "Importando e analisando (alguns minutos)..."
& $headless $projeto ben10 -import $elf -processor "r5900:LE:32:default" `
    -scriptPath $scripts -postScript ApplyScriptApiNames.java -overwrite `
    *> (Join-Path $saida "passo1.log")
if ($LASTEXITCODE -ne 0) { throw "analyzeHeadless falhou (veja generated\ghidra\passo1.log)" }

# --- 3. Reanalisar e exportar ----------------------------------------------
Write-Host "Reanalisando e exportando..."
& $headless $projeto ben10 -process "SLUS_216.61" -scriptPath $scripts `
    -postScript ExportPS2Functions.java (Join-Path $saida "ghidra_export.toml") (Join-Path $saida "functions.csv") `
    *> (Join-Path $saida "passo2.log")
if ($LASTEXITCODE -ne 0) { throw "analyzeHeadless falhou (veja generated\ghidra\passo2.log)" }

Select-String -Path (Join-Path $saida "passo1.log"), (Join-Path $saida "passo2.log") `
    -Pattern "ApplyScriptApiNames.java>|ExportPS2Functions.java>" | ForEach-Object { $_.Line }
Write-Host "Pronto: generated\ghidra\functions.csv"
