# Flash WeatherStationThread, refusing any board but the expected one (MAC
# check in ../tools/flash-esp32.ps1: several ESP32-C6 boards share this PC's
# USB ports with the same VID:PID). Build first with .\build.ps1 -Board <same>.
#   .\flash.ps1                     DevKitC
#   .\flash.ps1 -Board firebeetle   FireBeetle
# DevKitC: plug in the port labelled USB (native USB-Serial-JTAG) -- the MAC
# check only sees that one; the UART port is for the console.
# A board's MAC goes in $Macs once known (the boot log prints it, or
# `python -m esptool --chip esp32c6 -p COMx read_mac`); -Mac overrides.
param(
    [ValidateSet('devkitc', 'firebeetle')] [string]$Board = 'devkitc',
    [string]$Mac,
    [string]$Port
)
$Macs = @{
    devkitc    = 'ac:eb:e6:d2:94:0c'   # DevKitC-1-N8, read 2026-11-25
    firebeetle = ''   # not recorded yet
}
if (-not $Mac) { $Mac = $Macs[$Board] }
if (-not $Mac) { throw "No MAC recorded for $Board in flash.ps1: pass -Mac once, then record it." }
$stamp = Join-Path $PSScriptRoot 'build\board.txt'
$built = if (Test-Path $stamp) { (Get-Content $stamp -Raw).Trim() } else { $null }
if ($built -ne $Board) { throw "build\ is for '$built', not $Board. Run .\build.ps1 -Board $Board first." }
& "$PSScriptRoot\..\tools\flash-esp32.ps1" -ProjectDir $PSScriptRoot `
    -ExpectedMac $Mac `
    -IdfActivate 'C:\esp\v5.4.1\esp-idf\export.ps1' `
    -Port $Port
