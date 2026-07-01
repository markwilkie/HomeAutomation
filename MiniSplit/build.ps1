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
    Write-Host "Error: idf.py not found on PATH. Open the 'ESP-IDF 5.4 CMD' shortcut" -ForegroundColor Red
    Write-Host "(or run export.ps1 from your ESP-IDF install) and try again." -ForegroundColor Red
    Write-Host "See WINDOWS_TOOLCHAIN_SETUP.md if ESP-IDF isn't installed yet." -ForegroundColor Red
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
