# Flash SpaV2/test_phorp to the SpaV2 board (MAC 58:8C:81:5D:89:1C -- the
# former ThreadRouter board, repurposed for SpaV2 on 2026-11-14). Refuses any
# other board.
param([string]$Port, [int]$Jobs = 4, [switch]$NoBuild)
& "$PSScriptRoot\..\..\tools\flash-esp32.ps1" -ProjectDir $PSScriptRoot `
    -ExpectedMac '58:8C:81:5D:89:1C' `
    -Port $Port -Jobs $Jobs -NoBuild:$NoBuild
