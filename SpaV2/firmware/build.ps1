# Build SpaV2/firmware (ESP32-C6) if anything changed (seconds when nothing did). Same as
# ..\..\tools\esp-build.ps1 -ProjectDir SpaV2\firmware; see tools/ESP_BUILDS.md.
#   .\build.ps1            build
#   .\build.ps1 -Jobs 2    unattended (slower, lighter on memory)
param([int]$Jobs = 4)
& "$PSScriptRoot\..\..\tools\esp-build.ps1" -ProjectDir $PSScriptRoot -Jobs $Jobs
