# Flash an ESP-IDF project to the board whose MAC matches -ExpectedMac, and
# refuse otherwise. Written 2026-10-05 after the MiniSplit firmware was
# flashed onto the ThreadRouter: both are ESP32-C6 boards with the same USB
# VID:PID, and COM numbers move, so the port alone says nothing.
#
# Identification, least intrusive first:
#   1. USB serial number -- the C6's USB-Serial-JTAG reports its MAC there,
#      readable from Windows without touching the board.
#   2. esptool read_mac -- resets the board, so only used when exactly one
#      Espressif board is attached.
param(
    [Parameter(Mandatory)] [string]$ProjectDir,
    [Parameter(Mandatory)] [string]$ExpectedMac,
    [Parameter(Mandatory)] [string]$IdfActivate,
    [string]$Port
)
# No global ErrorActionPreference=Stop: Windows PowerShell 5.1 turns any
# native-tool stderr (ESP-IDF's activation, idf.py progress) into a
# terminating error. Failures are thrown explicitly below instead.
$ExpectedMac = $ExpectedMac.ToUpper()

function Get-EspPorts {
    Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.DeviceID -match 'VID_303A&PID_1001&MI_00' -and $_.Name -match '\((COM\d+)\)' } |
        ForEach-Object {
            $com = [regex]::Match($_.Name, 'COM\d+').Value
            $serial = $null
            try {
                $parent = (Get-PnpDeviceProperty -InstanceId $_.DeviceID -KeyName DEVPKEY_Device_Parent).Data
                $tail = ($parent -split '\\')[-1]
                if ($tail -match '^([0-9A-F]{2}:){5}[0-9A-F]{2}$') { $serial = $tail.ToUpper() }
            } catch { }
            [pscustomobject]@{ Port = $com; Mac = $serial }
        }
}

. $IdfActivate *> $null
$env:IDF_COMPONENT_CACHE_PATH = 'C:\icc'

$ports = @(Get-EspPorts)
if ($Port) { $ports = @($ports | Where-Object { $_.Port -eq $Port }) }
if ($ports.Count -eq 0) { throw "No Espressif board found$(if ($Port) { " on $Port" })." }

$target = $ports | Where-Object { $_.Mac -eq $ExpectedMac } | Select-Object -First 1
if (-not $target) {
    $unknown = @($ports | Where-Object { -not $_.Mac })
    if ($ports.Count -eq 1 -and $unknown.Count -eq 1) {
        $p = $ports[0].Port
        Write-Host "USB serial number unavailable on $p; reading MAC with esptool (resets the board)..."
        $out = python -m esptool --chip esp32c6 -p $p --after hard_reset read_mac 2>&1 | Out-String
        $m = [regex]::Match($out, 'BASE MAC:\s*([0-9a-f:]{17})')
        $mac = if ($m.Success) { $m.Groups[1].Value.ToUpper() } else { $null }
        if ($mac -eq $ExpectedMac) { $target = [pscustomobject]@{ Port = $p; Mac = $mac } }
        else { throw "Board on $p has MAC '$mac', expected $ExpectedMac. Not flashing." }
    } else {
        $seen = ($ports | ForEach-Object { "$($_.Port)=$(if ($_.Mac) { $_.Mac } else { 'unknown' })" }) -join ', '
        throw "No attached board has MAC $ExpectedMac ($seen). Not flashing."
    }
}

Write-Host "Flashing $ProjectDir to $($target.Port) (MAC $($target.Mac))"
Push-Location $ProjectDir
try {
    idf.py -p $target.Port flash
    if ($LASTEXITCODE -ne 0) { throw "idf.py flash failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}
