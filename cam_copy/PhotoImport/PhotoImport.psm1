# PhotoImport.psm1 -- camera/card -> local staging -> NAS, verified file by file. Plan: ..\IMPROVEMENT_PLAN.md
#
# A run: preflight -> fixed list of every photo on every source -> compare with the import manifest
# (name + size) -> copy what's missing (camera -> C: staging -> NAS .incoming -> final folder, size-checked
# at each hop) -> repeat until nothing is missing -> verdict. New JPGs also land in the local Lightroom inbox.
# Never deletes anything from a card or the camera.

Set-StrictMode -Version 2.0

$script:LogFile = $null
function Write-ImportLog {
    param([string]$Message, [ValidateSet('INFO', 'OK', 'WARN', 'ERROR')][string]$Level = 'INFO')
    $line = '[{0:yyyy-MM-dd HH:mm:ss}] [{1}] {2}' -f (Get-Date), $Level, $Message
    $color = @{ INFO = 'Gray'; OK = 'Green'; WARN = 'Yellow'; ERROR = 'Red' }[$Level]
    Write-Host $line -ForegroundColor $color
    if ($script:LogFile) { Add-Content -LiteralPath $script:LogFile -Value $line -Encoding UTF8 }
}

# ---------------------------------------------------------------- config ----

function Get-PhotoImportConfig {
    param([Parameter(Mandatory)][string]$Path)
    $c = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $required = 'CameraName', 'Library', 'Extensions', 'FolderFormat', 'Share', 'LocalStaging', 'LightroomInbox',
                'ExportOutbox', 'Edited', 'Manifest', 'MaxPasses', 'LogDir', 'StatusUrl'
    $missing = $required | Where-Object { -not ($c.PSObject.Properties.Name -contains $_) }
    if ($missing) { throw "config.json is missing: $($missing -join ', ')" }
    $known = $required + @('_about')
    $extra = $c.PSObject.Properties.Name | Where-Object { $known -notcontains $_ }
    if ($extra) { throw "config.json has unknown settings (typo?): $($extra -join ', ')" }
    $c
}

# ------------------------------------------------------- pure functions ----

function Get-PhotoType {
    # '.RAF' -> 'raw', '.JPG' -> 'jpg', anything else -> $null
    param([string]$Extension, $Extensions)
    $e = $Extension.ToUpperInvariant()
    foreach ($t in $Extensions.PSObject.Properties) {
        if ($t.Value | Where-Object { $_.ToUpperInvariant() -eq $e }) { return $t.Name }
    }
    $null
}

function Get-ImportKey { param([string]$Name, [long]$Size) '{0}|{1}' -f $Name.ToUpperInvariant(), $Size }

function Get-DateFolder {
    # 2026-11-21 + 'yyyy\MM-dd' -> '2026\11-21'
    # Each level is formatted on its own: in .NET formats '\' escapes the next character.
    param([datetime]$Taken, [string]$Format)
    $inv = [Globalization.CultureInfo]::InvariantCulture
    (($Format -split '\\') | ForEach-Object { $Taken.ToString($_, $inv) }) -join '\'
}

function Get-ImportPlan {
    <#
      Pure: which photos still need importing, and where each goes.
      Photos: objects with Name, Size, Taken, Type (raw/jpg). Imported: hashtable of Get-ImportKey -> $true.
      Returns objects with the photo plus DestRel ('raw\2026\11-21\WJD01503.RAF'), InboxRel for JPGs
      ('2026-11-21\WJD01503.JPG'). The same photo on two cards (backup slot mode) is planned once.
    #>
    param([object[]]$Photos, [hashtable]$Imported, [string]$FolderFormat)
    $seen = @{}
    foreach ($p in @($Photos)) {
        if (-not $p.Type) { continue }
        $key = Get-ImportKey $p.Name $p.Size
        if ($Imported.ContainsKey($key) -or $seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        $folder = Get-DateFolder $p.Taken $FolderFormat
        [pscustomobject]@{
            Photo    = $p
            Key      = $key
            DestRel  = Join-Path (Join-Path $p.Type $folder) $p.Name
            InboxRel = if ($p.Type -eq 'jpg') { Join-Path $p.Taken.ToString('yyyy-MM-dd') $p.Name } else { $null }
        }
    }
}

function Get-EditedRelPath {
    # Same schema as the Lightroom edit saver: '2026\2026-11 November\2026-11-21 WJD01503.jpg'
    param([datetime]$Taken, [string]$BaseName)
    $inv = [Globalization.CultureInfo]::InvariantCulture
    $name = ($BaseName -replace '\s*\(\d+\)$', '').Trim()
    '{0}\{1} {2}\{3} {4}.jpg' -f $Taken.ToString('yyyy', $inv), $Taken.ToString('yyyy-MM', $inv),
        $Taken.ToString('MMMM', $inv), $Taken.ToString('yyyy-MM-dd', $inv), $name
}

function Get-Verdict {
    param([int]$RawTotal, [int]$JpgTotal, [int]$Missing, [int]$NewCount)
    if ($Missing -eq 0) {
        $done = $RawTotal + $JpgTotal - $NewCount
        "✔ All $RawTotal RAF + $JpgTotal JPG safe on NAS ($NewCount new, $done already imported)"
    } else {
        "✘ INCOMPLETE: $Missing photo$(if ($Missing -ne 1) {'s'}) not copied. Don't format the cards. Run Import Photos again."
    }
}

# --------------------------------------------------------------- manifest ----

function Read-ImportManifest {
    param([string]$Path)
    $h = @{}
    if (Test-Path -LiteralPath $Path) {
        foreach ($r in Import-Csv -LiteralPath $Path) { $h[(Get-ImportKey $r.Name ([long]$r.Size))] = $true }
    }
    $h
}

function Add-ImportManifestRow {
    param([string]$Path, [string]$Name, [long]$Size, [string]$Type, [datetime]$Taken, [string]$DestRel)
    $dir = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $row = [pscustomobject]@{ Name = $Name; Size = $Size; Type = $Type; Taken = $Taken.ToString('s')
                              Path = $DestRel; ImportedAt = (Get-Date).ToString('s') }
    $row | Export-Csv -LiteralPath $Path -Append -NoTypeInformation -Encoding UTF8
}

function New-ImportManifest {
    # First run / -Rescan: index everything already in the library, so it's never copied twice.
    param($Config)
    $path = $Config.Manifest
    $rows = foreach ($t in $Config.Library.PSObject.Properties) {
        if (-not (Test-Path -LiteralPath $t.Value)) { continue }
        Get-ChildItem -LiteralPath $t.Value -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notmatch '\\\.incoming\\' -and (Get-PhotoType $_.Extension $Config.Extensions) } |
            ForEach-Object {
                [pscustomobject]@{ Name = $_.Name; Size = $_.Length; Type = $t.Name; Taken = $_.LastWriteTime.ToString('s')
                                   Path = $_.FullName.Substring($Config.Library.($t.Name).Length).TrimStart('\')
                                   ImportedAt = 'indexed' }
            }
    }
    $dir = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    @($rows) | Export-Csv -LiteralPath $path -NoTypeInformation -Encoding UTF8
    Write-ImportLog "Indexed $(@($rows).Count) photos already in the library into the manifest" OK
}

# -------------------------------------------------------------- preflight ----

function Test-ImportPreflight {
    param($Config)
    $drive = $Config.Share.Drive
    if (-not (Test-Path "$drive\")) {
        Write-ImportLog "$drive is not connected; reconnecting to $($Config.Share.Unc)" WARN
        & net use $drive $Config.Share.Unc /persistent:yes | Out-Null
        if (-not (Test-Path "$drive\")) {
            throw ("$drive (the NAS) is not reachable. Check the Synology at http://192.168.15.10:5000/ " +
                   "(Control Panel > Shared Folder: 'photo' exists; File Services > SMB enabled), then run: " +
                   "net use $drive $($Config.Share.Unc) /persistent:yes")
        }
    }
    foreach ($t in $Config.Library.PSObject.Properties) {
        if (-not (Test-Path -LiteralPath $t.Value)) { New-Item -ItemType Directory -Force -Path $t.Value | Out-Null }
    }
    $probe = Join-Path $Config.Library.raw ".write-test-$PID"
    try { Set-Content -LiteralPath $probe -Value 'x'; Remove-Item -LiteralPath $probe }
    catch { throw "Can't write to $($Config.Library.raw): $($_.Exception.Message)" }
    foreach ($d in $Config.LocalStaging, $Config.LightroomInbox.Path, $Config.ExportOutbox) {
        if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Force -Path $d | Out-Null }
    }
    Write-ImportLog "NAS ($drive) connected and writable" OK
}

function Assert-FreeSpace {
    param([string]$Path, [long]$Bytes, [string]$What)
    $root = [IO.Path]::GetPathRoot((Resolve-Path -LiteralPath $Path).ProviderPath)
    $free = (Get-PSDrive -Name $root.Substring(0, 1)).Free
    if ($free -lt ($Bytes + 1GB)) {
        throw ("Not enough space on {0} for this import: need {1:N1} GB, {2:N1} GB free" -f $What, ($Bytes / 1GB), ($free / 1GB))
    }
}

# ---------------------------------------------------------------- sources ----

function Get-MtpSize {
    # Over MTP the X-T5 reports FolderItem.Size = 0; the real size is the System.Size property.
    param($Item)
    $s = $Item.ExtendedProperty('System.Size')
    if ($s) { return [long]$s }
    [long]$Item.Size
}

function Get-MtpTaken {
    # Over MTP, System.DateModified is empty and ModifyDate is 1899-12-30. System.Photo.DateTaken (or
    # System.DateCreated) holds the camera's clock time, handed back converted to UTC: convert it back so
    # the folder date is the camera's own date, the same as a card reader's file time.
    param($Item)
    foreach ($p in 'System.Photo.DateTaken', 'System.DateCreated') {
        $v = $Item.ExtendedProperty($p)
        if ($v) { return [datetime]::SpecifyKind([datetime]$v, 'Utc').ToLocalTime() }
    }
    $m = [datetime]$Item.ModifyDate
    if ($m.Year -gt 1980) { return $m }
    Get-Date
}

function Get-CameraPhotos {
    # Every photo on the camera's cards over USB (MTP), both slots. Returns @() if the camera isn't connected.
    param($Config)
    $shell = New-Object -ComObject Shell.Application
    $cam = @($shell.NameSpace(17).Items()) | Where-Object { $_.Name -eq $Config.CameraName } | Select-Object -First 1
    if (-not $cam) { return @() }
    $out = New-Object System.Collections.ArrayList
    $walk = {
        param($folder, $slot)
        foreach ($it in @($folder.Items())) {   # @(): snapshot the list before anything is copied
            if ($it.IsFolder) { & $walk $it.GetFolder $slot; continue }
            $type = Get-PhotoType ([IO.Path]::GetExtension($it.Name)) $Config.Extensions
            if (-not $type) { continue }
            [void]$out.Add([pscustomobject]@{ Source = "$($Config.CameraName) $slot"; Kind = 'Camera'; Name = $it.Name
                                             Size = Get-MtpSize $it; Taken = Get-MtpTaken $it; Type = $type; Item = $it })
        }
    }
    foreach ($storage in @($cam.GetFolder.Items())) {
        foreach ($slot in @($storage.GetFolder.Items())) {
            $dcim = @($slot.GetFolder.Items()) | Where-Object { $_.Name -eq 'DCIM' } | Select-Object -First 1
            if ($dcim) { & $walk $dcim.GetFolder $slot.Name }
        }
    }
    $out.ToArray()
}

function Get-CardReaderPhotos {
    # Every photo on any drive that looks like a Fujifilm card (DCIM\*_FUJI).
    param($Config)
    $out = New-Object System.Collections.ArrayList
    foreach ($d in Get-PSDrive -PSProvider FileSystem) {
        if ($d.Name -eq $Config.Share.Drive.TrimEnd(':') -or -not $d.Root) { continue }
        $dcim = Join-Path $d.Root 'DCIM'
        if (-not (Test-Path -LiteralPath $dcim)) { continue }
        if (-not (Get-ChildItem -LiteralPath $dcim -Directory -Filter '*_FUJI' -ErrorAction SilentlyContinue)) { continue }
        foreach ($f in Get-ChildItem -LiteralPath $dcim -Recurse -File -ErrorAction SilentlyContinue) {
            $type = Get-PhotoType $f.Extension $Config.Extensions
            if (-not $type) { continue }
            [void]$out.Add([pscustomobject]@{ Source = "card $($d.Name):"; Kind = 'Drive'; Name = $f.Name; Size = $f.Length
                                             Taken = $f.LastWriteTime; Type = $type; Item = $f.FullName })
        }
    }
    $out.ToArray()
}

# ------------------------------------------------------------------- copy ----

function Copy-ToStaging {
    # Camera/card -> C: staging, size-checked. Returns the staged path.
    param($Photo, [string]$Staging)
    $dest = Join-Path $Staging $Photo.Name
    if ((Test-Path -LiteralPath $dest) -and (Get-Item -LiteralPath $dest).Length -eq $Photo.Size) { return $dest }
    Remove-Item -LiteralPath $dest -Force -ErrorAction SilentlyContinue
    if ($Photo.Kind -eq 'Drive') {
        Copy-Item -LiteralPath $Photo.Item -Destination $dest
    } else {
        $shell = New-Object -ComObject Shell.Application
        $shell.NameSpace($Staging).CopyHere($Photo.Item, 4 + 16 + 512 + 1024)   # no UI, yes to all
        $deadline = (Get-Date).AddSeconds(180); $last = -1
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 500
            $len = if (Test-Path -LiteralPath $dest) { (Get-Item -LiteralPath $dest).Length } else { -1 }
            if ($len -eq $Photo.Size -and $len -eq $last) { break }   # complete and stable
            $last = $len
        }
    }
    if (-not (Test-Path -LiteralPath $dest) -or (Get-Item -LiteralPath $dest).Length -ne $Photo.Size) {
        Remove-Item -LiteralPath $dest -Force -ErrorAction SilentlyContinue
        throw "copy from $($Photo.Source) was incomplete"
    }
    $dest
}

function Copy-ToNas {
    # C: staging -> NAS .incoming (size-checked) -> final folder. A same-named, different-size file
    # already there (Fuji numbers repeat after 9999) gets _2, _3... Returns the final relative path.
    param([string]$Staged, $PlanItem, $Config)
    $root = $Config.Library.($PlanItem.Photo.Type)
    $rel = $PlanItem.DestRel.Substring($PlanItem.Photo.Type.Length + 1)
    $final = Join-Path $root $rel
    $n = 1
    while ((Test-Path -LiteralPath $final) -and (Get-Item -LiteralPath $final).Length -ne $PlanItem.Photo.Size) {
        $n++
        $final = Join-Path (Split-Path -Parent (Join-Path $root $rel)) (
            '{0}_{1}{2}' -f [IO.Path]::GetFileNameWithoutExtension($PlanItem.Photo.Name), $n, [IO.Path]::GetExtension($PlanItem.Photo.Name))
    }
    if (-not (Test-Path -LiteralPath $final)) {
        $incoming = Join-Path (Join-Path $root '.incoming') $PlanItem.Photo.Name
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $incoming), (Split-Path -Parent $final) | Out-Null
        Copy-Item -LiteralPath $Staged -Destination $incoming -Force
        if ((Get-Item -LiteralPath $incoming).Length -ne $PlanItem.Photo.Size) {
            Remove-Item -LiteralPath $incoming -Force; throw 'copy to the NAS was incomplete'
        }
        Move-Item -LiteralPath $incoming -Destination $final
    }
    $final.Substring($root.Length).TrimStart('\')
}

# ------------------------------------------------------------- extra jobs ----

function Move-ExportOutbox {
    # Fallback path for edits: files exported from Lightroom to the outbox -> Edited\YYYY\YYYY-MM Month\.
    param($Config)
    $files = @(Get-ChildItem -LiteralPath $Config.ExportOutbox -File -ErrorAction SilentlyContinue |
               Where-Object { $_.Extension -match '^\.(jpe?g|xmp|psd|psb|tiff?)$' })
    foreach ($f in $files) {
        $taken = $f.LastWriteTime
        try {
            Add-Type -AssemblyName System.Drawing
            $img = [Drawing.Image]::FromFile($f.FullName)
            try { $raw = [Text.Encoding]::ASCII.GetString($img.GetPropertyItem(36867).Value).Trim([char]0)
                  $taken = [datetime]::ParseExact($raw, 'yyyy:MM:dd HH:mm:ss', $null) } catch { }
            finally { $img.Dispose() }
        } catch { }
        $rel = Get-EditedRelPath $taken $f.BaseName
        if ($f.Extension -eq '.xmp') { $rel = Join-Path (Join-Path $taken.ToString('yyyy') '_recipes') $f.Name }
        if ($f.Extension -match '^\.(psd|psb|tiff?)$') { $rel = Join-Path '_photoshop' $f.Name }   # layered Photoshop copies
        $final = Join-Path $Config.Edited $rel
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $final) | Out-Null
        Copy-Item -LiteralPath $f.FullName -Destination "$final.part" -Force
        if ((Get-Item -LiteralPath "$final.part").Length -ne $f.Length) { Write-ImportLog "Outbox copy incomplete: $($f.Name)" WARN; continue }
        Move-Item -LiteralPath "$final.part" -Destination $final -Force
        Remove-Item -LiteralPath $f.FullName
        Write-ImportLog "Filed export: Edited\$rel" OK
    }
}

function Clear-LightroomInbox {
    # Delete inbox day-folders older than KeepDays, only if every file in them is in the manifest.
    param($Config, [hashtable]$Imported)
    $cut = (Get-Date).AddDays(-[int]$Config.LightroomInbox.KeepDays)
    foreach ($d in Get-ChildItem -LiteralPath $Config.LightroomInbox.Path -Directory -ErrorAction SilentlyContinue) {
        if ($d.LastWriteTime -gt $cut) { continue }
        $files = @(Get-ChildItem -LiteralPath $d.FullName -File -Recurse)
        $unsafe = @($files | Where-Object { -not $Imported.ContainsKey((Get-ImportKey $_.Name $_.Length)) })
        if ($unsafe.Count) { Write-ImportLog "Kept inbox $($d.Name): $($unsafe.Count) files not verified on the NAS" WARN; continue }
        Remove-Item -LiteralPath $d.FullName -Recurse -Force
        Write-ImportLog "Cleared old inbox folder $($d.Name) (all on the NAS)"
    }
}

function Get-SafetyStatusLine {
    # Backup + edit saver status from the wyse dashboard (photo-status.json). Best effort.
    param($Config)
    try {
        $s = Invoke-RestMethod -Uri $Config.StatusUrl -TimeoutSec 5
        $parts = @()
        if ($s.backup) { $parts += $(if ($s.backup.ok) { 'OneDrive backup OK (runs nightly at 2am)' } else { 'OneDrive backup FAILED last night' }) }
        if ($s.edits) {
            $parts += "Edits on NAS: $($s.edits.saved_total) of $($s.edits.edited_total)"
            if ($s.edits.sign_in_expires) {
                $days = [math]::Floor(([datetime]$s.edits.sign_in_expires - (Get-Date)).TotalDays)
                if ($days -lt 3) { $parts += "Lightroom sign-in expires in $days days: run Renew Lightroom sign-in" }
            }
        }
        if ($parts) { return ($parts -join ' · ') }
    } catch { }
    'Backup status unavailable (wyse dashboard not reachable)'
}

function Show-ImportToast {
    param([string]$Title, [string]$Text, [bool]$Ok)
    try {
        Add-Type -AssemblyName System.Windows.Forms, System.Drawing
        $n = New-Object System.Windows.Forms.NotifyIcon
        $n.Icon = if ($Ok) { [Drawing.SystemIcons]::Information } else { [Drawing.SystemIcons]::Warning }
        $n.BalloonTipTitle = $Title; $n.BalloonTipText = $Text; $n.Visible = $true
        $n.ShowBalloonTip(8000)
    } catch { }
}

# ------------------------------------------------------------ folder check ----

function Get-CameraNumber {
    # 'WJD01486 (1)' / '2026-08-20 WJD01486-Edit' -> 'WJD01486'; $null if there's no camera number
    param([string]$Name)
    if ($Name -match '([A-Za-z_]{3,4}\d{4,5})') { return $Matches[1].ToUpperInvariant() }
    $null
}

function Get-ExifTaken {
    # DateTimeOriginal from a JPEG/TIFF, or $null
    param([string]$Path)
    try {
        Add-Type -AssemblyName System.Drawing
        $img = [Drawing.Image]::FromFile($Path)
        try {
            $raw = [Text.Encoding]::ASCII.GetString($img.GetPropertyItem(36867).Value).Trim([char]0)
            return [datetime]::ParseExact($raw, 'yyyy:MM:dd HH:mm:ss', $null)
        } finally { $img.Dispose() }
    } catch { return $null }
}

function Get-FolderCheckResult {
    <#
      Pure: classify one file against what's known to be on the NAS.
      Originals: hashtable 'NAME.EXT' (upper) -> @(sizes on the NAS). Edited: hashtable camera number -> path.
      EditedByTime: hashtable 'yyyy-MM-dd HH:mm:ss' -> path (edited files by their capture time).
      Returns Status (Safe / Covered / OnlyHere) and Why.
    #>
    param([string]$Name, [long]$Size, [Nullable[datetime]]$Taken, [hashtable]$Originals, [hashtable]$Edited, [hashtable]$EditedByTime)
    $ext = [IO.Path]::GetExtension($Name).ToUpperInvariant()
    $base = ([IO.Path]::GetFileNameWithoutExtension($Name) -replace '\s*\(\d+\)$', '').ToUpperInvariant()
    $num = Get-CameraNumber $base
    $sizes = $Originals["$base$ext"]
    if ($ext -eq '.JPEG' -and -not $sizes) { $sizes = $Originals["$base.JPG"] }
    if ($sizes -and ($sizes -contains $Size)) { return [pscustomobject]@{ Status = 'Safe'; Why = 'same original is on the NAS' } }
    if ($num -and $Edited.ContainsKey($num) -and $base -ne $num) {
        return [pscustomobject]@{ Status = 'Safe'; Why = "edited version on the NAS: $($Edited[$num])" }
    }
    if ($sizes) {
        $big = ($sizes | Measure-Object -Maximum).Maximum
        $note = if ($num -and $Edited.ContainsKey($num)) { "; edited version too: $($Edited[$num])" } else { '' }
        $what = if ($big -gt $Size) { 'a smaller copy' } else { 'a different version' }
        return [pscustomobject]@{ Status = 'Covered'; Why = ('{0}; the NAS has the original ({1:N0} MB){2}' -f $what, ($big / 1MB), $note) }
    }
    if ($num -and $Edited.ContainsKey($num)) { return [pscustomobject]@{ Status = 'Safe'; Why = "edited version on the NAS: $($Edited[$num])" } }
    if ($Taken) {
        $k = ([datetime]$Taken).ToString('yyyy-MM-dd HH:mm:ss')
        if ($EditedByTime.ContainsKey($k)) {
            return [pscustomobject]@{ Status = 'Safe'; Why = "same photo as $($EditedByTime[$k]) (matched by the time it was taken)" }
        }
    }
    [pscustomobject]@{ Status = 'OnlyHere'; Why = 'not on the NAS' }
}

function Test-FolderAgainstNas {
    # Report, for every photo in a folder, whether the NAS already has it. Deletes nothing.
    param($Config, [Parameter(Mandatory)][string]$Folder)
    if (-not (Test-Path -LiteralPath $Folder)) { throw "No such folder: $Folder" }
    Test-ImportPreflight $Config
    $photoExt = '^\.(raf|jpe?g|tiff?|psd|psb|png|heic|dng)$'
    $files = @(Get-ChildItem -LiteralPath $Folder -Recurse -File | Where-Object { $_.Extension -match $photoExt })
    $other = @(Get-ChildItem -LiteralPath $Folder -Recurse -File | Where-Object { $_.Extension -notmatch $photoExt })
    if (-not (Test-Path -LiteralPath $Config.Manifest)) { New-ImportManifest $Config }

    $originals = @{}
    foreach ($r in Import-Csv -LiteralPath $Config.Manifest) {
        $k = $r.Name.ToUpperInvariant()
        if (-not $originals.ContainsKey($k)) { $originals[$k] = @() }
        $originals[$k] += [long]$r.Size
    }
    $edited = @{}; $editedFiles = @()
    if (Test-Path -LiteralPath $Config.Edited) {
        $editedFiles = @(Get-ChildItem -LiteralPath $Config.Edited -Recurse -File |
                         Where-Object { $_.FullName -notmatch '\\(\.incoming|_recipes)\\' -and $_.Extension -match $photoExt })
        foreach ($e in $editedFiles) {
            $n = Get-CameraNumber $e.BaseName
            if ($n -and -not $edited.ContainsKey($n)) { $edited[$n] = $e.FullName.Substring($Config.Edited.Length).TrimStart('\') }
        }
    }
    $byTime = $null
    $results = foreach ($f in $files) {
        $taken = $null
        $r = Get-FolderCheckResult $f.Name $f.Length $null $originals $edited @{}
        if ($r.Status -eq 'OnlyHere' -and $f.Extension -match '^\.(jpe?g|tiff?)$') {
            if ($null -eq $byTime) {   # only read Edited's capture times when it's needed
                $byTime = @{}
                foreach ($e in $editedFiles | Where-Object { $_.Extension -match '^\.(jpe?g|tiff?)$' }) {
                    $t = Get-ExifTaken $e.FullName
                    if ($t) { $byTime[$t.ToString('yyyy-MM-dd HH:mm:ss')] = $e.FullName.Substring($Config.Edited.Length).TrimStart('\') }
                }
            }
            $taken = Get-ExifTaken $f.FullName
            if ($taken) { $r = Get-FolderCheckResult $f.Name $f.Length $taken $originals $edited $byTime }
        }
        [pscustomobject]@{ File = $f.FullName.Substring($Folder.TrimEnd('\').Length + 1); MB = [math]::Round($f.Length / 1MB, 1); Status = $r.Status; Why = $r.Why }
    }
    $results = @($results)
    $mark = @{ Safe = '✔ safe   '; Covered = '≈ covered'; OnlyHere = '✘ ONLY HERE' }
    $color = @{ Safe = 'Green'; Covered = 'Cyan'; OnlyHere = 'Red' }
    Write-Host ''
    foreach ($r in $results | Sort-Object @{ e = { @{ OnlyHere = 0; Covered = 1; Safe = 2 }[$_.Status] } }, File) {
        Write-Host ('{0}  {1,-30} {2,6:N1} MB  {3}' -f $mark[$r.Status], $r.File, $r.MB, $r.Why) -ForegroundColor $color[$r.Status]
    }
    foreach ($o in $other) { Write-Host ('?          {0}  (not a photo; not checked)' -f $o.FullName.Substring($Folder.TrimEnd('\').Length + 1)) -ForegroundColor DarkGray }
    $only = @($results | Where-Object Status -eq 'OnlyHere')
    $ok = $results.Count - $only.Count
    Write-Host ''
    if ($only.Count -eq 0) {
        Write-ImportLog ("✔ {0} photos checked: all {0} are on the NAS (or a better version is). This folder is safe to delete{1}." -f $results.Count, $(if ($other.Count) { " (except the $($other.Count) non-photo files, not checked)" } else { '' })) OK
    } else {
        Write-ImportLog ("✘ {0} of {1} photos are ONLY in this folder: keep them, or import them (Fujifilm files) / put finished edits in the Export Outbox. The other {2} are safe to delete." -f $only.Count, $results.Count, $ok) ERROR
    }
    if ($only.Count) { return 2 }
    0
}

# ------------------------------------------------------------------- run ----

function Invoke-PhotoImport {
    param($Config, [switch]$Preview, [switch]$Rescan)
    $logDir = $Config.LogDir
    if (-not [IO.Path]::IsPathRooted($logDir)) { $logDir = Join-Path $PSScriptRoot "..\$logDir" }
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    $script:LogFile = Join-Path $logDir ('{0:yyyy-MM-dd_HHmm}.log' -f (Get-Date))

    Test-ImportPreflight $Config
    if ($Rescan -or -not (Test-Path -LiteralPath $Config.Manifest)) { New-ImportManifest $Config }
    if (-not $Preview) { Move-ExportOutbox $Config }

    $newCount = 0; $inboxDays = @{}; $failed = @{}
    $photos = @(); $imported = @{}
    for ($pass = 1; $pass -le [int]$Config.MaxPasses; $pass++) {
        $photos = @(Get-CameraPhotos $Config) + @(Get-CardReaderPhotos $Config)
        if ($pass -eq 1) {
            if (-not $photos.Count) {
                Write-ImportLog "No camera ($($Config.CameraName)) or Fujifilm card found. Is the camera on and plugged in?" WARN
                break
            }
            $bySrc = $photos | Group-Object Source | ForEach-Object { "$($_.Name): $($_.Count)" }
            Write-ImportLog "Found $($photos.Count) photos ($($bySrc -join '; '))"
        }
        $imported = Read-ImportManifest $Config.Manifest
        $todo = @(Get-ImportPlan $photos $imported $Config.FolderFormat)
        if (-not $todo.Count) { break }
        if ($Preview) {
            $todo | ForEach-Object { Write-ImportLog "would copy $($_.Photo.Source) $($_.Photo.Name) -> $($_.DestRel)" }
            break
        }
        $bytes = [long](($todo | ForEach-Object { $_.Photo.Size } | Measure-Object -Sum).Sum)
        Assert-FreeSpace $Config.LocalStaging $bytes 'C:'
        Assert-FreeSpace $Config.Library.raw $bytes 'the NAS'
        Write-ImportLog "Pass ${pass}: copying $($todo.Count) photos"
        $i = 0
        foreach ($t in $todo) {
            $i++
            try {
                $staged = Copy-ToStaging $t.Photo $Config.LocalStaging
                $rel = Copy-ToNas $staged $t $Config
                Add-ImportManifestRow $Config.Manifest $t.Photo.Name $t.Photo.Size $t.Photo.Type $t.Photo.Taken $rel
                if ($t.InboxRel) {
                    $inbox = Join-Path $Config.LightroomInbox.Path $t.InboxRel
                    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $inbox) | Out-Null
                    Move-Item -LiteralPath $staged -Destination $inbox -Force
                    $inboxDays[(Split-Path -Parent $inbox)] = $true
                } else {
                    Remove-Item -LiteralPath $staged -Force
                }
                $newCount++; $failed.Remove($t.Key)
                Write-ImportLog ("[{0}/{1}] {2} -> {3}\{4}" -f $i, $todo.Count, $t.Photo.Name, $t.Photo.Type, $rel) OK
            } catch {
                $failed[$t.Key] = "$($t.Photo.Name): $($_.Exception.Message)"
                Write-ImportLog ("[{0}/{1}] {2} failed: {3}" -f $i, $todo.Count, $t.Photo.Name, $_.Exception.Message) WARN
            }
        }
    }

    $imported = Read-ImportManifest $Config.Manifest
    $missing = @(Get-ImportPlan $photos $imported $Config.FolderFormat).Count
    if (-not $Preview) { Clear-LightroomInbox $Config $imported }
    $raw = @($photos | Where-Object Type -eq 'raw' | ForEach-Object { Get-ImportKey $_.Name $_.Size } | Sort-Object -Unique).Count
    $jpg = @($photos | Where-Object Type -eq 'jpg' | ForEach-Object { Get-ImportKey $_.Name $_.Size } | Sort-Object -Unique).Count

    Write-Host ''
    if ($photos.Count -and -not $Preview) {
        $verdict = Get-Verdict $raw $jpg $missing $newCount
        Write-ImportLog $verdict $(if ($missing) { 'ERROR' } else { 'OK' })
        foreach ($f in $failed.Values) { Write-ImportLog "  not copied: $f" WARN }
        if ($inboxDays.Count) { Write-ImportLog ("Add to Lightroom from: " + (($inboxDays.Keys | Sort-Object) -join ', ')) OK }
        Write-ImportLog (Get-SafetyStatusLine $Config)
        Show-ImportToast 'Import Photos' $verdict ($missing -eq 0)
    }
    Write-ImportLog "Log: $script:LogFile"
    if ($missing) { return 2 }
    0
}

Export-ModuleMember -Function Get-PhotoImportConfig, Get-PhotoType, Get-ImportKey, Get-DateFolder, Get-ImportPlan,
    Get-EditedRelPath, Get-Verdict, Read-ImportManifest, New-ImportManifest, Invoke-PhotoImport, Write-ImportLog,
    Get-CameraNumber, Get-FolderCheckResult, Test-FolderAgainstNas
