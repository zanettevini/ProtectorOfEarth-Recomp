# coletar_disco.ps1
#
# Para que serve: prepara o material da Fase 1 (análise do executável) a partir
# da imagem do disco de Ben 10: Protector of Earth, SEM modificar o ISO.
#
# O que faz:
#   1. Calcula tamanho e SHA-1 do ISO (para comparar com o Redump).
#   2. Monta o ISO só para leitura (recurso nativo do Windows, sem 7-Zip).
#   3. Grava a lista de todos os arquivos do disco (caminho + tamanho) em CSV.
#   4. Copia o SYSTEM.CNF, o executável de boot que ele aponta (BOOT2) e todos
#      os módulos IRX / ELF / ERX, mantendo as pastas do disco.
#   5. Grava um relatório do ambiente (Visual Studio, GPU/driver, Windows).
#   6. Desmonta o ISO, mesmo se algo der errado.
#
# Saídas (nenhuma entra no repositório: .gitignore, AGENTS.md s.2):
#   game\           os arquivos tirados do disco (o ELF em game\SLUS_216.61)
#   out\disco\      iso_info.txt (SHA-1), manifesto.csv, ambiente.txt
#
# Como rodar (PowerShell), com o .iso em iso\ na raiz do repositório:
#   powershell -ExecutionPolicy Bypass -File tools\coletar_disco.ps1
# ou dizendo onde está o .iso:
#   powershell -ExecutionPolicy Bypass -File tools\coletar_disco.ps1 -Iso "D:\caminho\jogo.iso"
#
# Versão do repositório: a versão da pasta de estudo foi testada no Windows
# (findings/09 s.2); esta só muda as pastas de saída e AINDA NÃO FOI TESTADA.

param(
    # Caminho do ISO. Padrão: o primeiro .iso dentro de iso\ na raiz do repositório.
    [string]$Iso = "",
    # Pasta de relatórios. Padrão: out\disco.
    [string]$Saida = ""
)

$ErrorActionPreference = 'Stop'

# $PSScriptRoot = pasta deste script (tools). A raiz do repositório é a pasta acima.
$raiz = Split-Path -Parent $PSScriptRoot

if ($Iso -eq "") {
    $achado = Get-ChildItem -LiteralPath (Join-Path $raiz "iso") -Filter *.iso -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $achado) { throw "Nenhum .iso em $raiz\iso. Coloque o .iso la ou use -Iso <caminho>." }
    $Iso = $achado.FullName
}
$Iso = (Resolve-Path -LiteralPath $Iso).Path
if ($Saida -eq "") { $Saida = Join-Path $raiz "out\disco" }
New-Item -ItemType Directory -Force -Path $Saida | Out-Null

Write-Host "ISO:   $Iso"
Write-Host "Saida: $Saida"

# --- 1. Tamanho e hash ------------------------------------------------------
# SHA-1 é o hash que o Redump publica para cada disco verificado.
Write-Host "Calculando SHA-1 do ISO (pode levar um minuto)..."
$tamanho = (Get-Item -LiteralPath $Iso).Length
$sha1 = (Get-FileHash -LiteralPath $Iso -Algorithm SHA1).Hash
@(
    "arquivo: $(Split-Path -Leaf $Iso)"
    "bytes: $tamanho"
    "sha1: $sha1"
) | Out-File -Encoding utf8 (Join-Path $Saida "iso_info.txt")

# --- 2. Montar o ISO ---------------------------------------------------------
Write-Host "Montando o ISO (somente leitura)..."
$imagem = Mount-DiskImage -ImagePath $Iso -Access ReadOnly -PassThru
try {
    # A letra da unidade às vezes demora alguns instantes para aparecer.
    $letra = $null
    for ($i = 0; $i -lt 20 -and -not $letra; $i++) {
        $letra = ($imagem | Get-Volume).DriveLetter
        if (-not $letra) { Start-Sleep -Milliseconds 500 }
    }
    if (-not $letra) { throw "O ISO foi montado mas nao recebeu letra de unidade." }
    $raizDisco = "$($letra):\"
    Write-Host "Disco montado em $raizDisco"

    # --- 3. Lista de arquivos (manifesto) -----------------------------------
    # Só nomes e tamanhos: descreve o disco sem copiar conteúdo.
    $todos = Get-ChildItem -LiteralPath $raizDisco -Recurse -File
    $todos | ForEach-Object {
        [pscustomobject]@{
            caminho = $_.FullName.Substring($raizDisco.Length)
            bytes   = $_.Length
        }
    } | Export-Csv -NoTypeInformation -Encoding utf8 (Join-Path $Saida "manifesto.csv")
    Write-Host "Manifesto: $($todos.Count) arquivos"

    # Função auxiliar: copia um arquivo do disco mantendo a pasta relativa.
    # Os arquivos do disco vão para game\, mantendo as pastas do disco.
    $pastaArquivos = Join-Path $raiz "game"
    function Copiar-DoDisco([System.IO.FileInfo]$f) {
        $rel = $f.FullName.Substring($raizDisco.Length)
        $destino = Join-Path $pastaArquivos $rel
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destino) | Out-Null
        Copy-Item -LiteralPath $f.FullName -Destination $destino -Force
        # Arquivos vindos de ISO chegam como somente-leitura; tira o atributo da CÓPIA.
        (Get-Item -LiteralPath $destino).IsReadOnly = $false
        Write-Host "  copiado: $rel ($($f.Length) bytes)"
    }

    # --- 4a. SYSTEM.CNF e o executável de boot ------------------------------
    # SYSTEM.CNF tem uma linha "BOOT2 = cdrom0:\NOME;1" que diz qual ELF o PS2 executa.
    $cnf = $todos | Where-Object { $_.Name -ieq "SYSTEM.CNF" } | Select-Object -First 1
    if ($cnf) {
        Copiar-DoDisco $cnf
        $texto = Get-Content -LiteralPath $cnf.FullName -Raw
        if ($texto -match 'BOOT2\s*=\s*cdrom0:\\([^;\r\n]+)') {
            $nomeBoot = $Matches[1].Trim()
            $boot = Get-Item -LiteralPath (Join-Path $raizDisco $nomeBoot) -ErrorAction SilentlyContinue
            if ($boot) {
                Copiar-DoDisco $boot
                "boot2: $nomeBoot" | Out-File -Append -Encoding utf8 (Join-Path $Saida "iso_info.txt")
            } else {
                Write-Warning "SYSTEM.CNF aponta para $nomeBoot, mas o arquivo nao foi achado."
            }
        } else {
            Write-Warning "Linha BOOT2 nao encontrada no SYSTEM.CNF."
        }
    } else {
        Write-Warning "SYSTEM.CNF nao encontrado na raiz do disco."
    }

    # --- 4b. Módulos IRX (IOP) e outros ELF ---------------------------------
    # IRX = módulos que rodam no IOP; ERX/ELF = possíveis executáveis extras/overlays.
    $todos | Where-Object { $_.Extension -in '.IRX', '.irx', '.ERX', '.erx', '.ELF', '.elf' } |
        ForEach-Object { Copiar-DoDisco $_ }

    # --- 4c. Arquivos pequenos que a Fase 1 também precisa ------------------
    # GAME.DIR (~30 KB): provável índice do GAME.WAD (findings/09 s.3).
    # IOPRP300.IMG (~275 KB): imagem de módulos do IOP (findings/10 s.3).
    # O GAME.WAD (2,4 GB) NÃO é copiado.
    $todos | Where-Object { $_.Name -in 'GAME.DIR', 'IOPRP300.IMG' } |
        ForEach-Object { Copiar-DoDisco $_ }
}
finally {
    # --- 6. Desmontar sempre -----------------------------------------------
    Dismount-DiskImage -ImagePath $Iso | Out-Null
    Write-Host "ISO desmontado."
}

# --- 5. Ambiente ------------------------------------------------------------
# "where cl" vazio é normal: o MSVC só entra no PATH dentro do "Developer
# PowerShell". O vswhere diz se o Visual Studio está instalado de fato.
$relatorio = @()
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $vswhere) {
    $relatorio += "visual_studio:"
    $relatorio += (& $vswhere -all -products * -format text -property displayName)
    $relatorio += "msvc_x64 (componente C++):"
    $relatorio += (& $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
} else {
    $relatorio += "visual_studio: nao instalado (vswhere ausente)"
}
$relatorio += "windows: " + (Get-CimInstance Win32_OperatingSystem).Caption + " " + (Get-CimInstance Win32_OperatingSystem).Version
Get-CimInstance Win32_VideoController | ForEach-Object {
    $relatorio += "gpu: $($_.Name) | driver $($_.DriverVersion)"
}
$relatorio += "vulkan_sdk: $env:VULKAN_SDK"
$relatorio | Out-File -Encoding utf8 (Join-Path $Saida "ambiente.txt")

Write-Host ""
Write-Host "Pronto. Resultado em: $Saida"
