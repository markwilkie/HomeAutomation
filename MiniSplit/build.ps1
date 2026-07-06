<#
.SYNOPSIS
  Build and flash script for the MiniSplit Matter Bridge (native Windows).
  Windows equivalent of build.sh.

.PARAMETER Target
  IDF target chip. Defaults to esp32c6. Do NOT use esp32s3 for this project.

.PARAMETER Port
  COM port to flash. Defaults to COM6.

.EXAMPLE
  .\build.ps1
  .\build.ps1 -Target esp32c6 -Port COM7
#>

param(
    [string]$Target = "esp32c6",
    [string]$Port = "COM6"
)

$ErrorActionPreference = "Stop"

Write-Host "=========================================="
Write-Host "MiniSplit Matter Bridge Build Script"
Write-Host "=========================================="
Write-Host "Target: $Target"
Write-Host ""

if (-not (Test-Path "CMakeLists.txt")) {
    Write-Host "Error: CMakeLists.txt not found. Run from project root." -ForegroundColor Red
    exit 1
}

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    # EIM (Espressif Installation Manager) installs are common on Windows and don't
    # put idf.py on PATH by default. Its Python venv lives under C:\Espressif, not
    # the %USERPROFILE%\.espressif path that ESP-IDF's own export.ps1 expects, so
    # sourcing export.ps1 directly fails with "Python virtual environment not found".
    # Auto-activate the EIM profile script instead if we can find one.
    $eimActivation = Get-ChildItem "C:\Espressif\tools" -Filter "Microsoft.v*.PowerShell_profile.ps1" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($eimActivation) {
        Write-Host "idf.py not on PATH; activating $($eimActivation.Name)..." -ForegroundColor Yellow
        . $eimActivation.FullName
    }
}

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    Write-Host "Error: idf.py not found on PATH." -ForegroundColor Red
    Write-Host "If ESP-IDF is installed via EIM, activate it first:" -ForegroundColor Red
    Write-Host "  . C:\Espressif\tools\Microsoft.v5.4.1.PowerShell_profile.ps1" -ForegroundColor Red
    Write-Host "Otherwise open the 'ESP-IDF 5.4 CMD' shortcut, or install ESP-IDF v5.4.1 via EIM:" -ForegroundColor Red
    Write-Host "  winget install Espressif.EIM-CLI; eim install -i v5.4.1" -ForegroundColor Red
    exit 1
}

Write-Host "Setting ESP32 target to: $Target"
idf.py set-target $Target

Write-Host "Configure:"
Write-Host "  - WiFi SSID/Password in src/main.c"
Write-Host "  - Tuya credentials in CONFIG.md / secure NVS storage"
$runMenuconfig = Read-Host "Run menuconfig? (y/n)"
if ($runMenuconfig -match '^[Yy]') {
    idf.py menuconfig
}

Write-Host ""
Write-Host "Building firmware..."
idf.py build

Write-Host ""
Read-Host "Ready to flash. Connect your ESP32-C6 and press Enter"

Write-Host "Flashing to $Port..."
idf.py -p $Port flash

Write-Host ""
$runMonitor = Read-Host "Start serial monitor? (y/n)"
if ($runMonitor -match '^[Yy]') {
    idf.py -p $Port monitor
}

Write-Host "Done!"
