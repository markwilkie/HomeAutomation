# Build SpaV2/test_leds for the ESP32-C6. Same ESP-IDF install as MiniSplit
# (plain-upstream v5.4.1 at C:\esp\v5.4.1\esp-idf -- see MiniSplit/BUILD.md).
# No ErrorActionPreference=Stop: PS 5.1 turns idf.py's stderr into errors.
. 'C:\esp\v5.4.1\esp-idf\export.ps1' *> $null
$env:IDF_COMPONENT_CACHE_PATH = 'C:\icc'
Push-Location $PSScriptRoot
try {
    if (-not (Test-Path sdkconfig)) {
        idf.py set-target esp32c6
        if ($LASTEXITCODE -ne 0) { throw "set-target failed ($LASTEXITCODE)" }
    }
    idf.py build
    if ($LASTEXITCODE -ne 0) { throw "idf.py build failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}
