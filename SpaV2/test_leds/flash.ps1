# Flash SpaV2/test_leds to the SpaV2 board (MAC 58:8C:81:5D:89:1C -- the
# former ThreadRouter board, repurposed for SpaV2 on 2026-11-14). Refuses any
# other board.
param([string]$Port)
& "$PSScriptRoot\..\..\tools\flash-esp32.ps1" -ProjectDir $PSScriptRoot `
    -ExpectedMac '58:8C:81:5D:89:1C' `
    -IdfActivate 'C:\esp\v5.4.1\esp-idf\export.ps1' `
    -Port $Port
