# Build and flash the MiniSplit bridge (MAC 58:8C:81:5D:89:04) -- refuses any
# other board. Uses the plain-upstream ESP-IDF this project's build/ cache is
# configured with (see BUILD.md).
param([string]$Port)
& "$PSScriptRoot\..\tools\flash-esp32.ps1" -ProjectDir $PSScriptRoot `
    -ExpectedMac '58:8C:81:5D:89:04' `
    -IdfActivate 'C:\esp\v5.4.1\esp-idf\export.ps1' `
    -Port $Port
