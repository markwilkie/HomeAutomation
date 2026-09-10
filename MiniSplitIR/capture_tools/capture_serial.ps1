<#
Host-side companion for the bit-level RawPinTest.ino. That firmware now
owns the actual start/stop/capture logic (interrupt-buffered, microsecond
timestamps, nothing printed until after capture stops) -- this script just
forwards your 's'/'e' keypresses to the device over serial, echoes
everything the device sends back live, and saves the full transcript to a
file you name once the device reports CAPTURE_END.

Usage:
  .\capture_serial.ps1 [-Port COM3] [-BaudRate 115200]
#>
param(
    [string]$Port = "COM3",
    [int]$BaudRate = 115200
)

$serialPort = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
# RTS is tied to this board's reset line and DTR doubles as the boot-mode
# strap -- raising both together on open risks landing the chip in the ROM
# download/bootloader loop instead of running the app (see capture_tools
# README "Build gotcha"). Only assert DTR (needed so the sketch's
# `while (!Serial)` unblocks); leave RTS alone so no reset is triggered.
$serialPort.RtsEnable = $false
$serialPort.DtrEnable = $true

try {
    $serialPort.Open()
} catch {
    Write-Host "Failed to open $Port : $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

Write-Host "Opened $Port at $BaudRate baud." -ForegroundColor Cyan

$buffer = New-Object System.Text.StringBuilder

function Drain-Echo {
    if ($serialPort.BytesToRead -gt 0) {
        $chunk = $serialPort.ReadExisting()
        [void]$buffer.Append($chunk)
        Write-Host -NoNewline $chunk
        return $chunk
    }
    return ""
}

try {
    # Let the device's boot banner / "send 's' to start capture" prompt arrive.
    Start-Sleep -Milliseconds 500
    Drain-Echo | Out-Null

    Write-Host ""
    Write-Host "Press 's' to start capture..." -ForegroundColor Cyan
    while ($true) {
        Drain-Echo | Out-Null
        if ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            if ($key.KeyChar -eq 's') {
                $serialPort.Write("s")
                break
            }
        }
        Start-Sleep -Milliseconds 20
    }

    Write-Host "Capturing... press 'e' to stop." -ForegroundColor Yellow
    $stopSent = $false
    while ($true) {
        $chunk = Drain-Echo
        if ($chunk -match "CAPTURE_END") { break }
        if (-not $stopSent -and [Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            if ($key.KeyChar -eq 'e') {
                $serialPort.Write("e")
                $stopSent = $true
            }
        }
        Start-Sleep -Milliseconds 10
    }

    Write-Host ""
    Write-Host "Capture complete." -ForegroundColor Yellow

    $fileName = Read-Host "Enter filename to save capture as (e.g. fresh_air_test1.log)"
    if (-not $fileName) {
        $fileName = "capture_$(Get-Date -Format 'yyyyMMdd_HHmmss').log"
        Write-Host "No filename given, using $fileName"
    }

    $outPath = Join-Path (Get-Location) $fileName
    [System.IO.File]::WriteAllText($outPath, $buffer.ToString())
    Write-Host "Saved to $outPath" -ForegroundColor Green
}
finally {
    if ($serialPort.IsOpen) { $serialPort.Close() }
    $serialPort.Dispose()
}
