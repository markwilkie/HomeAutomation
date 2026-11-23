# Build an ESP-IDF project in this repo, compiling only what changed: when
# nothing did, ninja says "no work to do" in seconds. Used by every project's
# build.ps1 and by flash-esp32.ps1. Why it's built this way: tools/ESP_BUILDS.md.
#
#   tools\esp-build.ps1 -ProjectDir MiniSplit            build if needed
#   tools\esp-build.ps1 -ProjectDir MiniSplit -Jobs 2    unattended (slower, safer on memory)
#   tools\esp-build.ps1 -ProjectDir MiniSplit -ActivateOnly   just set up ESP-IDF in this shell
#
# No ErrorActionPreference=Stop: Windows PowerShell 5.1 turns native-tool
# stderr (ESP-IDF's activation, idf.py progress) into terminating errors.
# Failures are thrown explicitly instead.
param(
    [Parameter(Mandatory)] [string]$ProjectDir,
    # Cap on parallel compiles: idf.py has no job limit, and a full-parallel
    # esp-matter build (~2000 files) runs this PC out of memory.
    [int]$Jobs = 4,
    [string]$Target = 'esp32c6',
    [switch]$ActivateOnly
)
$ProjectDir = (Resolve-Path $ProjectDir).Path

# This PC has two ESP-IDF tool installs for the same v5.4.1 source: the EIM one
# (C:\Espressif\tools) and the plain-upstream one (~\.espressif). A build/ made
# with one fully rebuilds -- or fails -- under the other, so use whichever made
# this project's build/ (new builds: plain upstream).
$upstream = 'C:\esp\v5.4.1\esp-idf\export.ps1'
$eim = 'C:\Espressif\tools\Microsoft.v5.4.1.PowerShell_profile.ps1'
$activate = $upstream
$cache = Join-Path $ProjectDir 'build\CMakeCache.txt'
if (Test-Path $cache) {
    $m = Select-String -Path $cache -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=(.*)$' | Select-Object -First 1
    if ($m -and $m.Matches[0].Groups[1].Value -like 'C:/Espressif/*') { $activate = $eim }
}
if ($env:HA_ESP_IDF_ACTIVATED -ne $activate) {
    if ($env:HA_ESP_IDF_ACTIVATED) {
        throw "This shell already set up $($env:HA_ESP_IDF_ACTIVATED), but $ProjectDir\build was made with $activate. Use a new PowerShell window."
    }
    . $activate *> $null
    $env:HA_ESP_IDF_ACTIVATED = $activate
}
$env:IDF_COMPONENT_CACHE_PATH = 'C:\icc'
if ($ActivateOnly) { return }

Push-Location $ProjectDir
try {
    if (-not (Test-Path version.txt)) { throw "$ProjectDir has no version.txt (the firmware version -- see tools/ESP_BUILDS.md)." }
    # set-target wipes build/ and regenerates sdkconfig: first build only.
    if (-not (Test-Path sdkconfig)) {
        idf.py set-target $Target
        if ($LASTEXITCODE -ne 0) { throw "set-target failed ($LASTEXITCODE)" }
    }
    if (-not (Test-Path build\build.ninja)) {
        idf.py reconfigure
        if ($LASTEXITCODE -ne 0) { throw "reconfigure failed ($LASTEXITCODE)" }
    }
    # The app image's timestamp tells whether anything was compiled: ninja
    # never says "no work to do" here, because ESP-IDF's bootloader
    # sub-build and partition size check run on every build.
    $appBefore = $null
    if (Test-Path build\project_description.json) {
        $old = Join-Path build ((Get-Content build\project_description.json -Raw | ConvertFrom-Json).app_bin)
        if (Test-Path $old) { $appBefore = (Get-Item $old).LastWriteTimeUtc }
    }
    # idf.py build has no job limit, so drive ninja directly.
    ninja -C build -j $Jobs
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
    $desc = Get-Content build\project_description.json -Raw | ConvertFrom-Json
    if ($appBefore -and (Get-Item (Join-Path build $desc.app_bin)).LastWriteTimeUtc -eq $appBefore) {
        Write-Host "Up to date: $($desc.app_bin) $($desc.project_version) -- nothing compiled."
    } else {
        # The firmware reports only version.txt, so record what it was built from here.
        $commit = git rev-parse --short HEAD
        $dirty = @(git status --porcelain -- .)
        $info = @(
            "version: $($desc.project_version)",
            "built:   $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
            "commit:  $commit$(if ($dirty.Count) { " plus uncommitted changes in this project:" })"
        ) + ($dirty | ForEach-Object { "  $_" })
        $info | Set-Content build\build-info.txt -Encoding utf8
        Write-Host "Built $($desc.app_bin) $($desc.project_version) from $commit$(if ($dirty.Count) { ' + uncommitted changes' })."
    }
} finally {
    Pop-Location
}
