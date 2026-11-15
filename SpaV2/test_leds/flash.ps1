# Flash SpaV2/test_leds -- only to the board whose MAC you pass. The SpaV2
# board's MAC isn't recorded yet, so -Mac is required (see README for how to
# read it). Refuses the MiniSplit bridge and the ThreadRouter outright.
param(
    [string]$Mac,
    [string]$Port
)
$protected = @{
    '58:8C:81:5D:89:04' = 'MiniSplit bridge'
    '58:8C:81:5D:89:1C' = 'ThreadRouter'
}
if (-not $Mac) {
    Write-Host "Pass the SpaV2 board's MAC: .\flash.ps1 -Mac XX:XX:XX:XX:XX:XX [-Port COMn]" -ForegroundColor Red
    Write-Host "Find it with only that board plugged in: python -m esptool --chip esp32c6 read_mac" -ForegroundColor Red
    exit 1
}
$Mac = $Mac.ToUpper()
if ($protected.ContainsKey($Mac)) {
    Write-Host "$Mac is the $($protected[$Mac]). Not flashing test_leds onto it." -ForegroundColor Red
    exit 1
}
& "$PSScriptRoot\..\..\tools\flash-esp32.ps1" -ProjectDir $PSScriptRoot `
    -ExpectedMac $Mac `
    -IdfActivate 'C:\esp\v5.4.1\esp-idf\export.ps1' `
    -Port $Port
