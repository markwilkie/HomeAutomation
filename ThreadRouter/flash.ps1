# Build and flash the Thread router (MAC 58:8C:81:5D:89:1C) -- refuses any
# other board. Uses the EIM ESP-IDF install this project's build/ cache is
# configured with. Never erases flash: that would wipe its Matter
# commissioning (see CLAUDE.md).
param([string]$Port)
& "$PSScriptRoot\..\tools\flash-esp32.ps1" -ProjectDir $PSScriptRoot `
    -ExpectedMac '58:8C:81:5D:89:1C' `
    -IdfActivate 'C:\Espressif\tools\Microsoft.v5.4.1.PowerShell_profile.ps1' `
    -Port $Port
