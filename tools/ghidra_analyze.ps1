# ghidra_analyze.ps1: headless Ghidra analysis and export of the function list
# that ps2_recomp reads (docs/findings/12). Windows version of
# tools/ghidra_analyze.sh.
#
# Steps:
#   1. installs the ghidra-emotionengine-reloaded extension into Ghidra (unzips
#      the .zip from Extensions\Ghidra\ into Ghidra\Extensions\), if it is not
#      there yet;
#   2. imports game\SLUS_216.61 with the r5900:LE:32:default processor,
#      analyses it and runs tools\ghidra_scripts\ApplyScriptApiNames.java;
#   3. re-analyses and exports with PS2Recomp's ExportPS2Functions.java to
#      generated\ghidra\functions.csv.
#
# The Ghidra project lives in generated\ghidra\project\ (it contains the game
# binary: never versioned).
#
# Requirements: JDK 21 and Ghidra 12.1.x (with the extension's .zip in
# <Ghidra>\Extensions\Ghidra\).
#
# How to run (PowerShell, at the repository root):
#   powershell -ExecutionPolicy Bypass -File tools\ghidra_analyze.ps1 -GhidraDir "C:\path\ghidra_12.1.4_PUBLIC"
#
# STATUS: the Linux version (ghidra_analyze.sh) was tested and reproduces the
# same function list. This Windows version HAS NOT BEEN TESTED YET.

param(
    [string]$GhidraDir = $env:GHIDRA_DIR
)

$ErrorActionPreference = 'Stop'

if (-not $GhidraDir) { throw "Say where Ghidra is: -GhidraDir `"C:\...\ghidra_12.1.4_PUBLIC`"" }
$root     = Split-Path -Parent $PSScriptRoot
$headless = Join-Path $GhidraDir "support\analyzeHeadless.bat"
$scripts  = Join-Path $root "tools\ghidra_scripts"
$elf      = Join-Path $root "game\SLUS_216.61"
$out      = Join-Path $root "generated\ghidra"
$project  = Join-Path $out "project"

foreach ($p in @($headless, $elf)) {
    if (-not (Test-Path -LiteralPath $p)) { throw "Missing: $p" }
}
New-Item -ItemType Directory -Force -Path $project | Out-Null

# --- 1. Emotion Engine extension -------------------------------------------
$extDest = Join-Path $GhidraDir "Ghidra\Extensions\ghidra-emotionengine-reloaded"
if (-not (Test-Path -LiteralPath $extDest)) {
    $zip = Get-ChildItem -LiteralPath (Join-Path $GhidraDir "Extensions\Ghidra") -Filter "*emotionengine*.zip" | Select-Object -First 1
    if (-not $zip) { throw "ghidra-emotionengine-reloaded extension zip not found in $GhidraDir\Extensions\Ghidra" }
    Write-Host "Installing extension: $($zip.Name)"
    Expand-Archive -LiteralPath $zip.FullName -DestinationPath (Join-Path $GhidraDir "Ghidra\Extensions")
}

# More memory for the analysis (headless Ghidra's default is 2 GB).
$env:GHIDRA_MAXMEM = "4G"

# --- 2. Import, analyse, name the script API -------------------------------
Write-Host "Importing and analysing (a few minutes)..."
& $headless $project ben10 -import $elf -processor "r5900:LE:32:default" `
    -scriptPath $scripts -postScript ApplyScriptApiNames.java -overwrite `
    *> (Join-Path $out "step1.log")
if ($LASTEXITCODE -ne 0) { throw "analyzeHeadless failed (see generated\ghidra\step1.log)" }

# --- 3. Re-analyse and export ----------------------------------------------
Write-Host "Re-analysing and exporting..."
& $headless $project ben10 -process "SLUS_216.61" -scriptPath $scripts `
    -postScript ExportPS2Functions.java (Join-Path $out "ghidra_export.toml") (Join-Path $out "functions.csv") `
    *> (Join-Path $out "step2.log")
if ($LASTEXITCODE -ne 0) { throw "analyzeHeadless failed (see generated\ghidra\step2.log)" }

Select-String -Path (Join-Path $out "step1.log"), (Join-Path $out "step2.log") `
    -Pattern "ApplyScriptApiNames.java>|ExportPS2Functions.java>" | ForEach-Object { $_.Line }
Write-Host "Done: generated\ghidra\functions.csv"
