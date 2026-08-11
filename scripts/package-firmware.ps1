# Package PaxxTouch build artifacts for GitHub Releases + web flasher.
# Usage: .\scripts\package-firmware.ps1 [-Version "0.1.5"] [-Env "paxxtouch-remote"]

param(
    [string]$Version = "0.1.5",
    [ValidateSet("paxxtouch-remote", "paxxtouch")]
    [string]$Env = "paxxtouch-remote"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $Root ".pio\build\$Env"
$OutDir = Join-Path $Root "dist\firmware"

if (-not (Test-Path (Join-Path $BuildDir "firmware.bin"))) {
    Write-Host "Building firmware ($Env)..."
    Push-Location $Root
    python -m platformio run -e $Env
    Pop-Location
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Copy-Item (Join-Path $BuildDir "bootloader.bin") (Join-Path $OutDir "paxxtouch-bootloader.bin") -Force
Copy-Item (Join-Path $BuildDir "partitions.bin") (Join-Path $OutDir "paxxtouch-partitions.bin") -Force
Copy-Item (Join-Path $BuildDir "firmware.bin") (Join-Path $OutDir "paxxtouch-firmware.bin") -Force

$PagesFirmware = Join-Path $Root "docs\flasher\firmware"
New-Item -ItemType Directory -Force -Path $PagesFirmware | Out-Null
Copy-Item (Join-Path $OutDir "*") $PagesFirmware -Force
Write-Host "Copied firmware to $PagesFirmware (web flasher same-origin hosting)"

# boot_app0.bin from Arduino-ESP32 framework (required for OTA partition table)
$BootApp0 = Get-ChildItem "$env:USERPROFILE\.platformio\packages" -Recurse -Filter "boot_app0.bin" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($BootApp0) {
    Copy-Item $BootApp0.FullName (Join-Path $OutDir "paxxtouch-boot_app0.bin") -Force
    Write-Host "Included boot_app0.bin from $($BootApp0.FullName)"
} else {
    Write-Warning "boot_app0.bin not found - web flasher marks it optional; OTA may not work without it."
}

$ZipPath = Join-Path $Root "dist\paxxtouch-firmware-v$Version.zip"
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
Compress-Archive -Path (Join-Path $OutDir "*") -DestinationPath $ZipPath

Write-Host ""
Write-Host "Packaged $Env firmware to:"
Write-Host "  $OutDir"
Write-Host "  $ZipPath"
Write-Host ""
Write-Host "Upload these files as GitHub Release assets (tag v$Version):"
Get-ChildItem $OutDir | ForEach-Object { Write-Host "  - $($_.Name)" }
