# Build WeatherStationThread for one board. Same ESP-IDF install as MiniSplit
# and SpaV2 (plain-upstream v5.4.1 at C:\esp\v5.4.1\esp-idf).
#   .\build.ps1                     DevKitC (development)
#   .\build.ps1 -Board firebeetle   FireBeetle 2 ESP32-C6 (deploy)
# Board settings come from sdkconfig.defaults + sdkconfig.defaults.<board>.
# Switching boards wipes sdkconfig and build\ (a full rebuild): sdkconfig is
# generated from the defaults once and would otherwise keep the old board.
# -Jobs caps parallel compiles (default 4; use 2 for unattended builds).
# No ErrorActionPreference=Stop: PS 5.1 turns idf.py's stderr into errors.
param(
    [ValidateSet('devkitc', 'firebeetle')] [string]$Board = 'devkitc',
    [int]$Jobs = 4
)
. 'C:\esp\v5.4.1\esp-idf\export.ps1' *> $null
$env:IDF_COMPONENT_CACHE_PATH = 'C:\icc'
Push-Location $PSScriptRoot
try {
    $stamp = 'build\board.txt'
    $built = if (Test-Path $stamp) { (Get-Content $stamp -Raw).Trim() } else { $null }
    if ($built -and $built -ne $Board) {
        Write-Host "Switching from $built to $Board`: removing sdkconfig and build\"
        Remove-Item -Recurse -Force build
        Remove-Item -Force sdkconfig -ErrorAction SilentlyContinue
    }
    if (-not (Test-Path build\build.ninja)) {
        Remove-Item -Force sdkconfig -ErrorAction SilentlyContinue
        idf.py -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.$Board" set-target esp32c6
        if ($LASTEXITCODE -ne 0) { throw "set-target failed ($LASTEXITCODE)" }
        Set-Content -Path $stamp -Value $Board -Encoding ascii
    }
    # idf.py build has no job limit, so drive ninja directly.
    ninja -C build -j $Jobs
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
    idf.py size
} finally {
    Pop-Location
}
