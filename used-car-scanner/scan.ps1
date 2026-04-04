param(
    [string]$Only,       # Run only this search (prefix name from searches.json)
    [string]$Platforms,  # Comma-separated platforms to scan (e.g. craigslist,facebook)
    [switch]$Schedule    # Run on a loop per schedule.json
)

$env:PYTHONIOENCODING = "utf-8"
$env:PYTHONUNBUFFERED = "1"
Set-Location $PSScriptRoot

# Azure OpenAI config
$env:AZURE_OPENAI_ENDPOINT = "https://mark-9211-resource.cognitiveservices.azure.com"
$env:AZURE_OPENAI_API_KEY = ""
$env:AZURE_OPENAI_DEPLOYMENT = "gpt-4.1"

# --- Logging helper ---
$logsDir = Join-Path $PSScriptRoot "logs"
if (-not (Test-Path $logsDir)) { New-Item -ItemType Directory -Path $logsDir | Out-Null }

function Start-ScanLog {
    $script:logFile = Join-Path $logsDir ("scan_{0}.log" -f (Get-Date -Format "yyyy-MM-dd_HHmmss"))
    "=== Scan started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ===" | Out-File -FilePath $script:logFile -Encoding utf8
    Log "Logging to $script:logFile"
}

function Stop-ScanLog {
    "=== Scan finished $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ===" | Out-File -FilePath $script:logFile -Append -Encoding utf8
    # Prune logs older than 30 days
    Get-ChildItem $logsDir -Filter "scan_*.log" |
        Where-Object { $_.LastWriteTime -lt (Get-Date).AddDays(-30) } |
        Remove-Item -Force
}

function Log([string]$msg) {
    Write-Host $msg
    if ($script:logFile) { $msg | Out-File -FilePath $script:logFile -Append -Encoding utf8 }
}

function Run-Py {
    # Run a Python command and capture output to both console and log file
    $argList = $args
    & py -3 @argList 2>&1 | ForEach-Object {
        $line = "$_"
        Write-Host $line
        if ($script:logFile) { $line | Out-File -FilePath $script:logFile -Append -Encoding utf8 }
    }
}

# --- Core scan function ---
function Invoke-Scan {
    param([bool]$OpenBrowser = $true)

    $searches = Get-Content "searches.json" -Raw | ConvertFrom-Json
    $out = "output"
    if (-not (Test-Path $out)) { New-Item -ItemType Directory -Path $out | Out-Null }

foreach ($name in $searches.PSObject.Properties.Name) {
    if ($Only -and $name -ne $Only) { continue }
    $s = $searches.$name
    $prefix = $name
    $make = $s.make
    $model = $s.model
    $minYear = $s.min_year
    $maxYear = $s.max_year
    $maxPrice = $s.max_price
    $minPrice = $s.min_price
    $maxMiles = $s.max_miles
    $title = $s.title

    Log ""
    Log "============================================"
    Log "  Search: $title ($prefix)"
    Log "============================================"

    Log "`n=== Saving baseline URLs for $prefix ==="
    Run-Py -c @"
import csv, os, shutil
urls = set()
prev_csv = 'output/${prefix}_results_by_distance.csv'
prev_bak = 'output/${prefix}_previous_results.csv'
prev_urls = 'output/${prefix}_previous_urls.txt'
# Carry forward existing baseline URLs (important for partial platform scans)
try:
    with open(prev_urls, encoding='utf-8') as f:
        for line in f:
            urls.add(line.strip())
except FileNotFoundError:
    pass
try:
    with open(prev_csv, encoding='utf-8') as f:
        for r in csv.DictReader(f):
            urls.add(r['url'])
    # Keep a copy so rebuild_html.py can look up details for removed listings
    shutil.copy2(prev_csv, prev_bak)
except FileNotFoundError:
    pass
with open(prev_urls, 'w', encoding='utf-8') as f:
    for u in sorted(urls):
        f.write(u + '\n')
print(f'Saved {len(urls)} previous URLs for $prefix')
"@
    if ($LASTEXITCODE -ne 0) { Log "Failed to save baseline for $prefix"; exit 1 }

    Log "`n=== Scanning all dealers for $title ==="
    $scanArgs = @("scan.py", "--make", $make, "--model", $model, "--min-year", $minYear, "--max-year", $maxYear, "--max-price", $maxPrice, "--max-miles", $maxMiles)
    if ($minPrice) { $scanArgs += @("--min-price", $minPrice) }
    $fbQuery = $s.facebook_query
    if ($fbQuery) { $scanArgs += @("--facebook-query", $fbQuery) }
    if ($Platforms) {
        # Partial platform scan: write to a temp file, then merge with existing results
        $scanArgs += @("--csv", "output/${prefix}_partial_results.csv")
        $scanArgs += @("--platforms", $Platforms)
    } else {
        $scanArgs += @("--csv", "output/${prefix}_results.csv")
    }
    Run-Py @scanArgs
    if ($LASTEXITCODE -ne 0) { Log "Scan failed for $prefix"; exit 1 }

    if ($Platforms) {
        Log "`n=== Merging partial results into full CSV ($prefix) ==="
        Run-Py -c @"
import csv, json, os
platforms = set('$Platforms'.lower().split(','))
# Get base_urls for scanned platforms from dealers.json
with open('dealers.json', encoding='utf-8') as f:
    dealers = json.load(f)
scanned_bases = set()
for d in dealers:
    if d.get('platform', '').lower() in platforms:
        base = d.get('base_url', '')
        if base:
            # Extract domain from base_url
            from urllib.parse import urlparse
            domain = urlparse(base).netloc.replace('www.', '')
            scanned_bases.add(domain)
full_csv = 'output/${prefix}_results.csv'
partial_csv = 'output/${prefix}_partial_results.csv'
# Read existing full results, keep rows from non-scanned platforms
kept = []
header = None
if os.path.exists(full_csv):
    with open(full_csv, encoding='utf-8') as f:
        reader = csv.reader(f)
        header = next(reader, None)
        if header:
            url_idx = header.index('url')
            for row in reader:
                url = row[url_idx] if url_idx < len(row) else ''
                from urllib.parse import urlparse
                row_domain = urlparse(url).netloc.replace('www.', '')
                if row_domain not in scanned_bases:
                    kept.append(row)
# Read partial results (new scan)
partial_rows = []
if os.path.exists(partial_csv):
    with open(partial_csv, encoding='utf-8') as f:
        reader = csv.reader(f)
        phdr = next(reader, None)
        if not header:
            header = phdr
        partial_rows = list(reader)
# Write merged
if header:
    with open(full_csv, 'w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(header)
        writer.writerows(kept)
        writer.writerows(partial_rows)
    print(f'Merged: {len(kept)} kept + {len(partial_rows)} new = {len(kept)+len(partial_rows)} total')
if os.path.exists(partial_csv):
    os.remove(partial_csv)
"@
        if ($LASTEXITCODE -ne 0) { Log "Merge failed for $prefix"; exit 1 }
    }

    Log "`n=== Enriching distance + location ($prefix) ==="
    Run-Py enrich_results.py $prefix
    if ($LASTEXITCODE -ne 0) { Log "Distance enrichment failed for $prefix"; exit 1 }

    Log "`n=== Enriching dealer names + ratings ($prefix) ==="
    Run-Py enrich_dealers.py $prefix
    if ($LASTEXITCODE -ne 0) { Log "Dealer enrichment failed for $prefix"; exit 1 }

    Log "`n=== Cross-referencing AutoTrader dealers ($prefix) ==="
    Run-Py match_at_dealers.py $prefix
    if ($LASTEXITCODE -ne 0) { Log "AutoTrader matching failed for $prefix"; exit 1 }

    Log "`n=== Re-enriching dealer ratings ($prefix) ==="
    Run-Py enrich_dealers.py $prefix
    if ($LASTEXITCODE -ne 0) { Log "Re-enrichment failed for $prefix"; exit 1 }

    Log "`n=== VIN decode via NHTSA ($prefix) ==="
    Run-Py enrich_vin.py $prefix
    if ($LASTEXITCODE -ne 0) { Log "VIN enrichment failed for $prefix"; exit 1 }

    # Pre-reject + LLM classification
    $filterPrompt = if ($s.prompt) { $s.prompt } else { "" }
    Log "`n=== Filtering ($prefix) ==="
    Run-Py filter_llm.py $prefix "$filterPrompt"
    if ($LASTEXITCODE -ne 0) { Log "Filtering failed for $prefix"; exit 1 }

    Log "`n=== Rebuilding HTML ($prefix) ==="
    Run-Py rebuild_html.py $prefix
    if ($LASTEXITCODE -ne 0) { Log "HTML rebuild failed for $prefix"; exit 1 }

    Log "`n=== Opening results for $prefix ==="
    if ($OpenBrowser) { Start-Process "chrome.exe" "$(Resolve-Path "output/${prefix}_results.html")" }

    # Rebuild combined page and publish after each search
    Log "`n=== Rebuilding combined HTML ==="
    Run-Py rebuild_combined.py
    if ($LASTEXITCODE -ne 0) { Log "Combined HTML rebuild failed" }

    Log "`n=== Publishing to Azure ==="
    $publishFiles = Get-ChildItem "output\*.html" | Where-Object { $_.Name -notmatch '^fb_' }
    foreach ($f in $publishFiles) {
        az storage blob upload --account-name usedcarscannersite --container-name '$web' --file $f.FullName --name $f.Name --content-type "text/html" --overwrite --only-show-errors 2>&1 | Out-Null
    }
    Log "Published $($publishFiles.Count) files to https://usedcarscannersite.z5.web.core.windows.net/"
}

if ($OpenBrowser) {
    Start-Process "chrome.exe" "$(Resolve-Path "output/combined_results.html")"
}

Log "`nAll searches done!"
}

# --- Find next scheduled time from times list ---
function Get-NextScheduledTime {
    param([string[]]$Times)
    $now = Get-Date
    $today = $now.Date
    # Build sorted list of today's and tomorrow's run times
    $candidates = @()
    foreach ($t in $Times) {
        $parsed = [datetime]::ParseExact($t, "HH:mm", $null)
        $candidates += $today.AddHours($parsed.Hour).AddMinutes($parsed.Minute)
        $candidates += $today.AddDays(1).AddHours($parsed.Hour).AddMinutes($parsed.Minute)
    }
    $candidates = $candidates | Sort-Object
    foreach ($c in $candidates) {
        if ($c -gt $now) { return $c }
    }
    return $candidates[0]  # fallback
}

# --- Interactive wait: prompt for commands until deadline ---
function Wait-WithPrompt {
    param([datetime]$Until)
    while ((Get-Date) -lt $Until) {
        $remaining = [math]::Round(($Until - (Get-Date)).TotalMinutes)
        $nextStr = $Until.ToString("HH:mm")
        Write-Host "`n[next scan at $nextStr, ${remaining}m away] " -ForegroundColor DarkCyan -NoNewline
        Write-Host 'Command (scan, scan <name>, status, next, quit, help): ' -ForegroundColor DarkCyan -NoNewline
        $line = $null
        while ((Get-Date) -lt $Until) {
            if ([Console]::KeyAvailable) {
                $line = Read-Host
                break
            }
            Start-Sleep -Milliseconds 500
        }
        if ($null -eq $line -or $line.Trim() -eq "") { continue }
        $parts = $line.Trim() -split '\s+', 2
        $cmd = $parts[0].ToLower()
        $arg = if ($parts.Count -gt 1) { $parts[1] } else { $null }

        switch ($cmd) {
            "scan" {
                if ($arg) { $script:Only = $arg }
                $label = if ($arg) { "Ad-hoc scan ($arg)" } else { "Ad-hoc scan" }
                Write-Host "`n--- $label ---" -ForegroundColor Yellow
                Start-ScanLog
                try { Invoke-Scan -OpenBrowser $false } catch { Write-Host "Scan error: $_" -ForegroundColor Red }
                Stop-ScanLog
                if ($arg) { $script:Only = "" }
                Write-Host "Ad-hoc scan complete." -ForegroundColor Green
            }
            "status" {
                $sched = Get-Content (Join-Path $PSScriptRoot "schedule.json") -Raw | ConvertFrom-Json
                Write-Host "  Schedule: $($sched.times -join ', ')" -ForegroundColor Cyan
                Write-Host "  Next scheduled scan: $($Until.ToString('yyyy-MM-dd HH:mm'))" -ForegroundColor Cyan
                $lastLog = Get-ChildItem $logsDir -Filter "scan_*.log" | Sort-Object Name | Select-Object -Last 1
                if ($lastLog) { Write-Host "  Last log: $($lastLog.Name)" -ForegroundColor Cyan }
            }
            "next" {
                Write-Host "Skipping wait - running next scheduled scan now." -ForegroundColor Yellow
                return
            }
            "quit" {
                Write-Host "Exiting scheduler." -ForegroundColor Yellow
                exit 0
            }
            "help" {
                Write-Host "  scan            Run a full scan now" -ForegroundColor Gray
                Write-Host '  scan <name>     Scan one search (e.g. scan Frontier)' -ForegroundColor Gray
                Write-Host "  status          Show schedule config and next run time" -ForegroundColor Gray
                Write-Host "  next            Skip wait, run scheduled scan immediately" -ForegroundColor Gray
                Write-Host "  quit            Stop the scheduler" -ForegroundColor Gray
            }
            default {
                Write-Host "Unknown command '${cmd}'. Type 'help' for options." -ForegroundColor Red
            }
        }
    }
}

# --- Main ---
if ($Schedule) {
    # Re-read schedule.json each iteration so you can edit it live
    $sched = Get-Content (Join-Path $PSScriptRoot "schedule.json") -Raw | ConvertFrom-Json
    $times = @($sched.times)
    Write-Host "`n*** Scheduled mode - daily at: $($times -join ', ') ***" -ForegroundColor Yellow
    Write-Host "Type 'help' for commands, 'quit' to stop" -ForegroundColor Yellow

    while ($true) {
        $sched = Get-Content (Join-Path $PSScriptRoot "schedule.json") -Raw | ConvertFrom-Json
        $times = @($sched.times)
        $nextRun = Get-NextScheduledTime -Times $times

        Write-Host "[$(Get-Date -Format 'HH:mm')] Next scan at $($nextRun.ToString('HH:mm'))" -ForegroundColor Yellow
        Wait-WithPrompt -Until $nextRun

        # Check if we're past the deadline (not skipped by user command returning early to re-wait)
        if ((Get-Date) -ge $nextRun) {
            Write-Host "`n[$(Get-Date -Format 'yyyy-MM-dd HH:mm')] Starting scheduled scan..." -ForegroundColor Yellow
            $scanStart = Get-Date
            Start-ScanLog
            try {
                Invoke-Scan -OpenBrowser $false
            } catch {
                Write-Host "Scan error: $_" -ForegroundColor Red
            }
            Stop-ScanLog
            $scanDuration = (Get-Date) - $scanStart

            # Handle overlap: skip any scheduled times that passed while scanning
            $sched = Get-Content (Join-Path $PSScriptRoot "schedule.json") -Raw | ConvertFrom-Json
            $times = @($sched.times)
            $nextAfterScan = Get-NextScheduledTime -Times $times
            $skipped = @()
            # Count how many times we jumped past
            $now = Get-Date
            $today = $now.Date
            foreach ($t in $times) {
                $parsed = [datetime]::ParseExact($t, "HH:mm", $null)
                $candidate = $today.AddHours($parsed.Hour).AddMinutes($parsed.Minute)
                if ($candidate -gt $scanStart -and $candidate -le $now) {
                    $skipped += $t
                }
            }
            if ($skipped.Count -gt 0) {
                $skipMsg = $skipped -join ', '
                Write-Host "  Scan took $([math]::Round($scanDuration.TotalMinutes))m - skipped overlapping: $skipMsg" -ForegroundColor DarkYellow
            }
        }
    }
} else {
    # One-shot run
    Start-ScanLog
    Invoke-Scan -OpenBrowser $true
    Stop-ScanLog
}
