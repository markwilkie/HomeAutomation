<#
.SYNOPSIS
  Renew the Lightroom sign-in used by the nightly edit saver on wyse (about every 14 days).

.DESCRIPTION
  Adobe makes the Lightroom sign-in last 14 days and refreshing doesn't extend it. When the daily
  brief or the dashboard says it's about to expire, run this (desktop shortcut "Renew Lightroom sign-in"):
    1. opens Adobe's sign-in in your browser; approve it
       (if the browser ends up on httpbin.org instead of "Signed in", paste that page's address when asked)
    2. copies the new sign-in to wyse (/mnt/data/appdata/photo-sync/lightroom-tokens.json, chmod 600)
    3. runs the edit saver on wyse once, so any missed edits are saved and the status turns green

  Requires: lightroom\.venv and lightroom\.env (ADOBE_CLIENT_ID) on this PC; ssh key access to wyse.
#>
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$py = Join-Path $here '.venv\Scripts\python.exe'
$wyse = 'mwilkie@192.168.15.30'
$remote = '/mnt/data/appdata/photo-sync/lightroom-tokens.json'

function Fail($msg) { Write-Host "`n✘ $msg" -ForegroundColor Red; Read-Host 'Press Enter to close'; exit 1 }

if (-not (Test-Path $py)) { Fail "Python environment missing: $py (see lightroom\README.md)" }
Push-Location $here
try {
    Write-Host 'Step 1 of 3: signing in to Adobe...' -ForegroundColor Cyan
    & $py -m lightroom_tool auth signin
    if ($LASTEXITCODE -ne 0) { Fail 'Sign-in did not complete.' }

    Write-Host 'Step 2 of 3: copying the sign-in to wyse...' -ForegroundColor Cyan
    $tmp = Join-Path $env:TEMP ("lr-tokens-{0}.json" -f [guid]::NewGuid())
    try {
        & $py -m lightroom_tool auth export-token $tmp | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail 'Could not export the sign-in.' }
        & scp -q $tmp "${wyse}:$remote.new"
        if ($LASTEXITCODE -ne 0) { Fail 'Could not copy to wyse (is it on, and is the ssh key set up?).' }
        & ssh $wyse "chmod 600 $remote.new && mv $remote.new $remote"
        if ($LASTEXITCODE -ne 0) { Fail 'Could not install the sign-in on wyse.' }
    } finally {
        Remove-Item -Force -ErrorAction SilentlyContinue $tmp
    }

    Write-Host 'Step 3 of 3: saving any missed edits now (can take a few minutes)...' -ForegroundColor Cyan
    & ssh $wyse '~/github/HomeAutomation/Wyse5070DebSetup/setup-photo-sync.sh --run-edits && tail -n 3 ~/photo-sync.log'
    Write-Host "`n✔ Lightroom sign-in renewed for 14 days." -ForegroundColor Green
} finally {
    Pop-Location
}
Read-Host 'Press Enter to close'
