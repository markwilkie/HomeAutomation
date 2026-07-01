<#
.SYNOPSIS
  Installs ESP-IDF (native Windows) so the MiniSplit Matter Bridge firmware
  can be built and flashed for the ESP32-C6.

.DESCRIPTION
  This project pulls in Matter as a managed component (espressif/esp_matter
  in main/idf_component.yml), which builds fine on native Windows -- no
  WSL2 needed. This script:
    1. Enables Windows long-path support (registry; needs admin).
    2. Installs the Espressif Installation Manager (EIM) via winget.
    3. Uses EIM to install ESP-IDF v5.4.1, matching this project's
       recorded sdkconfig (ESP-IDF 5.4.1).
    4. Verifies the riscv32-esp-elf toolchain actually extracted (EIM's
       built-in zip extractor has been observed to fail silently on some
       Windows setups -- it logs "ZipArchive decompression failed... os
       error 3" then claims a PowerShell fallback "completed successfully"
       even though files like cc1.exe never get written) and repairs it
       with `tar` if cc1.exe is missing.

.NOTES
  - Run from a normal PowerShell window. Steps that touch HKLM need an
    elevated (Run as Administrator) window -- the script warns and skips
    if it isn't elevated, rather than failing outright.
  - Requires winget (ships with modern Windows 10/11) and internet access.
  - Safe to re-run.
#>

param(
    [string]$IdfVersion = "v5.4.1",
    [string]$IdfToolsPath = "C:\Espressif\tools",
    [string]$IdfDistPath = "C:\Espressif\dist"
)

$ErrorActionPreference = "Stop"

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal $id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

Write-Host "== 1. Enabling Windows long-path support =="
if (Test-IsAdmin) {
    $current = (Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name LongPathsEnabled -ErrorAction SilentlyContinue).LongPathsEnabled
    if ($current -eq 1) {
        Write-Host "Already enabled."
    } else {
        New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name "LongPathsEnabled" -Value 1 -PropertyType DWORD -Force | Out-Null
        Write-Host "Enabled (some apps may need a restart to pick this up)."
    }
} else {
    Write-Host "Not running elevated -- skipping. The ESP-IDF toolchain has deeply" -ForegroundColor Yellow
    Write-Host "nested paths that can hit Windows' MAX_PATH limit on some setups." -ForegroundColor Yellow
    Write-Host "Re-run this script as Administrator at least once to enable long paths." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "== 2. Installing Espressif Installation Manager (EIM) =="
if (-not (Get-Command eim -ErrorAction SilentlyContinue)) {
    winget install --id Espressif.EIM-CLI --accept-package-agreements --accept-source-agreements
    Write-Host ""
    Write-Host "winget just updated PATH, but THIS PowerShell window won't see it." -ForegroundColor Yellow
    Write-Host "Close this window, open a NEW PowerShell, and re-run this script" -ForegroundColor Yellow
    Write-Host "(or just run: eim install -i $IdfVersion) to continue." -ForegroundColor Yellow
    exit 0
} else {
    Write-Host "eim already installed: $((Get-Command eim).Source)"
}

Write-Host ""
Write-Host "== 3. Installing ESP-IDF $IdfVersion via EIM =="
eim install -i $IdfVersion

Write-Host ""
Write-Host "== 4. Verifying the riscv32-esp-elf toolchain extracted correctly =="
$riscvRoot = Get-ChildItem -Path (Join-Path $IdfToolsPath "riscv32-esp-elf") -Directory -ErrorAction SilentlyContinue |
    Select-Object -First 1
$cc1 = $null
if ($riscvRoot) {
    $cc1 = Get-ChildItem -Recurse -Path $riscvRoot.FullName -Filter cc1.exe -ErrorAction SilentlyContinue | Select-Object -First 1
}

if ($cc1) {
    Write-Host "OK: found $($cc1.FullName)"
} else {
    Write-Host "cc1.exe is missing -- EIM's extractor likely failed silently. Repairing with tar..." -ForegroundColor Yellow

    if (-not $riscvRoot) {
        Write-Host "Could not find an extracted riscv32-esp-elf folder under $IdfToolsPath\riscv32-esp-elf -- skipping auto-repair." -ForegroundColor Red
        Write-Host "See WINDOWS_TOOLCHAIN_SETUP.md to repair manually." -ForegroundColor Red
    } else {
        $zip = Get-ChildItem -Path $IdfDistPath -Filter "riscv32-esp-elf-*-x86_64-w64-mingw32.zip" -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if (-not $zip) {
            Write-Host "Could not find the cached riscv32-esp-elf zip under $IdfDistPath -- skipping auto-repair." -ForegroundColor Red
            Write-Host "See WINDOWS_TOOLCHAIN_SETUP.md to repair manually." -ForegroundColor Red
        } else {
            Write-Host "Re-extracting $($zip.Name) with tar..."
            Remove-Item -Recurse -Force $riscvRoot.FullName
            New-Item -ItemType Directory -Force -Path $riscvRoot.FullName | Out-Null
            tar -xf $zip.FullName -C $riscvRoot.FullName

            $cc1 = Get-ChildItem -Recurse -Path $riscvRoot.FullName -Filter cc1.exe -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($cc1) {
                Write-Host "Repaired: found $($cc1.FullName)"
            } else {
                Write-Host "Still missing after re-extraction. See WINDOWS_TOOLCHAIN_SETUP.md for further troubleshooting." -ForegroundColor Red
            }
        }
    }
}

Write-Host ""
Write-Host "Done."
Write-Host ""
Write-Host "Open the 'ESP-IDF $($IdfVersion.Substring(0,4)) CMD' shortcut EIM created"
Write-Host "(Start Menu / Desktop), or source the environment yourself, then:"
Write-Host ""
Write-Host "  cd $PSScriptRoot"
Write-Host "  idf.py set-target esp32c6"
Write-Host "  idf.py build"
Write-Host "  idf.py -p COM6 flash monitor   # swap COM6 for your device's port"
Write-Host ""
Write-Host "See WINDOWS_TOOLCHAIN_SETUP.md for troubleshooting."
