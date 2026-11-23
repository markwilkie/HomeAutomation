# Flash an ESP-IDF project to the board whose MAC matches -ExpectedMac, and
# refuse otherwise. Written 2026-10-05 after the MiniSplit firmware was
# flashed onto the ThreadRouter: both are ESP32-C6 boards with the same USB
# VID:PID, and COM numbers move, so the port alone says nothing.
#
# Builds first only if something changed (tools/esp-build.ps1 -- seconds when
# nothing did), then writes build/ with esptool. -NoBuild writes what's in
# build/ as-is and only warns if it's out of date. Never erases flash, so
# Matter commissioning and NVS survive. See tools/ESP_BUILDS.md.
#
# Identification, least intrusive first:
#   1. USB serial number -- the C6's USB-Serial-JTAG reports its MAC there,
#      readable from Windows without touching the board.
#   2. esptool read_mac -- resets the board, so only used when exactly one
#      Espressif board is attached.
param(
    [Parameter(Mandatory)] [string]$ProjectDir,
    [Parameter(Mandatory)] [string]$ExpectedMac,
    [string]$Port,
    [int]$Jobs = 4,
    [switch]$NoBuild
)
# No global ErrorActionPreference=Stop: Windows PowerShell 5.1 turns any
# native-tool stderr (ESP-IDF's activation, idf.py progress) into a
# terminating error. Failures are thrown explicitly below instead.
$ExpectedMac = $ExpectedMac.ToUpper()
$ProjectDir = (Resolve-Path $ProjectDir).Path

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

& "$PSScriptRoot\esp-build.ps1" -ProjectDir $ProjectDir -ActivateOnly

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

if ($NoBuild) {
    # Dry run. ESP-IDF's bootloader sub-build and partition size check show up
    # every time; anything else means build/ is behind the source.
    $steps = @(ninja -C (Join-Path $ProjectDir build) -n 2>&1 |
        Where-Object { $_ -match '^\[\d+/\d+\]' -and $_ -notmatch "bootloader'|check_sizes\.py" })
    if ($steps.Count) {
        Write-Warning "build/ is out of date with the source -- writing it anyway (-NoBuild). Drop -NoBuild to build first."
    }
} else {
    & "$PSScriptRoot\esp-build.ps1" -ProjectDir $ProjectDir -Jobs $Jobs
}

$build = Join-Path $ProjectDir 'build'
if (-not (Test-Path (Join-Path $build 'flash_args'))) { throw "No build\flash_args in $ProjectDir -- build it first." }
$desc = Get-Content (Join-Path $build 'project_description.json') -Raw | ConvertFrom-Json
$bin = Get-Item (Join-Path $build $desc.app_bin)
Write-Host "Flashing $($desc.app_bin) $($desc.project_version) (built $($bin.LastWriteTime)) to $($target.Port) (MAC $($target.Mac))"
Push-Location $build
try {
    # The same files and offsets idf.py flash writes, without its build step.
    python -m esptool --chip $desc.target -p $target.Port -b 460800 --before default_reset --after hard_reset write_flash '@flash_args'
    if ($LASTEXITCODE -ne 0) { throw "esptool write_flash failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}
