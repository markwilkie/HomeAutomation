<#
.SYNOPSIS
  Import Photos: copy everything new from the X-T5 (over USB) or a Fujifilm card (card reader) to the NAS.

.DESCRIPTION
  One run copies every photo that isn't on the NAS yet, checks each file's size at every hop, repeats
  until nothing is missing, and ends with a verdict:
    ✔ All 62 RAF + 62 JPG safe on NAS (48 new, 76 already imported)
    ✘ INCOMPLETE: 3 photos not copied. Don't format the cards. Run Import Photos again.
  RAF -> P:\Xt5\raw\YYYY\MM-DD\, JPG -> P:\Xt5\jpg\YYYY\MM-DD\ (date taken). New JPGs also go to
  C:\Photos\Lightroom Inbox\YYYY-MM-DD\ so Lightroom can add them from the local disk.
  Also files anything in C:\Photos\Export Outbox\ into P:\Xt5\Edited\ (the manual fallback for edits).
  Never deletes anything from the camera or a card. Settings: import-config.json.

.PARAMETER Preview
  List what would be copied and where; change nothing.

.PARAMETER Rescan
  Rebuild the import manifest from what's already in P:\Xt5\raw and P:\Xt5\jpg.

.EXAMPLE
  .\Import-Photos.ps1            # the desktop shortcut runs this
  .\Import-Photos.ps1 -Preview
#>
param([switch]$Preview, [switch]$Rescan, [string]$ConfigPath = (Join-Path $PSScriptRoot 'import-config.json'),
      [switch]$NoPause)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'PhotoImport\PhotoImport.psm1') -Force
$code = 1
try {
    $config = Get-PhotoImportConfig $ConfigPath
    $code = Invoke-PhotoImport -Config $config -Preview:$Preview -Rescan:$Rescan
} catch {
    Write-ImportLog "Import could not run: $($_.Exception.Message)" ERROR
    $code = 1
}
if (-not $NoPause) { Read-Host "`nPress Enter to close" | Out-Null }
exit $code
