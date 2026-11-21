# Pester 3.4 (built into Windows PowerShell 5.1):  Invoke-Pester .\cam_copy\tests
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $here '..\PhotoImport\PhotoImport.psm1') -Force

$ext = ('{"raw": [".RAF"], "jpg": [".JPG"]}' | ConvertFrom-Json)
function P($name, $size, $taken = '2026-11-21 09:12:33') {
    [pscustomobject]@{ Name = $name; Size = $size; Taken = [datetime]$taken
                       Type = (Get-PhotoType ([IO.Path]::GetExtension($name)) $ext); Source = 'test'; Kind = 'Drive'; Item = $name }
}

Describe 'Get-PhotoType' {
    It 'routes by extension, case-insensitive' {
        Get-PhotoType '.RAF' $ext | Should Be 'raw'
        Get-PhotoType '.jpg' $ext | Should Be 'jpg'
        Get-PhotoType '.MOV' $ext | Should BeNullOrEmpty
    }
}

Describe 'Get-ImportPlan' {
    It 'files by date taken, RAF to raw and JPG to jpg + inbox' {
        $plan = @(Get-ImportPlan @((P 'WJD01503.RAF' 44), (P 'WJD01503.JPG' 26)) @{} 'yyyy\MM-dd')
        $plan.Count | Should Be 2
        ($plan | Where-Object { $_.Photo.Type -eq 'raw' }).DestRel | Should Be 'raw\2026\11-21\WJD01503.RAF'
        ($plan | Where-Object { $_.Photo.Type -eq 'raw' }).InboxRel | Should BeNullOrEmpty
        ($plan | Where-Object { $_.Photo.Type -eq 'jpg' }).DestRel | Should Be 'jpg\2026\11-21\WJD01503.JPG'
        ($plan | Where-Object { $_.Photo.Type -eq 'jpg' }).InboxRel | Should Be '2026-11-21\WJD01503.JPG'
    }
    It 'skips photos already imported (name + size), whatever day they were imported' {
        $done = @{ (Get-ImportKey 'WJD01503.RAF' 44) = $true }
        $plan = @(Get-ImportPlan @((P 'WJD01503.RAF' 44), (P 'WJD01504.RAF' 45)) $done 'yyyy\MM-dd')
        $plan.Count | Should Be 1
        $plan[0].Photo.Name | Should Be 'WJD01504.RAF'
    }
    It 'treats a reused file number with a different size as a new photo (9999 wrap)' {
        $done = @{ (Get-ImportKey 'WJD00001.RAF' 44) = $true }
        @(Get-ImportPlan @((P 'WJD00001.RAF' 47)) $done 'yyyy\MM-dd').Count | Should Be 1
    }
    It 'plans the same photo on two cards only once' {
        @(Get-ImportPlan @((P 'WJD01503.JPG' 26), (P 'WJD01503.JPG' 26)) @{} 'yyyy\MM-dd').Count | Should Be 1
    }
    It 'ignores files that are not photos' {
        @(Get-ImportPlan @((P 'clip.MOV' 900)) @{} 'yyyy\MM-dd').Count | Should Be 0
    }
}

Describe 'Get-EditedRelPath' {
    It 'matches the edit saver schema and strips duplicate suffixes' {
        Get-EditedRelPath ([datetime]'2026-11-21 10:00') 'WJD01486 (1)' | Should Be '2026\2026-11 November\2026-11-21 WJD01486.jpg'
    }
}

Describe 'Get-Verdict' {
    It 'says safe when nothing is missing' {
        Get-Verdict 62 62 0 48 | Should Be '✔ All 62 RAF + 62 JPG safe on NAS (48 new, 76 already imported)'
    }
    It 'says do not format when something is missing' {
        Get-Verdict 62 62 3 45 | Should Match "INCOMPLETE: 3 photos not copied. Don't format"
    }
}

Describe 'Get-PhotoImportConfig' {
    It 'loads the real config' {
        $c = Get-PhotoImportConfig (Join-Path $here '..\import-config.json')
        $c.CameraName | Should Be 'X-T5'
        $c.Library.raw | Should Be 'P:\Xt5\raw'
    }
    It 'rejects unknown settings' {
        $tmp = Join-Path $env:TEMP "cfg-$PID.json"
        $j = Get-Content (Join-Path $here '..\import-config.json') -Raw | ConvertFrom-Json
        $j | Add-Member NoteProperty 'MaxPasess' 3
        $j | ConvertTo-Json -Depth 5 | Set-Content $tmp
        { Get-PhotoImportConfig $tmp } | Should Throw 'unknown settings'
        Remove-Item $tmp
    }
}
