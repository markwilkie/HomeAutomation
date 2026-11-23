# Build (only if something changed) and flash the MiniSplit bridge (MAC 58:8C:81:5D:89:04) -- refuses any
# other board. -NoBuild writes build/ as-is. See tools/ESP_BUILDS.md.
param([string]$Port, [int]$Jobs = 4, [switch]$NoBuild)
& "$PSScriptRoot\..\tools\flash-esp32.ps1" -ProjectDir $PSScriptRoot `
    -ExpectedMac '58:8C:81:5D:89:04' `
    -Port $Port -Jobs $Jobs -NoBuild:$NoBuild
