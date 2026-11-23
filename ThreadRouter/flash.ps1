# Build (only if something changed) and flash the Thread router (MAC 10:51:DB:0D:27:88, since 2026-11-15)
# -- refuses any other board. The original router board (58:8C:81:5D:89:1C)
# became the SpaV2 board on 2026-11-14. -NoBuild writes build/ as-is. Never erases flash: that
# would wipe its Matter commissioning (see CLAUDE.md).
param(
    [string]$Mac = '10:51:DB:0D:27:88',
    [string]$Port,
    [int]$Jobs = 4,
    [switch]$NoBuild
)
$taken = @{ '58:8C:81:5D:89:04' = 'MiniSplit bridge'; '58:8C:81:5D:89:1C' = 'SpaV2 board' }
if ($taken.ContainsKey($Mac.ToUpper())) { throw "$Mac is the $($taken[$Mac.ToUpper()]). Not flashing ThreadRouter onto it." }
& "$PSScriptRoot\..\tools\flash-esp32.ps1" -ProjectDir $PSScriptRoot `
    -ExpectedMac $Mac `
    -Port $Port -Jobs $Jobs -NoBuild:$NoBuild
