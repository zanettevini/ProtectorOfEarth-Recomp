# collect_disc.ps1
#
# What it is for: prepares the phase 1 material (executable analysis) from the
# Ben 10: Protector of Earth disc image, WITHOUT modifying the ISO.
#
# What it does:
#   1. Computes the ISO's size and SHA-1 (to compare with Redump).
#   2. Mounts the ISO read-only (a native Windows feature, no 7-Zip needed).
#   3. Writes the list of every file on the disc (path + size) to a CSV.
#   4. Copies SYSTEM.CNF, the boot executable it points to (BOOT2) and every
#      IRX / ELF / ERX module, keeping the disc's folders.
#   5. Writes an environment report (Visual Studio, GPU/driver, Windows).
#   6. Unmounts the ISO, even if something goes wrong.
#
# Outputs (none of them goes into the repository: .gitignore, AGENTS.md s.2):
#   game\           the files taken from the disc (the ELF in game\SLUS_216.61)
#   out\disc\       iso_info.txt (SHA-1), manifest.csv, environment.txt
#
# How to run (PowerShell), with the .iso in iso\ at the repository root:
#   powershell -ExecutionPolicy Bypass -File tools\collect_disc.ps1
# or saying where the .iso is:
#   powershell -ExecutionPolicy Bypass -File tools\collect_disc.ps1 -Iso "D:\path\game.iso"
#
# Repository version: the study-folder version was tested on Windows
# (findings/09 s.2); this one only changes the output folders and HAS NOT
# BEEN TESTED YET.

param(
    # Path to the ISO. Default: the first .iso inside iso\ at the repository root.
    [string]$Iso = "",
    # Report folder. Default: out\disc.
    [string]$Out = ""
)

$ErrorActionPreference = 'Stop'

# $PSScriptRoot = this script's folder (tools). The repository root is the folder above.
$root = Split-Path -Parent $PSScriptRoot

if ($Iso -eq "") {
    $found = Get-ChildItem -LiteralPath (Join-Path $root "iso") -Filter *.iso -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $found) { throw "No .iso in $root\iso. Put the .iso there or use -Iso <path>." }
    $Iso = $found.FullName
}
$Iso = (Resolve-Path -LiteralPath $Iso).Path
if ($Out -eq "") { $Out = Join-Path $root "out\disc" }
New-Item -ItemType Directory -Force -Path $Out | Out-Null

Write-Host "ISO:    $Iso"
Write-Host "Output: $Out"

# --- 1. Size and hash --------------------------------------------------------
# SHA-1 is the hash Redump publishes for every verified disc.
Write-Host "Computing the ISO's SHA-1 (may take a minute)..."
$size = (Get-Item -LiteralPath $Iso).Length
$sha1 = (Get-FileHash -LiteralPath $Iso -Algorithm SHA1).Hash
@(
    "file: $(Split-Path -Leaf $Iso)"
    "bytes: $size"
    "sha1: $sha1"
) | Out-File -Encoding utf8 (Join-Path $Out "iso_info.txt")

# --- 2. Mount the ISO --------------------------------------------------------
Write-Host "Mounting the ISO (read-only)..."
$image = Mount-DiskImage -ImagePath $Iso -Access ReadOnly -PassThru
try {
    # The drive letter sometimes takes a moment to show up.
    $letter = $null
    for ($i = 0; $i -lt 20 -and -not $letter; $i++) {
        $letter = ($image | Get-Volume).DriveLetter
        if (-not $letter) { Start-Sleep -Milliseconds 500 }
    }
    if (-not $letter) { throw "The ISO was mounted but got no drive letter." }
    $discRoot = "$($letter):\"
    Write-Host "Disc mounted at $discRoot"

    # --- 3. File list (manifest) --------------------------------------------
    # Names and sizes only: describes the disc without copying content.
    $all = Get-ChildItem -LiteralPath $discRoot -Recurse -File
    $all | ForEach-Object {
        [pscustomobject]@{
            path  = $_.FullName.Substring($discRoot.Length)
            bytes = $_.Length
        }
    } | Export-Csv -NoTypeInformation -Encoding utf8 (Join-Path $Out "manifest.csv")
    Write-Host "Manifest: $($all.Count) files"

    # Helper: copies a file from the disc, keeping its relative folder.
    # Disc files go into game\, keeping the disc's folders.
    $gameDir = Join-Path $root "game"
    function Copy-FromDisc([System.IO.FileInfo]$f) {
        $rel = $f.FullName.Substring($discRoot.Length)
        $dest = Join-Path $gameDir $rel
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
        Copy-Item -LiteralPath $f.FullName -Destination $dest -Force
        # Files from an ISO arrive read-only; clear the attribute on the COPY.
        (Get-Item -LiteralPath $dest).IsReadOnly = $false
        Write-Host "  copied: $rel ($($f.Length) bytes)"
    }

    # --- 4a. SYSTEM.CNF and the boot executable -----------------------------
    # SYSTEM.CNF has a "BOOT2 = cdrom0:\NAME;1" line saying which ELF the PS2 runs.
    $cnf = $all | Where-Object { $_.Name -ieq "SYSTEM.CNF" } | Select-Object -First 1
    if ($cnf) {
        Copy-FromDisc $cnf
        $text = Get-Content -LiteralPath $cnf.FullName -Raw
        if ($text -match 'BOOT2\s*=\s*cdrom0:\\([^;\r\n]+)') {
            $bootName = $Matches[1].Trim()
            $boot = Get-Item -LiteralPath (Join-Path $discRoot $bootName) -ErrorAction SilentlyContinue
            if ($boot) {
                Copy-FromDisc $boot
                "boot2: $bootName" | Out-File -Append -Encoding utf8 (Join-Path $Out "iso_info.txt")
            } else {
                Write-Warning "SYSTEM.CNF points to $bootName, but the file was not found."
            }
        } else {
            Write-Warning "No BOOT2 line found in SYSTEM.CNF."
        }
    } else {
        Write-Warning "SYSTEM.CNF not found at the disc root."
    }

    # --- 4b. IRX modules (IOP) and other ELFs -------------------------------
    # IRX = modules that run on the IOP; ERX/ELF = possible extra executables/overlays.
    $all | Where-Object { $_.Extension -in '.IRX', '.irx', '.ERX', '.erx', '.ELF', '.elf' } |
        ForEach-Object { Copy-FromDisc $_ }

    # --- 4c. Small files phase 1 also needs ---------------------------------
    # GAME.DIR (~30 KB): probably the GAME.WAD index (findings/09 s.3).
    # IOPRP300.IMG (~275 KB): IOP module image (findings/10 s.3).
    # GAME.WAD (2.4 GB) is NOT copied.
    $all | Where-Object { $_.Name -in 'GAME.DIR', 'IOPRP300.IMG' } |
        ForEach-Object { Copy-FromDisc $_ }
}
finally {
    # --- 6. Always unmount ---------------------------------------------------
    Dismount-DiskImage -ImagePath $Iso | Out-Null
    Write-Host "ISO unmounted."
}

# --- 5. Environment ---------------------------------------------------------
# An empty "where cl" is normal: MSVC only goes on the PATH inside the
# "Developer PowerShell". vswhere says whether Visual Studio is really installed.
$report = @()
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $vswhere) {
    $report += "visual_studio:"
    $report += (& $vswhere -all -products * -format text -property displayName)
    $report += "msvc_x64 (C++ component):"
    $report += (& $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
} else {
    $report += "visual_studio: not installed (no vswhere)"
}
$report += "windows: " + (Get-CimInstance Win32_OperatingSystem).Caption + " " + (Get-CimInstance Win32_OperatingSystem).Version
Get-CimInstance Win32_VideoController | ForEach-Object {
    $report += "gpu: $($_.Name) | driver $($_.DriverVersion)"
}
$report += "vulkan_sdk: $env:VULKAN_SDK"
$report | Out-File -Encoding utf8 (Join-Path $Out "environment.txt")

Write-Host ""
Write-Host "Done. Results in: $Out"
