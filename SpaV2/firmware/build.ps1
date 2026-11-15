# Build SpaV2/firmware for the ESP32-C6. Same ESP-IDF install as MiniSplit
# (plain-upstream v5.4.1 at C:\esp\v5.4.1\esp-idf -- see MiniSplit/BUILD.md).
# -Jobs caps parallel compiles (default 4): a full-parallel esp-matter build
# (~2000 files) ran this PC out of memory. Use 2 for unattended builds.
# No ErrorActionPreference=Stop: PS 5.1 turns idf.py's stderr into errors.
param([int]$Jobs = 4)
. 'C:\esp\v5.4.1\esp-idf\export.ps1' *> $null
$env:IDF_COMPONENT_CACHE_PATH = 'C:\icc'
Push-Location $PSScriptRoot
try {
    if (-not (Test-Path sdkconfig)) {
        idf.py set-target esp32c6
        if ($LASTEXITCODE -ne 0) { throw "set-target failed ($LASTEXITCODE)" }
    }
    if (-not (Test-Path build\build.ninja)) {
        idf.py reconfigure
        if ($LASTEXITCODE -ne 0) { throw "reconfigure failed ($LASTEXITCODE)" }
    }
    # idf.py build has no job limit, so drive ninja directly.
    ninja -C build -j $Jobs
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
    idf.py size
} finally {
    Pop-Location
}
